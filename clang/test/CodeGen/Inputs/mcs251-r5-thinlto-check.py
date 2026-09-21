#!/usr/bin/env python3
"""WP4 round 5: the ThinLTO importing entry must apply the same MCS251 contract
verdict as the ordinary code-generation entry.

`clang -cc1 -fthinlto-index=<index> ... -emit-obj` dispatches into
`runThinLTOBackend` and RETURNS, before `EmitAssemblyHelper::emitAssembly` --
which is where the `DeferredChecks` guard lives -- ever runs. The deferral flag
is not set on this path either, so no later layer performs the check. Before
round 5 this was a fail-open: a module the ordinary entry rejects compiled to an
object here.

This helper builds a real ThinLTO index with llvm-as / opt / llvm-lto2, then
runs the same module through BOTH entries and requires them to agree. It also
checks that a supported module still compiles through the ThinLTO entry, so the
guard cannot pass by refusing everything.

Usage:
  mcs251-r5-thinlto-check.py --clang-cc1 <cc1>

The companion tools (llvm-as, opt, llvm-lto2) are resolved next to the cc1
binary, which is where the build tree puts them; clang's lit config does not
substitute %llvm-as, so deriving them here keeps the RUN line to one argument.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# The data layout clang emits for the default MCS251 memory contract. Taken
# from the compiler itself rather than hard-coded: the helper asks clang for the
# layout of a trivial module first.
LAYOUT_PROBE = "int mcs251_layout_probe;\n"

REJECTED = {
    # The case that actually leaked: the atomic load's result is dead, so the
    # ThinLTO import/optimize pipeline removes it before codegen and the
    # backend's own gates never see it. The ordinary entry rejects it in the
    # STRUCTURAL pre-optimization phase, which is exactly the phase the ThinLTO
    # dispatch skipped. Measured on the pre-round-5 compiler: ordinary rc=1,
    # ThinLTO rc=0 WITH an object.
    "dead-atomic-load": """
define i32 @dead_atomic() {
entry:
  %p = alloca i32, align 4
  store i32 0, ptr %p, align 4
  %v = load atomic i32, ptr %p seq_cst, align 4
  ret i32 7
}
""",
    # A live atomic operation: rejected by both entries (the backend's own gate
    # catches it), kept here as the neighbour that must not regress.
    "live-atomic-rmw": """
@shared = global i16 0
define i16 @live_rmw() {
entry:
  %v = atomicrmw add ptr @shared, i16 1 seq_cst
  ret i16 %v
}
""",
    "runtime-i64-mul": """
define i64 @fmul(i64 %a, i64 %b) {
entry:
  %r = mul i64 %a, %b
  ret i64 %r
}
""",
    "weak-definition": """
