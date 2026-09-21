#!/usr/bin/env python3
"""WP4 round 6: the six-cell matrix for the C++ condition-variable rule and
the explicitly registered ordinary-function-body vs StmtExpr boundary.

Part 1 -- condition variables, the paired positive half. The REJECTED half
(the four statement forms whose condition initializer contains an atomic
operation, plus `__c11_atomic_init`) runs in mcs251-r5-obj-check.py. This
helper runs the ACCEPTED counterparts through all six cells -- O0/O2 x
syntax/IR/object -- and requires status 0 with a real artifact on the
emitting modes, so the rejection is known to be about the initializer and
not about the statement form itself.

Part 2 -- the conservative boundary, registered in the ledger as a policy
asymmetry (it is fail-closed and predates round 4; it is NOT a unified
accept policy). Constant-false-branch suppression applies only where the
check point has the full selection context: inside a GNU statement
expression the full-expression is deferred to the outer verdict and the
statement walker can prune an integer-constant-conditioned subtree with no
external label or case/default entry. An independent full expression in an
ORDINARY function body is still checked in ActOnFinishFullExpr with no
outer statement-level reachability suppression, so `if(0)`/`while(0)`/
`for(;0;)` there can still report the atomic-family diagnostic while the
corresponding statement expression passes Sema. Also, a Sema acceptance is
not an IR/object acceptance: if CodeGen keeps the atomic IR, the
pre-optimization structural check still rejects it.

The measured verdicts this helper pins (real <stdatomic.h>, `atomic_int *p`):

  shape                                    syntax O0/O2   IR/obj O0/O2
  ({ while(0){ atomic_load(p); } 1; })     rc=0           rc=1
  ({ for(;0;){ atomic_load(p); } 1; })     rc=0           rc=1
  ({ if(0){ atomic_load(p); } 1; })        rc=0           rc=0
  relaxed fence, same three shapes         rc=0           rc=0
  ordinary body while/if/for, atomic_load  rc=1           rc=1

`atomic_load` and a relaxed fence are NOT interchangeable as evidence: the
fence lowers to nothing in IR while the load keeps an atomic IR node, which
is exactly the IR/object split above. A genuinely reachable entry (goto into
the branch, case of an enclosing switch) is rejected in every cell.

Usage: mcs251-r6-condvar-boundary-check.py --clang-cc1 <cc1 argv...>
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

CRASH_TEXT = ("PLEASE submit", "Stack dump", "PLEASE ATTACH", "crash backtrace")

# (name, source, language, {cell: expected rc}) with cells named
# "syntax"/"ir"/"obj" x "O0"/"O2". All cells are asserted exactly.
CPP = "c++17"
C = "gnu11"

INCLUDE = '#include <stdatomic.h>\n'

ALL_ZERO = {"syntax.O0": 0, "syntax.O2": 0, "ir.O0": 0, "ir.O2": 0,
            "obj.O0": 0, "obj.O2": 0}
ALL_ONE = {k: 1 for k in ALL_ZERO}
# The StmtExpr while/for row of the registered boundary table: Sema defers
# (syntax rc=0), the IR/object path keeps the atomic IR node and rejects.
SE_LOOP_LOAD = {"syntax.O0": 0, "syntax.O2": 0, "ir.O0": 1, "ir.O2": 1,
                "obj.O0": 1, "obj.O2": 1}

CASES = [
    # ---- Part 1: condition variables, accepted half (C++) -----------------
    ("condvar-plain-if", """
int f(void) { return ({ if (int y = 1) {} 0; }); }
""", CPP, ALL_ZERO),
    ("condvar-plain-while", """
int f(void) { return ({ while (int y = 0) {} 0; }); }
""", CPP, ALL_ZERO),
    ("condvar-plain-for", """
int f(void) { return ({ for (; int y = 0;) {} 0; }); }
""", CPP, ALL_ZERO),
    ("condvar-plain-switch", """
int f(void) { return ({ switch (int y = 1) { default: break; } 0; }); }
""", CPP, ALL_ZERO),
    ("condvar-sizeof-deferred", """
int f(void) { return sizeof(({ if (int y = (__atomic_signal_fence(0), 1)) {} 0; })); }
""", CPP, ALL_ZERO),
    ("condvar-dead-branch-fence", """
int f(void) { return ({ if (0) { int y = (__atomic_signal_fence(0), 1); } 0; }); }
""", CPP, ALL_ZERO),
    # ---- Part 2: the registered boundary (C, real <stdatomic.h>) ----------
    # StmtExpr + constant-false loop + real atomic_load: Sema defers, the
    # IR/object path still rejects (the atomic IR node is kept).
    ("boundary-se-while-load", INCLUDE + """
int f(atomic_int *p) { return ({ while (0) { atomic_load(p); } 1; }); }
""", C, SE_LOOP_LOAD),
    ("boundary-se-for-load", INCLUDE + """
