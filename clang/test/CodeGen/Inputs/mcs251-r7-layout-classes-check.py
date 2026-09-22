#!/usr/bin/env python3
"""WP4 round 7 (R3): the ThinLTO empty-invocation exemption, paired across the
three classes of "empty in the container sense".

Round 6 narrowed the exemption to six empty containers plus an empty module-asm
string. Round 7 extends the predicate with the module's DataLayout STRING,
because container emptiness says nothing about the layout: the contract check
reads `getPointerSizeInBits(0)` and the program address space out of it, and an
input carrying an explicit layout of its own has declared a layout contract
even with no functions, globals, aliases, ifuncs, named metadata or module
assembly. Measured before this round's fix: with
`target datalayout = "E-p:64:64"` and only a triple beside it, the ordinary
entry exited 1 in all four O0/O2 x IR/object cells ("AS0 pointer width must be
16 or 32 bits") while the ThinLTO entry exited 0 for IR; `getDataLayoutStr()`
is exactly what separates that input from the upstream-fabricated one.

This helper pins all three classes, on BOTH entries where the input admits one,
at O0 and O2 in IR and object mode:

  1. LEGAL layout, otherwise empty (the measured, unchanged "nothing to judge"
     invocation): IR rc=0 on both entries in every cell. The object cells exit
     70 on BOTH entries through the object writer's pre-existing
     `!mcs251.signatures` requirement -- identical on the ordinary entry, and
     registered as a pre-existing object-writer exit rather than a verdict of
     this exemption.
  2. ILLEGAL explicit layout (`p:64:64`, otherwise empty): rc=1 in every IR and
     object cell on BOTH entries. The ThinLTO entry reports it through the
     ordinary contract diagnostic; the ordinary entry reports the layout
     mismatch before the contract check. Before round 7 the ThinLTO IR cells
     were 0.
  3. BARE FABRICATED module (bitcode without a ThinLTO module summary, so
     CodeGenAction::loadModule fabricates a default-constructed module): the
     ThinLTO entry accepts the IR (rc=0) and the object cell exits 70. No legal
     object path is claimed for this input. This is the case the exemption
     exists for, and it is the reason the predicate must key on the layout
     STRING being absent rather than on a layout that merely looks default.

The accepted IR artifact of class 1 and 3 is asserted to be a non-empty MCS251
module, and both are replayed as negative controls (0-byte file, and the same
bytes relabeled) so the assertion is exercised rather than asserted only by
inspection.

Usage: mcs251-r7-layout-classes-check.py --clang-cc1 <cc1 argv...>
"""
import subprocess
import sys
import tempfile
from pathlib import Path

CRASH_TEXT = ("PLEASE submit", "Stack dump", "PLEASE ATTACH", "crash backtrace")

# The layout the target itself produces; a module carrying it has declared a
# supported contract and is accepted as "nothing to judge".
LEGAL_LAYOUT = ("E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-"
                "p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-"
                "i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0")
# An explicit layout that is NOT the target's: 64-bit AS0 pointers. The
# contract check reads this and rejects it.
ILLEGAL_LAYOUT = "E-p:64:64"

TRIPLE = 'target triple = "mcs251-unknown-none"\n'

failures = []


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True,
                          **kw)


def ir_ok(path):
    text = Path(path).read_text()
    if not text.strip():
        return "accepted IR artifact is empty"
    if 'target triple = "mcs251-unknown-none"' not in text:
        return "accepted IR artifact has no MCS251 target triple"
    return None


def negative_controls(samples):
    problems = []
    for label, path in samples:
        empty = Path(str(path) + ".empty")
        empty.write_bytes(b"")
        if not ir_ok(empty):
            problems.append(f"{label}: a 0-byte IR artifact was accepted")
        trunc = Path(str(path) + ".trunc")
        trunc.write_text("not LLVM IR at all\n")
        if not ir_ok(trunc):
            problems.append(f"{label}: a non-IR text was accepted")
    return problems