define weak i16 @w(i16 %a) {
  ret i16 %a
}
""",
}

# A module with no unsupported construct: the ThinLTO entry must still accept
# it, so the guard is a rejection of violations and not of the entry itself.
SUPPORTED = """
define i32 @fint(i32 %a) {
entry:
  %r = add i32 %a, 1
  ret i32 %r
}
"""

EXPORTED = {
    "dead-atomic-load": "_dead_atomic",
    "live-atomic-rmw": "_live_rmw",
    "runtime-i64-mul": "_fmul",
    "weak-definition": "_w",
    "supported": "_fint",
}

# Globals a module defines, which llvm-lto2 also wants resolved. Declared per
# case rather than scraped, so the check does not depend on llvm-lto2's own
# name mangling.
GLOBALS = {
    "dead-atomic-load": (),
    "live-atomic-rmw": ("_shared",),
    "runtime-i64-mul": (),
    "weak-definition": (),
    "supported": (),
}

CRASH_TEXT = ("PLEASE submit", "Stack dump", "PLEASE ATTACH", "crash backtrace")

failures = []


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main():
    parser = argparse.ArgumentParser()
    # %clang_cc1 expands to the cc1 binary PLUS its implicit flags
    # (-internal-isystem ... -nostdsysteminc), so trailing arguments are part
    # of the cc1 command line rather than errors.
    parser.add_argument("--clang-cc1", nargs="+", required=True)
    parser.add_argument("--llvm-as")
    parser.add_argument("--opt")
    parser.add_argument("--llvm-lto2")
    args, extra = parser.parse_known_args()
    if extra:
        args.clang_cc1 = args.clang_cc1 + extra
    cc1 = list(args.clang_cc1)

    # Resolve the companion tools next to the cc1 binary unless given.
    bindir = Path(cc1[0]).resolve().parent

    def sibling(name, override):
        if override:
            return override
        candidate = bindir / name
        if candidate.exists():
            return str(candidate)
        sys.exit(f"FAIL: could not find {name} next to {cc1[0]}")
    args.llvm_as = sibling("llvm-as", args.llvm_as)
    args.opt = sibling("opt", args.opt)
    args.llvm_lto2 = sibling("llvm-lto2", args.llvm_lto2)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        # Learn the exact data layout string from the compiler under test.
        probe = tmp / "probe.c"
        probe.write_text(LAYOUT_PROBE)
        result = run(cc1 + [
            "-triple", "mcs251-unknown-none", "-std=gnu11",
            "-O0", "-emit-llvm", str(probe), "-o", str(tmp / "probe.ll"),
        ])
        if result.returncode != 0:
            sys.exit(f"FAIL: layout probe failed: {result.stderr[:300]}")
        layout = None
        for line in (tmp / "probe.ll").read_text().splitlines():
            if line.startswith('target datalayout = "'):
                layout = line.split('"')[1]
                break
        if layout is None:
            sys.exit("FAIL: no target datalayout in the probe output")
        header = f'target datalayout = "{layout}"\ntarget triple = "mcs251-unknown-none"\n'

        def run_entry(name, body, use_index, extsym):
            """Run one module through one entry. Returns (rc, artifact, stderr)."""
            src = tmp / f"{name}.ll"
            src.write_text(header + body)
            bc = tmp / f"{name}.bc"
            r = run([args.llvm_as, str(src), "-o", str(bc)])
            if r.returncode != 0:
                failures.append(f"{name}: llvm-as failed: {r.stderr[:200]}")
                return None
            cmd = cc1 + [
                "-triple", "mcs251-unknown-none", "-O2",
            ]
            # The ThinLTO backend consumes the bitcode the THIN LINK produced
            # from, i.e. the summary-carrying module; a module without the
            # ThinLTO flag is not what this entry is handed in a real build.
            cc1_input = bc
            if use_index:
                summary = tmp / f"{name}.summary.bc"
                r = run([args.opt, "-module-summary", str(bc), "-o", str(summary)])
                if r.returncode != 0:
                    failures.append(f"{name}: opt -module-summary failed")
                    return None
                r = run([
                    args.llvm_lto2, "run", "--thinlto-distributed-indexes",
                    "-o", str(tmp / f"{name}.lto-out"),
                    f"-r={summary},{extsym},plx",
                    # Globals the module defines must be resolved too, or
                    # llvm-lto2 declines to write the index.
                    *[f"-r={summary},{g},plx" for g in GLOBALS.get(name, ())],
                    str(summary),
                ])
                index = Path(str(summary) + ".thinlto.bc")
                if not index.exists():
                    failures.append(
                        f"{name}: llvm-lto2 produced no index: {r.stderr[:200]}"
                    )
                    return None
                cmd += [f"-fthinlto-index={index}"]
                cc1_input = summary
            out = tmp / f"{name}.{'lto' if use_index else 'std'}.out"
            out.unlink(missing_ok=True)
            cmd += ["-emit-obj", str(cc1_input), "-o", str(out)]
            r = run(cmd)
            return r.returncode, out.exists(), r.stderr

        for name, body in REJECTED.items():
            extsym = EXPORTED[name]
            std = run_entry(name, body, False, extsym)
            thin = run_entry(name, body, True, extsym)
            if std is None or thin is None:
                continue
            std_rc, std_art, _std_err = std
            thin_rc, thin_art, thin_err = thin
            if std_rc != 1:
                failures.append(
                    f"{name}: ordinary entry expected status 1, got {std_rc}"
                )
            if std_art:
                failures.append(f"{name}: ordinary entry left an artifact")
            if thin_rc != 1:
                failures.append(
                    f"{name}: ThinLTO entry expected status 1, got {thin_rc}"
                )
            if thin_art:
                failures.append(f"{name}: ThinLTO entry left an artifact")
            if thin_rc != std_rc:
                failures.append(
                    f"{name}: entries disagree: ordinary {std_rc}, "
                    f"ThinLTO {thin_rc}"
                )
            if "MCS251 contract violation" not in thin_err:
                failures.append(
                    f"{name}: ThinLTO entry diagnostic is not the contract "
                    f"violation: {thin_err[:300]}"
                )
            for needle in CRASH_TEXT:
                if needle in thin_err:
                    failures.append(
                        f"{name}: ThinLTO entry printed crash text {needle!r}"
                    )

        # The positive control: the guard must not refuse a legal module.
        thin = run_entry("supported", SUPPORTED, True, EXPORTED["supported"])
        if thin is None:
            pass
        else:
            rc, art, err = thin
            if rc != 0:
                failures.append(
                    f"supported module: ThinLTO entry expected status 0, "
                    f"got {rc}: {err[:300]}"
                )
            if not art:
                failures.append("supported module: no object written")

    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    print(
        f"ThinLTO entry agrees with the ordinary entry on "
        f"{len(REJECTED)} rejected modules; supported module still compiles"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