int f(atomic_int *p) { return ({ for (; 0;) { atomic_load(p); } 1; }); }
""", C, SE_LOOP_LOAD),
    # StmtExpr + constant-false if + real atomic_load: both sides accept.
    ("boundary-se-if-load", INCLUDE + """
int f(atomic_int *p) { return ({ if (0) { atomic_load(p); } 1; }); }
""", C, ALL_ZERO),
    # The relaxed-fence column: accepted in every cell, and NOT interchangeable
    # with atomic_load as evidence.
    ("boundary-se-while-fence", INCLUDE + """
int f(atomic_int *p) { return ({ while (0) { atomic_signal_fence(memory_order_relaxed); } 1; }); }
""", C, ALL_ZERO),
    ("boundary-se-for-fence", INCLUDE + """
int f(atomic_int *p) { return ({ for (; 0;) { atomic_signal_fence(memory_order_relaxed); } 1; }); }
""", C, ALL_ZERO),
    ("boundary-se-if-fence", INCLUDE + """
int f(atomic_int *p) { return ({ if (0) { atomic_signal_fence(memory_order_relaxed); } 1; }); }
""", C, ALL_ZERO),
    # The paired ordinary-function-body rows: the same statements OUTSIDE a
    # statement expression report the atomic-family diagnostic in Sema, in
    # every cell. This is the registered conservative analysis boundary.
    ("boundary-plain-while-load", INCLUDE + """
int f(atomic_int *p) { while (0) { atomic_load(p); } return 1; }
""", C, ALL_ONE),
    ("boundary-plain-if-load", INCLUDE + """
int f(atomic_int *p) { if (0) { atomic_load(p); } return 1; }
""", C, ALL_ONE),
    ("boundary-plain-for-load", INCLUDE + """
int f(atomic_int *p) { for (; 0;) { atomic_load(p); } return 1; }
""", C, ALL_ONE),
    # Genuinely reachable entries are always rejected, both inside a StmtExpr
    # dead branch and in an ordinary body.
    ("boundary-goto-entry", INCLUDE + """
int f(atomic_int *p) { return ({ goto L; if (0) { L: atomic_load(p); } 1; }); }
""", C, ALL_ONE),
    ("boundary-case-entry", INCLUDE + """
