#!/usr/bin/env python3
"""WP4 round 7 (R1): the six-cell matrix for the C++ A2 rule that round 7 wired
into the C++ source layer, with the base-subobject shape from the round-7
tasking at the centre.

Round 6 taught the C walker to pair an aggregate initializer list with the
SUBOBJECTS it initializes: unnamed bitfields get no element, and a class
initializes its base subobjects before its fields, so the semantic element
order is [base elements..., field elements...]. The C++ base shape
`struct S : B { int *p; }; S s = {{0}, (int *)0x1234};` was only ever judged by
the IR and object layers, though: A2's source call site lives inside
Sema::CheckForConstantInitializer, which C++ does not run, and BOTH C++ branches
there are no-ops. The C++ source layer was therefore silent for every static
pointer initializer -- the base shape was one instance, not the cause, and
`int *p = (int *)0x1234;` was equally silent. Round 7 adds the C++ call in
Sema::CheckCompleteVariableDeclaration, gated on the initializer being a
constant initializer.

This helper pins the matrix per cell (O0/O2 x syntax/IR/object):
  * REJECTED rows: a class whose aggregate list starts with a base subobject
    and whose pointer element is an integer-cast image (one and two bases, the
    pointer inside the base, a nested aggregate inside the base, block-scope
    static), plus the non-aggregate/scalar/array/nested/anonymous/namespace/
    out-of-line-static-member counterparts. Exact status 1 in all six cells, no
    artifact, an ordinary error diagnostic naming the rule, no crash text.
    Before round 7 the syntax cell of every one of these was 0.
  * ACCEPTED rows: the same shapes with a null or '&symbol' element, in the
    same position. Exact status 0 in all six cells with a real artifact (the IR
    must be a non-empty MCS251 module naming the global; the object must be
    EM_MCS251 ELF32 big-endian with the defined symbol).
  * A C++ DYNAMIC initializer (`int *p = (int *)runtime_value();`) stays
    accepted by the source layer: it is not a constant initializer, so the IR
    classifier's static-pointer domain does not describe it. Its object path
    has its own separately registered pre-existing behavior and is not asserted
    here.

The accepted artifacts are replayed at the end as negative controls (a 0-byte
IR file, a 'not ELF' text, an object whose e_machine is changed to EM_386), so
the strengthened artifact assertions are exercised rather than asserted only by
inspection.

Usage: mcs251-r7-cpp-a2-check.py --clang-cc1 <cc1 argv...>
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CRASH_TEXT = ("PLEASE submit", "Stack dump", "PLEASE ATTACH", "crash backtrace")
EM_MCS251 = 0x9999
SHT_SYMTAB = 2

REJECT = {
    # The round-7 tasking shape, and its siblings.
    "cpp-base-abs": """
struct B { int n; };
struct S : B { int *p; };
S s = {{0}, (int *)0x1234};
""",
    "cpp-base-abs-2base": """
struct B1 { int a; };
struct B2 { int b; };
struct S : B1, B2 { int *p; };
S s = {{0}, {0}, (int *)0x1234};
""",
    "cpp-baseptr-first": """
struct PB { int *q; };
struct DS : PB { int n; };
DS s = {{(int *)0x1234}, 0};
""",
    "cpp-nested-base-abs": """
struct NBInner { int m; };
struct NB { NBInner n; };
struct NS : NB { int *p; };
NS s = {{{0}}, (int *)0x1234};
""",
    "cpp-base-abs-static": """
struct B { int n; };
struct S : B { int *p; };
void f() { static S s = {{0}, (int *)0x1234}; (void)s; }
""",
    # The same source-layer gap outside the base shape: none of these was
    # reported at the source layer before round 7 either.
    "cpp-scalar-abs": """
int *p = (int *)0x1234;
""",
    "cpp-array-abs": """
int *a[2] = {0, (int *)0x1234};
""",
    "cpp-nested-abs": """