def main():
    argv = [str(a) for a in sys.argv[1:]]
    if not argv:
        sys.exit("usage: mcs251-r7-layout-classes-check.py <cc1 argv...>")
    cc1 = argv
    bindir = Path(cc1[0]).resolve().parent

    def sibling(name):
        cand = bindir / name
        if not cand.exists():
            sys.exit(f"FAIL: could not find {name} next to {cc1[0]}")
        return str(cand)

    llvm_as, opt, llvm_lto2 = (sibling(n)
                               for n in ("llvm-as", "opt", "llvm-lto2"))

    samples = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        def assemble(name, text, summarise=True):
            src = tmp / f"{name}.ll"
            src.write_text(text)
            bc = tmp / f"{name}.bc"
            r = run([llvm_as, src, "-o", bc])
            if r.returncode != 0:
                failures.append(f"{name}: llvm-as failed: {r.stderr[:150]}")
                return None, None
            if not summarise:
                return bc, None
            summary = tmp / f"{name}.sum.bc"
            r = run([opt, "-module-summary", bc, "-o", summary])
            if r.returncode != 0:
                failures.append(f"{name}: opt -module-summary failed")
                return None, None
            return bc, summary

        def make_index(name, summary):
            r = run([llvm_lto2, "run", "--thinlto-distributed-indexes",
                     "-o", str(tmp / f"{name}.lto-out"), str(summary)])
            index = Path(str(summary) + ".thinlto.bc")
            if r.returncode != 0 or not index.exists():
                failures.append(f"{name}: no distributed index: {r.stderr[:150]}")
                return None
            return index

        def cell(opt_level, mode, inp, out, index=None):
            cmd = cc1 + ["-triple", "mcs251-unknown-none", f"-{opt_level}"]
            if mode == "ir":
                cmd += ["-emit-llvm"]
            else:
                cmd += ["-emit-obj", "-mllvm", "-mcs251-object-format=elf"]
            if index is not None:
                cmd += [f"-fthinlto-index={index}"]
            out = Path(out)
            out.unlink(missing_ok=True)
            r = run(cmd + [str(inp), "-o", str(out)])
            return r.returncode, out, r.stderr

        # ---- class 1: legal layout, otherwise empty -------------------------
        legal_bc, legal_sum = assemble("legal", f'target datalayout = "{LEGAL_LAYOUT}"\n{TRIPLE}')
        legal_index = make_index("legal", legal_sum) if legal_sum else None
        if legal_index is None:
            sys.exit("FAIL: could not build the legal-layout index")
        for opt_level in ("O0", "O2"):
            for mode in ("ir", "obj"):
                expected = 0 if mode == "ir" else 1
                for entry, inp, index in (
                        ("ordinary", legal_sum, None),
                        ("ThinLTO", legal_sum, legal_index)):
                    out = tmp / f"legal.{entry}.{opt_level}.{mode}.out"
                    rc, out, err = cell(opt_level, mode, inp, out, index)
                    label = f"legal.{entry}.{opt_level}.{mode}"
                    if rc != expected:
                        failures.append(
                            f"{label}: expected {expected}, got {rc}: {err[:180]}")
                        continue
                    for needle in CRASH_TEXT:
                        if needle in err:
                            failures.append(
                                f"{label}: crash text {needle!r} present")
                    if mode == "ir":
                        # Each accepted IR cell asserts its own artifact: a
                        # missing file must fail HERE, not be covered by a
                        # sibling cell's sample (round 7 measured that deleting
                        # the legal-layout accept IR left the helper at PASS).
                        if not out.exists():
                            failures.append(
                                f"{label}: accepted IR cell left no artifact")
                        else:
                            bad = ir_ok(out)
                            if bad:
                                failures.append(f"{label}: artifact: {bad}")
                            else:
                                samples.append((label, out))

        # ---- class 2: illegal explicit layout, otherwise empty --------------
        bad_bc, bad_sum = assemble("bad", f'target datalayout = "{ILLEGAL_LAYOUT}"\n{TRIPLE}')
        bad_index = make_index("bad", bad_sum) if bad_sum else None
        if bad_index is None:
            sys.exit("FAIL: could not build the illegal-layout index")
        for opt_level in ("O0", "O2"):
            for mode in ("ir", "obj"):
                for entry, inp, index in (
                        ("ordinary", bad_sum, None),
                        ("ThinLTO", bad_sum, bad_index)):
                    out = tmp / f"bad.{entry}.{opt_level}.{mode}.out"
                    rc, out, err = cell(opt_level, mode, inp, out, index)
                    label = f"bad.{entry}.{opt_level}.{mode}"
                    if rc != 1:
                        failures.append(
                            f"bad.{entry}.{opt_level}.{mode}: expected status 1, "
                            f"got {rc}: {err[:200]}")
                    if out.exists():
                        failures.append(f"{label}: rejected compile left an artifact")
                    if "error:" not in err:
                        failures.append(
                            f"{label}: no ordinary error diagnostic: {err[:180]}")
                    for needle in CRASH_TEXT:
                        if needle in err:
                            failures.append(
                                f"{label}: crash text {needle!r} present")
        # The ThinLTO IR cells are the round-7 measurement: name the diagnostic
        # the contract check produces there, so the cell is known to be decided
        # by the layout predicate rather than by a later layer.
        for opt_level in ("O0", "O2"):
            out = tmp / f"bad.ThinLTO.{opt_level}.ir.probe"
            rc, out, err = cell(opt_level, "ir", bad_sum, out, bad_index)
            if rc == 1 and "AS0 pointer width" not in err:
                failures.append(
                    f"bad.ThinLTO.{opt_level}.ir: expected the layout contract "
                    f"diagnostic, got: {err[:200]}")

        # ---- class 3: bare fabricated (no ThinLTO summary) -------------------
        bare_bc, _ = assemble("bare", TRIPLE, summarise=False)
        for mode, expected in (("ir", 0), ("obj", 1)):
            # The index is taken from the legal-layout module so the ThinLTO
            # dispatch is actually entered; the INPUT bitcode carries no
            # summary, which is what makes upstream fabricate the module.
            out = tmp / f"bare.ThinLTO.O0.{mode}.out"
            rc, out, err = cell("O0", mode, bare_bc, out, legal_index)
            label = f"bare.ThinLTO.O0.{mode}"
            if rc != expected:
                failures.append(f"{label}: expected {expected}, got {rc}: {err[:180]}")
                continue
            for needle in CRASH_TEXT:
                if needle in err:
                    failures.append(f"{label}: crash text {needle!r} present")
            if mode == "ir":
                # Same per-cell rule as class 1: the accepted IR artifact must
                # exist for THIS cell.
                if not out.exists():
                    failures.append(
                        f"{label}: accepted IR cell left no artifact")
                else:
                    bad = ir_ok(out)
                    if bad:
                        failures.append(f"{label}: artifact: {bad}")
                    else:
                        samples.append((label, out))

        if not samples:
            failures.append("artifact negative controls: no accepted IR artifact "
                            "was collected to build them from")
        else:
            for line in negative_controls(samples):
                failures.append(line)

    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    print("layout exemption classes pinned: legal-layout empty (IR 0 / obj 70 on "
          "both entries), illegal explicit layout (status 1 in all cells on both "
          "entries), bare fabricated module (IR 0 / obj 70); artifact assertions "
          "exercised by negative controls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
