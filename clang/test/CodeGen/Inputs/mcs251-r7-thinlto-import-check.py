#!/usr/bin/env python3
"""WP4 round 7 (R2): the ThinLTO importing entry must judge the module the
IMPORTER PRODUCED, not only the caller module the invocation loaded.

Measured before this round's fix, with a real distributed index (llvm-lto2 run
--thinlto-distributed-indexes) built from two modules: a caller that only
DECLARES and calls `callee`, and a callee whose body contains a sequence the
optimizer removes (alloca / store / atomic load / ret). The caller module alone
carries no unsupported construct, so the pre-import structural check in
emitBackendOutput had nothing to judge; the imported body was then optimized
away before the arithmetic hook could see it either, and the ThinLTO entry
produced IR at rc=0 and an EM_MCS251 ELF object. The same post-import IR handed
to the ORDINARY entry exits 1 in all four O0/O2 x IR/object cells with the
atomic diagnostic. Round 7 adds a structural gate on the post-import module,
BEFORE the importing/optimization pipeline (Conf.PostImportModuleHook, composed
with any pre-existing hook rather than replacing it), so the importing entry
reaches the same verdict as the ordinary one.

This helper builds the index itself with llvm-as / opt / llvm-lto2 and runs the
real dispatch. Per callee variant, per O0/O2, in IR and object mode:
  * the ORDINARY entry on the CALLER ALONE must be rc=0 with an artifact: the
    caller really does carry no unsupported construct, so the ThinLTO verdict
    below is not the caller's own;
  * the ThinLTO entry on the same caller + index must exit exactly 1, with an
    ordinary `error:` diagnostic naming the atomic rule, no artifact, and no
    crash text -- at O0 AND at O2. The O2 cell is the structural gate's: the
    imported atomic load is dead by then, so an arithmetic-only hook cannot
    explain the rejection;
  * the atomic-free callee variant must import successfully and be ACCEPTED
    through the ThinLTO entry in every cell (rc=0, non-empty IR naming the
    call, or an EM_MCS251 ELF32 big-endian object with the defined symbol);
  * the same content LINKED into a single module (llvm-link) must be rejected
    by the ordinary entry in every cell, so the verdict is known to be about
    the CONTENT rather than about the ThinLTO entry.

Scope note, deliberately not widened: what is claimed is that the IMPORTING
BACKEND has no structural gap for the content it actually imports. Nothing here
claims that a whole link accepts or rejects unsupported programs; this helper
does not perform a link.

The accepted artifacts are replayed as negative controls (a 0-byte IR file, a
'not ELF' text, an object whose e_machine is changed to EM_386), so the artifact
assertions are exercised rather than asserted only by inspection.

Usage: mcs251-r7-thinlto-import-check.py --clang-cc1 <cc1 argv...>
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CRASH_TEXT = ("PLEASE submit", "Stack dump", "PLEASE ATTACH", "crash backtrace")
EM_MCS251 = 0x9999
SHT_SYMTAB = 2

LAYOUT = ("E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-"
          "p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-"
          "i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0")

HEADER = (f'target datalayout = "{LAYOUT}"\n'
          'target triple = "mcs251-unknown-none"\n')

CALLER = HEADER + """
declare i32 @callee(i32) addrspace(4)
define i32 @f(i32 %a) addrspace(4) {
entry:
  %v = call addrspace(4) i32 @callee(i32 %a)
  ret i32 %v
}
!mcs251.signatures = !{!0, !1}
!0 = !{!"_f", i32 1, i32 0, i32 0}
!1 = !{!"_callee", i32 1, i32 0, i32 0}
"""

# The dead sequence: the alloca/store/atomic load are all removable, and the
# function returns a constant, so at -O2 nothing of the body survives.
CALLEE_ATOMIC = HEADER + """
define i32 @callee(i32 %a) addrspace(4) {
entry:
  %p = alloca i32, align 1
  store i32 0, ptr %p, align 1
  %v = load atomic i32, ptr %p seq_cst, align 4
  ret i32 7
}
!mcs251.signatures = !{!0}
!0 = !{!"_callee", i32 1, i32 0, i32 0}
"""

CALLEE_CLEAN = HEADER + """
define i32 @callee(i32 %a) addrspace(4) {
entry:
  %r = add i32 %a, 1
  ret i32 %r
}
!mcs251.signatures = !{!0}
!0 = !{!"_callee", i32 1, i32 0, i32 0}
"""

VARIANTS = {
    "callee-atomic": (CALLEE_ATOMIC, dict(reject=True)),
    "callee-clean": (CALLEE_CLEAN, dict(reject=False)),
}

failures = []


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)


def elf_identity(path):
    blob = Path(path).read_bytes()
    if blob[:4] != b"\x7fELF":
        return f"not an ELF object (magic {blob[:4].hex()})", blob
    if len(blob) <= 64:
        return f"too small ({len(blob)} bytes)", blob
    if blob[4] != 1 or blob[5] != 2:
        return f"not ELF32 big-endian (class {blob[4]}, data {blob[5]})", blob
    e_machine = struct.unpack(">H", blob[18:20])[0]
    if e_machine != EM_MCS251:
        return f"e_machine is {e_machine:#x}, not EM_MCS251 ({EM_MCS251:#x})", blob
    return None, blob


def elf_has_symbol(path, want):
    ok, blob = elf_identity(path)
    if ok:
        return ok
    shoff = struct.unpack(">I", blob[32:36])[0]
    shentsize, shnum, shstrndx = struct.unpack(">HHH", blob[46:52])
    raw = [struct.unpack(">IIIIIIIIII",
                         blob[shoff + i * shentsize:
                              shoff + i * shentsize + 40])
           for i in range(shnum)]
    for sec in raw:
        _n, typ, _f, _a, off, size, link, _i, _al, entsize = sec
        if typ != SHT_SYMTAB or entsize == 0:
            continue
        st = raw[link][4]
        for j in range(size // entsize):
            so = off + j * entsize
            _nm, _v, _s, _inf, _o, shndx = struct.unpack(">IIIBBH",
                                                        blob[so:so + 16])
            start = st + _nm
            name = blob[start:blob.index(b"\0", start)].decode("ascii", "replace")
            if shndx != 0 and name == want:
                return None
    return f"defined symbol {want!r} not found"


def ir_ok(path):
    text = Path(path).read_text()
    if not text.strip():
        return "accepted IR artifact is empty"
    if 'target triple = "mcs251-unknown-none"' not in text:
        return "accepted IR artifact has no MCS251 target triple"
    if "@f" not in text:
        return "accepted IR artifact does not define @f"
    return None


def artifact_ok(path, mode):
    return ir_ok(path) if mode == "ir" else elf_has_symbol(path, "_f")


def negative_controls(samples):
    problems = []
    for label, ir_path, obj_path in samples:
        if ir_path is not None and Path(ir_path).exists():
            empty = Path(str(ir_path) + ".empty")
            empty.write_bytes(b"")
            if not ir_ok(empty):
                problems.append(f"{label}: a 0-byte IR artifact was accepted")
        if obj_path is not None and Path(obj_path).exists():
            fake = Path(str(obj_path) + ".notelf")
            fake.write_text("not ELF")
            if not elf_identity(fake)[0]:
                problems.append(f"{label}: a 'not ELF' object was accepted")
            blob = bytearray(Path(obj_path).read_bytes())
            blob[18:20] = (3).to_bytes(2, "big")  # e_machine = EM_386
            wrong = Path(str(obj_path) + ".emachine3")
            wrong.write_bytes(blob)
            if not elf_identity(wrong)[0]:
                problems.append(f"{label}: an object with e_machine=3 was accepted")
    return problems


def main():
    argv = [str(a) for a in sys.argv[1:]]
    if not argv:
        sys.exit("usage: mcs251-r7-thinlto-import-check.py <cc1 argv...>")
    cc1 = argv
    bindir = Path(cc1[0]).resolve().parent

    def sibling(name):
        cand = bindir / name
        if not cand.exists():
            sys.exit(f"FAIL: could not find {name} next to {cc1[0]}")
        return str(cand)

    llvm_as, opt, llvm_lto2, llvm_link = (
        sibling(n) for n in ("llvm-as", "opt", "llvm-lto2", "llvm-link"))

    ir_samples = []
    obj_samples = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        def write(name, text):
            p = tmp / name
            p.write_text(text)
            return p

        def entry_cmd(opt_level, mode, index, inp, out):
            cmd = cc1 + ["-triple", "mcs251-unknown-none", f"-{opt_level}"]
            if mode == "ir":
                cmd += ["-emit-llvm"]
            else:
                cmd += ["-emit-obj", "-mllvm", "-mcs251-object-format=elf"]
            if index is not None:
                cmd += [f"-fthinlto-index={index}"]
            out = Path(out)
            out.unlink(missing_ok=True)
            return cmd + [str(inp), "-o", str(out)], out

        caller_ll = write("caller.ll", CALLER)
        caller_bc = tmp / "caller.bc"
        r = run([llvm_as, caller_ll, "-o", caller_bc])
        if r.returncode != 0:
            sys.exit(f"FAIL: llvm-as caller: {r.stderr[:200]}")

        for name, (callee_text, opts) in VARIANTS.items():
            reject = opts["reject"]
            callee_ll = write(f"{name}.ll", callee_text)
            callee_bc = tmp / f"{name}.bc"
            r = run([llvm_as, callee_ll, "-o", callee_bc])
            if r.returncode != 0:
                failures.append(f"{name}: llvm-as callee failed: {r.stderr[:150]}")
                continue
            caller_sum = tmp / f"{name}.caller.sum.bc"
            callee_sum = tmp / f"{name}.callee.sum.bc"
            for src, dst in ((caller_bc, caller_sum), (callee_bc, callee_sum)):
                r = run([opt, "-module-summary", src, "-o", dst])
                if r.returncode != 0:
                    failures.append(f"{name}: opt -module-summary failed")
                    break
            else:
                # A REAL distributed index over both modules: the caller
                # exports/defines _f and only DECLARES _callee; the callee
                # defines and exports _callee.
                r = run([llvm_lto2, "run", "--thinlto-distributed-indexes",
                         "-o", str(tmp / f"{name}.lto-out"),
                         str(caller_sum), str(callee_sum),
                         f"-r={caller_sum},_f,plx",
                         f"-r={caller_sum},_callee,",
                         f"-r={callee_sum},_callee,pl"])
                index = Path(str(caller_sum) + ".thinlto.bc")
                if r.returncode != 0 or not index.exists():
                    failures.append(
                        f"{name}: llvm-lto2 produced no index: {r.stderr[:150]}")
                    continue

            # The same content linked into one module, for the ordinary entry.
            merged = tmp / f"{name}.merged.bc"
            r = run([llvm_link, str(caller_bc), str(callee_bc), "-o", merged])
            if r.returncode != 0:
                failures.append(f"{name}: llvm-link failed: {r.stderr[:150]}")
                continue

            for opt_level in ("O0", "O2"):
                for mode in ("ir", "obj"):
                    cell = f"{opt_level}.{mode}"
                    # (a) ordinary entry, caller ALONE: no unsupported content.
                    cmd, out = entry_cmd(opt_level, mode, None, caller_bc,
                                         tmp / f"{name}.caller.{cell}.out")
                    r = run(cmd)
                    label = f"{name}.caller-alone.{cell}"
                    if r.returncode != 0:
                        failures.append(
                            f"{label}: caller alone expected 0, got "
                            f"{r.returncode}: {r.stderr[:150]}")
                    elif not out.exists():
                        failures.append(f"{label}: caller alone left no artifact")

                    # (b) the ThinLTO entry on caller + real distributed index.
                    cmd, out = entry_cmd(opt_level, mode, index, caller_sum,
                                         tmp / f"{name}.thin.{cell}.out")
                    r = run(cmd)
                    label = f"{name}.thinlto.{cell}"
                    if reject:
                        if r.returncode != 1:
                            failures.append(
                                f"{label}: expected status 1, got "
                                f"{r.returncode}: {r.stderr[:200]}")
                        if out.exists():
                            failures.append(
                                f"{label}: rejected compile left an artifact")
                        if "error:" not in r.stderr or "atomic" not in r.stderr:
                            failures.append(
                                f"{label}: expected an ordinary atomic "
                                f"diagnostic, got: {r.stderr[:200]}")
                        for needle in CRASH_TEXT:
                            if needle in r.stderr:
                                failures.append(
                                    f"{label}: crash text {needle!r} present")
                    else:
                        if r.returncode != 0:
                            failures.append(
                                f"{label}: expected status 0, got "
                                f"{r.returncode}: {r.stderr[:200]}")
                        elif not out.exists():
                            failures.append(
                                f"{label}: accepted compile left no artifact")
                        else:
                            bad = artifact_ok(out, mode)
                            if bad:
                                failures.append(f"{label}: artifact: {bad}")
                            elif mode == "ir":
                                ir_samples.append((label, out, None))
                            else:
                                obj_samples.append((label, None, out))

                    # (c) the same content in one module, ordinary entry.
                    cmd, out = entry_cmd(opt_level, mode, None, merged,
                                         tmp / f"{name}.merged.{cell}.out")
                    r = run(cmd)
                    label = f"{name}.merged.{cell}"
                    if reject:
                        if r.returncode != 1:
                            failures.append(
                                f"{label}: linked content expected 1, got "
                                f"{r.returncode}: {r.stderr[:200]}")
                    elif r.returncode != 0:
                        failures.append(
                            f"{label}: linked clean content expected 0, got "
                            f"{r.returncode}: {r.stderr[:200]}")

        samples = ir_samples + obj_samples
        if not samples:
            failures.append("artifact negative controls: no accepted artifact "
                            "was collected to build them from")
        else:
            for line in negative_controls(samples):
                failures.append(line)

    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    print(f"ThinLTO importing entry vs the imported content: "
          f"{len(VARIANTS)} callees x O0/O2 x IR/obj, caller-alone and "
          f"linked-module controls, negative controls replayed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