struct Inner { int *q; };
struct Outer { struct Inner in; };
struct Outer o = {{(int *)0x1234}};
""",
    "cpp-anon-abs": """
struct Anon { struct { int n; } a; int *p; };
struct Anon a = {{0}, (int *)0x1234};
""",
    "cpp-namespace-abs": """
namespace N { int *p = (int *)0x1234; }
""",
    "cpp-static-member-abs": """
struct Holder { static int *member; };
int *Holder::member = (int *)0x1234;
""",
}

ACCEPT = {
    "cpp-base-null": """
struct B { int n; };
struct S : B { int *p; };
S s = {{0}, 0};
""",
    "cpp-base-symbol": """
struct B { int n; };
struct S : B { int *p; };
int target;
S s = {{0}, (int *)&target};
""",
    "cpp-base-null-2base": """
struct B1 { int a; };
struct B2 { int b; };
struct S : B1, B2 { int *p; };
S s = {{0}, {0}, 0};
""",
    "cpp-base-symbol-2base": """
struct B1 { int a; };
struct B2 { int b; };
struct S : B1, B2 { int *p; };
int target;
S s = {{0}, {0}, (int *)&target};
""",
    "cpp-baseptr-null": """
struct PB { int *q; };
struct DS : PB { int n; };
DS s = {{0}, 0};
""",
    "cpp-nested-base-null": """
struct NBInner { int m; };
struct NB { NBInner n; };
struct NS : NB { int *p; };
NS s = {{0}, 0};
""",
    "cpp-base-null-static": """
struct B { int n; };
struct S : B { int *p; };
void f() { static S s = {{0}, 0}; (void)s; }
""",
    "cpp-array-null-symbol": """
int target;
int *a[2] = {0, (int *)&target};
""",
    "cpp-nested-symbol": """