int f(int x, atomic_int *p) { return ({ switch (x) { if (0) { case 1: atomic_load(p); } } 1; }); }
""", C, ALL_ONE),
]

failures = []


# WP4 round 7 (T1): a file appearing is not an acceptance. Round 6 measured
# that a 0-byte .ll and a `not ELF` text both satisfied a bare exists() check.
# The accepted cells are therefore required to carry content: the IR must be
# non-empty and name the MCS251 triple, the object must be a complete
# big-endian ELF32 with e_machine == EM_MCS251 and a section header table that
# lies inside the file. The completeness part is not decoration: Alice measured
# that a legal object TRUNCATED to 20 or 64 bytes still satisfied the round-6
# magic/class/data/e_machine checks (the ELF32 file header is 52 bytes, and the
# section header table is reached through fields in it), and that truncating
# every accepted object of the matrix to 20 bytes left the whole helper at PASS.
EM_MCS251 = 0x9999
ELF32_HDR_SIZE = 52


def artifact_ok(path, mode):
    blob = Path(path).read_bytes()
    if not blob:
        return "accepted artifact is empty"
    if mode == "ir":
        text = blob.decode("utf-8", "replace")
        if 'target triple = "mcs251-unknown-none"' not in text:
            return "accepted IR artifact has no MCS251 target triple"
        # The cases are C and C++, so the defined symbol is `f` in one and
        # `_Z1fv` in the other: require a definition rather than one spelling.
        if not re.search(r"^define\b", text, re.M):
            return "accepted IR artifact defines no function"
        return None
    if blob[:4] != b"\x7fELF":
        return "accepted object is not an ELF file"
    if len(blob) < ELF32_HDR_SIZE:
        return (f"accepted object is shorter than an ELF32 file header "
                f"({len(blob)} < {ELF32_HDR_SIZE} bytes)")
    if blob[4] != 1:
        return f"accepted object is not ELF32 (EI_CLASS={blob[4]})"
    if blob[5] != 2:
        return f"accepted object is not big-endian (EI_DATA={blob[5]})"
    e_machine = int.from_bytes(blob[18:20], "big")
    if e_machine != EM_MCS251:
        return f"e_machine is {e_machine:#x}, not EM_MCS251 ({EM_MCS251:#x})"
    # The section header table must actually be present and contained: a
    # truncated file with a valid prefix is not an object the downstream tools
    # could consume.
    e_shoff = int.from_bytes(blob[32:36], "big")
    e_shentsize = int.from_bytes(blob[46:48], "big")
    e_shnum = int.from_bytes(blob[48:50], "big")
    if e_shentsize == 0 or e_shnum == 0:
        return (f"accepted object has no section header table "
                f"(e_shentsize={e_shentsize}, e_shnum={e_shnum})")
    if e_shoff + e_shnum * e_shentsize > len(blob):
        return (f"accepted object's section header table runs past the file "
                f"end (e_shoff={e_shoff} + {e_shnum} x {e_shentsize} > "
                f"{len(blob)})")
    return None


def negative_controls(tmp, samples):
    """The artifacts T1 used to measure the round-6 gap, replayed against the
    same assertions the accepted cells use, so a weakened check fails here."""
    problems = []
    for label, path, mode in samples:
        empty = Path(str(path) + ".empty")
        empty.write_bytes(b"")
        if artifact_ok(empty, mode) is None:
            problems.append(f"{label}: a 0-byte artifact was accepted")
        if mode == "ir":
            fake = Path(str(path) + ".fake")
            fake.write_text("not LLVM IR\n")
            if artifact_ok(fake, "ir") is None:
                problems.append(f"{label}: a non-IR text was accepted")
        else:
            fake = Path(str(path) + ".notelf")
            fake.write_text("not ELF")
            if artifact_ok(fake, "obj") is None:
                problems.append(f"{label}: a 'not ELF' object was accepted")
            blob = bytearray(Path(path).read_bytes())
            blob[18:20] = (3).to_bytes(2, "big")  # e_machine = EM_386
            wrong = Path(str(path) + ".emachine3")
            wrong.write_bytes(blob)
            if artifact_ok(wrong, "obj") is None:
                problems.append(
                    f"{label}: an object with e_machine=3 was accepted")
            # A valid PREFIX of a legal object is not an artifact: both the
            # 20-byte case (short of the 52-byte ELF32 header) and the 64-byte
            # case (header present, section table far outside the file).
            for cut in (20, 64):
                trunc = Path(f"{path}.trunc{cut}")
                trunc.write_bytes(bytes(Path(path).read_bytes())[:cut])
                if artifact_ok(trunc, "obj") is None:
                    problems.append(
                        f"{label}: an object truncated to {cut} bytes was "
                        f"accepted")
    return problems


def main():
    argv = sys.argv[1:]
    if not argv:
        sys.exit("usage: mcs251-r6-condvar-boundary-check.py <cc1 argv...>")
    cc1 = argv
    # %clang_cc1 already carries -internal-isystem for the resource dir.
    # Accepted artifacts collected during the matrix, replayed at the end as
    # the negative controls for the artifact checks.
    samples = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for name, text, std, expect in CASES:
            suffix = ".cpp" if std == CPP else ".c"
            src = tmp / (name + suffix)
            src.write_text(text)
            for opt in ("O0", "O2"):
                for mode, mode_args in (("syntax", ["-fsyntax-only"]),
                                        ("ir", ["-emit-llvm"]),
                                        ("obj", ["-emit-obj",
                                                 "-mllvm",
                                                 "-mcs251-object-format=elf"])):
                    out = tmp / f"{name}.{opt}.{mode}.out"
                    out.unlink(missing_ok=True)
                    result = subprocess.run(
                        cc1 + [
                            "-triple", "mcs251-unknown-none",
                            f"-std={std}",
                            "-Wno-unused-value",
                            f"-{opt}",
                            *mode_args,
                            str(src),
                            "-o", str(out),
                        ],
                        capture_output=True, text=True)
                    cell = f"{mode}.{opt}"
                    got = result.returncode
                    want = expect[cell]
                    label = f"{name}.{cell}"
                    if got != want:
                        failures.append(
                            f"{label}: expected status {want}, got {got}: "
                            f"{result.stderr[:200]}")
                    if want == 0 and mode != "syntax":
                        if not out.exists():
                            failures.append(f"{label}: no artifact for an "
                                            f"accepted compile")
                        else:
                            bad = artifact_ok(out, mode)
                            if bad:
                                failures.append(
                                    f"{label}: accepted artifact: {bad}")
                            elif samples is not None:
                                samples.append((label, out, mode))
                    if want == 1:
                        if out.exists():
                            failures.append(f"{label}: artifact left behind")
                        # The Sema spelling ends "not supported on MCS251",
                        # the contract-check spelling "not supported on this
                        # target"; both are ordinary-error exits.
                        if ("not supported on" not in result.stderr
                                or "error:" not in result.stderr):
                            failures.append(
                                f"{label}: unexpected diagnostic: "
                                f"{result.stderr[:200]}")
                    for needle in CRASH_TEXT:
                        if needle in result.stderr:
                            failures.append(
                                f"{label}: crash text {needle!r} present")

        # WP4 round 7 (T1): replay the fabricated artifacts against the same
        # assertions the accepted cells use.
        if not samples:
            failures.append("artifact negative controls: no accepted "
                            "artifact was collected to build them from")
        else:
            for line in negative_controls(tmp, samples):
                failures.append(line)

    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    print(f"condition-variable matrix and registered boundary: "
          f"{len(CASES)} shapes x 6 cells, exact statuses pinned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