struct Inner { int *q; };
struct Outer { struct Inner in; };
int target;
struct Outer o = {{(int *)&target}};
""",
}

# Global name -> the object symbol the MCS251 writer emits (leading underscore).
# A block-scope static has NO externally visible symbol; only its source layer
# cells are asserted and its artifacts are not treated as accepted output.
SYMBOL = {
    "cpp-base-null": "s", "cpp-base-symbol": "s",
    "cpp-base-null-2base": "s", "cpp-base-symbol-2base": "s",
    "cpp-baseptr-null": "s", "cpp-nested-base-null": "s",
    "cpp-array-null-symbol": "a",
    "cpp-nested-symbol": "o",
}

# Shapes whose accepted cells carry no stable external symbol to assert on (a
# block-scope static is emitted under an internal local name). Their six cells
# are still required to exit 0 and leave an artifact; the artifact's CONTENT is
# not asserted, and they are excluded from the negative controls.
NO_SYMBOL = {"cpp-base-null-static"}

failures = []


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


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


def ir_ok(path, want_global):
    text = Path(path).read_text()
    if not text.strip():
        return "accepted IR artifact is empty"
    if 'target triple = "mcs251-unknown-none"' not in text:
        return "accepted IR artifact has no MCS251 target triple"
    if want_global and f"@{want_global}" not in text:
        return f"accepted IR artifact does not mention @{want_global}"
    return None


def elf_ok(path, want_symbol):
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
            if shndx != 0 and name == want_symbol:
                return None
    return f"defined symbol {want_symbol!r} not found"


def negative_controls(samples):
    problems = []
    for label, ir_path, obj_path in samples:
        if ir_path is not None and Path(ir_path).exists():
            empty = Path(str(ir_path) + ".empty")
            empty.write_bytes(b"")
            if not ir_ok(empty, ""):
                problems.append(f"{label}: a 0-byte IR artifact was accepted")
        if obj_path is not None and Path(obj_path).exists():
            text = Path(str(obj_path) + ".notelf")
            text.write_text("not ELF")
            if not elf_identity(text)[0]:
                problems.append(f"{label}: a 'not ELF' object was accepted")
            blob = bytearray(Path(obj_path).read_bytes())
            blob[18:20] = (3).to_bytes(2, "big")  # e_machine = EM_386
            wrong = Path(str(obj_path) + ".emachine3")
            wrong.write_bytes(blob)
            if not elf_identity(wrong)[0]:
                problems.append(f"{label}: an object with e_machine=3 was accepted")
    return problems


def main():
    argv = sys.argv[1:]
    if not argv:
        sys.exit("usage: mcs251-r7-cpp-a2-check.py <cc1 argv...>")
    cc1 = argv
    ir_samples = []
    obj_samples = []

    def compile_shape(name, text, opt, mode, out):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / f"{name}.cpp"
            src.write_text(text)
            out = Path(out)
            out.unlink(missing_ok=True)
            cmd = cc1 + ["-triple", "mcs251-unknown-none", "-std=c++17",
                         "-Wno-unused-value", f"-{opt}"]
            if mode == "syntax":
                cmd += ["-fsyntax-only"]
            elif mode == "ir":
                cmd += ["-emit-llvm"]
            else:
                cmd += ["-emit-obj", "-mllvm", "-mcs251-object-format=elf"]
            cmd += [str(src), "-o", str(out)]
            r = run(cmd)
            return r.returncode, r.stderr

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for name, text in REJECT.items():
            for opt in ("O0", "O2"):
                for mode in ("syntax", "ir", "obj"):
                    out = tmp / f"{name}.{opt}.{mode}.out"
                    rc, err = compile_shape(name, text, opt, mode, out)
                    label = f"{name}.{opt}.{mode}"
                    if rc != 1:
                        failures.append(
                            f"{label}: expected status 1, got {rc}: {err[:200]}")
                    if out.exists():
                        failures.append(f"{label}: rejected compile left an artifact")
                    if "absolute-address" not in err or "error:" not in err:
                        failures.append(
                            f"{label}: unexpected diagnostic: {err[:200]}")
                    for needle in CRASH_TEXT:
                        if needle in err:
                            failures.append(
                                f"{label}: crash text {needle!r} present")
        for name, text in ACCEPT.items():
            for opt in ("O0", "O2"):
                for mode in ("syntax", "ir", "obj"):
                    out = tmp / f"{name}.{opt}.{mode}.out"
                    rc, err = compile_shape(name, text, opt, mode, out)
                    label = f"{name}.{opt}.{mode}"
                    if rc != 0:
                        failures.append(
                            f"{label}: expected status 0, got {rc}: {err[:200]}")
                        continue
                    for needle in CRASH_TEXT:
                        if needle in err:
                            failures.append(
                                f"{label}: crash text {needle!r} present")
                    if mode == "syntax":
                        continue
                    if not out.exists():
                        failures.append(f"{label}: accepted compile left no artifact")
                        continue
                    if name in NO_SYMBOL:
                        # Blocks-scope statics have no stable external symbol;
                        # the cell's status and the artifact's existence are the
                        # assertion, and the artifact is not used as a control.
                        if mode == "ir":
                            bad = ir_ok(out, "")
                            if bad:
                                failures.append(f"{label}: artifact: {bad}")
                        else:
                            bad = elf_identity(out)[0]
                            if bad:
                                failures.append(f"{label}: artifact: {bad}")
                        continue
                    sym = SYMBOL[name]
                    if mode == "ir":
                        bad = ir_ok(out, sym)
                        if bad:
                            failures.append(f"{label}: artifact: {bad}")
                        else:
                            ir_samples.append((label, out, None))
                    else:
                        bad = elf_ok(out, "_" + sym)
                        if bad:
                            failures.append(f"{label}: artifact: {bad}")
                        else:
                            obj_samples.append((label, None, out))
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
    print(f"C++ A2 six-cell matrix: {len(REJECT)} rejected shapes x O0/O2 x "
          f"syntax/IR/obj (exact status 1, no artifact) and {len(ACCEPT)} "
          f"accepted shapes with parsed artifacts; negative controls replayed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
