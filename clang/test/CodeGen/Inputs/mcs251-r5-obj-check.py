#!/usr/bin/env python3
"""WP4 round 5 (extended in round 6): check the accepted objects really are
complete MCS251 ELF objects -- not just "a file with the right magic" -- and
then re-run the rejected counterparts with an EXACT status of 1, no artifact,
and a diagnostic.

What "complete" means here, in increasing strength:
  * the ELF magic and class/endianness the MCS251 writer emits (32-bit big
    endian), plus a minimum length;
  * the DEFINED symbols the accepted fixtures declare, bound to the section
    that holds them -- so a truncated or empty object fails;
  * the relocation in .rela.mcs251.xinit that resolves the supported '&symbol'
    pointer initializer to its target symbol. This is what makes the check
    sensitive to the SOURCE the object was built from rather than to its size.
    Round 6 pins the SECTION NAME as well and carries an internal negative
    control that renames it.

The rejected side is the same shapes with an entry point added, plus the
condition-variable, `atomic_init` and A2-union shapes (round 6). `not cc1`
would only prove "nonzero", so each status is asserted to be exactly 1, with
no artifact left behind and no crash text.

Usage: mcs251-r5-obj-check.py <obj>... -- <clang-cc1 argv...>
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ELF_MAGIC = b"\x7fELF"
# ELFCLASS32 = 1; ELFDATA2MSB = 2. The MCS251 writer emits 32-bit big-endian.
ELFCLASS32 = 1
ELFDATA2MSB = 2
SHT_SYMTAB = 2
SHT_RELA = 4
SHN_UNDEF = 0


def read_elf(path):
    blob = Path(path).read_bytes()
    if blob[:4] != ELF_MAGIC:
        raise ValueError(f"{path} is not an ELF object (magic {blob[:4].hex()})")
    if len(blob) <= 64:
        raise ValueError(f"{path} is too small to be an object ({len(blob)} bytes)")
    if blob[4] != ELFCLASS32:
        raise ValueError(f"{path} is not ELF32 (class {blob[4]})")
    if blob[5] != ELFDATA2MSB:
        raise ValueError(f"{path} is not big-endian (data {blob[5]})")
    # Elf32_Ehdr: e_shoff at 32, e_shentsize/e_shnum/e_shstrndx at 46/48/50.
    shoff = struct.unpack(">I", blob[32:36])[0]
    shentsize, shnum, shstrndx = struct.unpack(">HHH", blob[46:52])
    if shnum == 0 or shentsize < 40:
        raise ValueError(f"{path} has no section table")
    if shoff + shnum * shentsize > len(blob):
        raise ValueError(f"{path} section table runs past the end of the file")
    raw = []
    for i in range(shnum):
        off = shoff + i * shentsize
        raw.append(struct.unpack(">IIIIIIIIII", blob[off:off + 40]))
    strtab_off = raw[shstrndx][4]
    names = []
    for fields in raw:
        start = strtab_off + fields[0]
        end = blob.index(b"\0", start)
        names.append(blob[start:end].decode("ascii", "replace"))
    return blob, raw, names


def defined_symbols(blob, raw, names):
    """Map symbol name -> (type, shndx) for every DEFINED symbol."""
    out = {}
    for i, sec in enumerate(raw):
        nameoff, typ, _flags, _addr, off, size, link, _info, _align, entsize = sec
        if typ != SHT_SYMTAB or entsize == 0:
            continue
        strtab = raw[link][4]
        for j in range(size // entsize):
            so = off + j * entsize
            # Elf32_Sym: name, value, size, info, other, shndx.
            sym_nameoff, _value, _sz, _info, _other, shndx = struct.unpack(
                ">IIIBBH", blob[so:so + 16])
            start = strtab + sym_nameoff
            name = blob[start:blob.index(b"\0", start)].decode("ascii", "replace")
            if shndx != SHN_UNDEF and name:
                out[name] = (typ, shndx)
    return out


def relocation_targets(blob, raw, names):
    """Every (section name, symbol name) pair referenced by a relocation (the
    initializer channel). The section name is kept: WP4 round 6 pins the
    relocation to `.rela.mcs251.xinit`, the section the MCS251 writer emits the
    supported '&symbol' pointer-initializer relocations into. Matching on the
    target symbol alone accepted an object whose equivalent relocation had been
    moved to an unrelated SHT_RELA section (measured: renaming
    `.rela.mcs251.xinit` to a same-length `.rela.mcs251.other` left the round-5
    check passing)."""
    targets = []
    symtab = None
    for i, sec in enumerate(raw):
        if sec[1] == SHT_SYMTAB:
            symtab = i
    if symtab is None:
        return targets
    st = raw[symtab]
    strtab = raw[st[6]][4]
    for i, sec in enumerate(raw):
        _nameoff, typ, _flags, _addr, off, size, _link, _info, _align, entsize = sec
        if typ != SHT_RELA or entsize == 0:
            continue
        for j in range(size // entsize):
            ro = off + j * entsize
            _offset, info, _addend = struct.unpack(">IIi", blob[ro:ro + 12])
            symidx = info >> 8
            so = st[4] + symidx * st[9]
            sym_nameoff = struct.unpack(">I", blob[so:so + 4])[0]
            start = strtab + sym_nameoff
            name = blob[start:blob.index(b"\0", start)].decode("ascii", "replace")
            targets.append((names[i], name))
    return targets


# The MCS251 symbol convention puts a leading underscore on C identifiers, so
# the expectations below are matched on the unprefixed form.
def has_symbol(syms, wanted):
    return wanted in syms or ("_" + wanted) in syms


EXPECTED_SYMBOLS = ("r5_pointer_target", "r5_symbol_pointer",
                    "plain_if_dead", "plain_while_dead", "plain_for_dead",
                    "nested_switch_case_dead", "label_without_operation",
                    # WP4 round 6 (A2 union fix): the union fixtures below.
                    "union_int_member", "union_ptr_member", "union_in_struct",
                    "anon_union_int")
# WP4 round 6: the relocation must sit in the exact section the MCS251 ELF
# writer designates for pointer-initializer relocations, not merely in any
# SHT_RELA section that happens to target the right symbol.
EXPECTED_RELOC_SECTION = ".rela.mcs251.xinit"
EXPECTED_RELOC_TARGET = "r5_pointer_target"

REJECTED = {
    # (1) label entry into a constant-false branch. A relaxed fence lowers to
    # nothing in IR, so no later layer can repair a wrong decision here.
    "goto-if-fence": """
int f(void) { return ({ goto L; if (0) { L: __atomic_signal_fence(0); } 1; }); }
""",
    "goto-while-fence": """
int f(void) { return ({ goto L; while (0) { L: __atomic_signal_fence(0); } 1; }); }
""",
    "goto-for-fence": """
int f(void) { return ({ goto L; for (; 0;) { L: __atomic_signal_fence(0); } 1; }); }
""",
    # A C11 atomic store is the second shape whose later lowering may fold the
    # operation away, so the source decision is the only one that decides it.
    "goto-c11-store": """
int f(_Atomic int *p) {
  return ({ goto L; if (0) { L: __c11_atomic_store(p, 1, 5); } 1; });
}
""",
    # (1b) case entry from an enclosing switch.
    "switch-case-dead-if": """
int f(int x) { return ({ switch (x) { if (0) { case 1: __atomic_signal_fence(0); } } 1; }); }
""",
    "switch-case-dead-for": """
int f(int x) { return ({ switch (x) { for (; 0;) { case 1: __atomic_signal_fence(0); } } 1; }); }
""",
    # (2) the A2 shapes that must decide on the final value.
    "a2-gnu-cond-abs": """
int *p = 0 ?: (int *)0x5678;
""",
    "a2-cond-selects-abs": """
int *p = ((unsigned char *)0x1234 != (unsigned char *)0) ? (unsigned char *)0x9A
                                                         : (unsigned char *)0;
""",
    "a2-agg-abs-second": """
unsigned char *p[2] = {(unsigned char *)0, (unsigned char *)0x1234};
""",
    # (3) WP4 round 6, A2 union traversal. A union initializes exactly ONE
    # member -- the one the designator names, not the first one declared.
    # Pairing the initializer with the first field judged a non-pointer
    # member's integer initializer as a pointer (false rejection) and a
    # pointer member's initializer as an integer (missed rejection that then
    # disagreed with the IR/object layers).
    "a2-union-nonfirst-ptr": """
union Box { int tag; int *ptr; };
union Box box = { .ptr = (int *)0x1234 };
""",
    "a2-union-nonfirst-ptr-plain": """
union Box { int *ptr; unsigned tag; };
union Box box = { (int *)0x1234 };
""",
    "a2-union-nested-in-struct": """
union U { int tag; int *ptr; };
struct S { int n; union U u; };
struct S s = { 1, { .ptr = (int *)0x1234 } };
""",
    # The anonymous-union-member pair of the same rule (field-order variation
    # is the whole point of the union fix).
    "a2-union-anon-ptr": """
struct Box { int n; union { unsigned tag; int *ptr; }; };
struct Box b = { 1, { .ptr = (int *)0x1234 } };
""",
    # (4) WP4 round 6: the C++ condition-variable shapes, six cells each.
    # The initializer of `if (T y = init)` runs before the condition is
    # tested, in all four statement forms; the atomic operation inside it is
    # an operation. These run in C++ mode (condition declarations are C++
    # [stmt.select]); the helper's per-shape `lang` field carries the mode.
    "cpp-condvar-if": ("""
int f(void) { return ({ if (int y = (__atomic_signal_fence(0), 1)) {} 0; }); }
""", "c++17"),
    "cpp-condvar-while": ("""
int f(void) { return ({ while (int y = (__atomic_signal_fence(0), 0)) {} 0; }); }
""", "c++17"),
    "cpp-condvar-for": ("""
int f(void) { return ({ for (; int y = (__atomic_signal_fence(0), 0);) {} 0; }); }
""", "c++17"),
    "cpp-condvar-switch": ("""
int f(void) { return ({ switch (int y = (__atomic_signal_fence(0), 1)) { default: break; } 0; }); }
""", "c++17"),
    # `__c11_atomic_init` initializes the condition variable through the C11
    # atomic-init builtin: same statement forms, same verdict.
    "cpp-condvar-atomic-init": ("""
int f(_Atomic int *p) { return ({ if (int y = (__c11_atomic_init(p, 1), 1)) {} 0; }); }
""", "c++17"),
}

CRASH_TEXT = (
    "PLEASE submit",
    "Stack dump",
    "PLEASE ATTACH",
    "crash backtrace",
    "frontend command failed",
)

failures = []

accepted_objects = []
for path in sys.argv[1:]:
    if path == "--":
        break
    try:
        blob, raw, names = read_elf(path)
    except ValueError as exc:
        sys.exit(f"FAIL: {exc}")
    sizes = len(blob)
    syms = defined_symbols(blob, raw, names)
    missing = [s for s in EXPECTED_SYMBOLS if not has_symbol(syms, s)]
    if missing:
        failures.append(f"{path}: missing defined symbols {missing}")
    relocs = relocation_targets(blob, raw, names)
    # Round 6: the initializer relocation must live in EXPECTED_RELOC_SECTION
    # (both spellings, because the symbol convention prefixes an underscore).
    pinned = [sec for sec, t in relocs
              if sec == EXPECTED_RELOC_SECTION and has_symbol({t}, EXPECTED_RELOC_TARGET)]
    if not pinned:
        failures.append(
            f"{path}: no relocation in {EXPECTED_RELOC_SECTION!r} targets "
            f"{EXPECTED_RELOC_TARGET!r}; found {relocs}"
        )
    accepted_objects.append((path, blob, raw, names))
    print(f"{path}: ELF32 big-endian, {sizes} bytes, "
          f"{len(syms)} defined symbols, {len(relocs)} relocations")

# Negative control for the section pinning (WP4 round 6): mutate a copy of a
# real accepted object so its `.rela.mcs251.xinit` section is RENAMED to a
# same-length unrelated name, then require the acceptance logic above to stop
# passing. Without this, a check that only matched the target symbol could not
# tell the initializer channel from any other relocation section.
if accepted_objects:
    path, blob, raw, names = accepted_objects[0]
    needle = EXPECTED_RELOC_SECTION.encode()
    pos = blob.find(needle)
    if pos < 0:
        failures.append("negative control: section name string not found in "
                        "the object's string table")
    else:
        mutant = bytearray(blob)
        # ".rela.mcs251.xinit" -> ".rela.mcs251.other" (same length).
        mutant[pos:pos + len(needle)] = b".rela.mcs251.other"
        m_path = Path(path).with_suffix(".mutant.o")
        m_path.write_bytes(mutant)
        try:
            m_blob, m_raw, m_names = read_elf(m_path)
        except ValueError as exc:
            failures.append(f"negative control: mutated object unreadable: {exc}")
        else:
            m_relocs = relocation_targets(m_blob, m_raw, m_names)
            m_pinned = [sec for sec, t in m_relocs
                        if sec == EXPECTED_RELOC_SECTION
                        and has_symbol({t}, EXPECTED_RELOC_TARGET)]
            if m_pinned:
                failures.append(
                    "negative control: renamed relocation section was still "
                    "accepted; the section-name pinning is not effective"
                )
            else:
                print("negative control: renamed relocation section rejected "
                      "as expected")

if "--" not in sys.argv:
    sys.exit("usage: mcs251-r5-obj-check.py <obj>... -- <clang-cc1 argv...>")
split = sys.argv.index("--")
cc1 = sys.argv[split + 1:]
if not cc1:
    sys.exit("usage: mcs251-r5-obj-check.py <obj>... -- <clang-cc1 argv...>")

with tempfile.TemporaryDirectory() as tmp:
    for name, spec in REJECTED.items():
        # A shape is either plain C source or a (source, language) pair; the
        # C++ condition-variable shapes need the C++ mode condition
        # declarations require.
        if isinstance(spec, tuple):
            text, std = spec
        else:
            text, std = spec, "gnu11"
        suffix = ".cpp" if std.startswith("c++") else ".c"
        src = Path(tmp) / (name + suffix)
        src.write_text(text)
        # The decision must be the same on all three output paths and at both
        # optimizations: a rejected construct must not compile to IR or to an
        # object on one path while --fsyntax-only refuses it on another.
        modes = {
            "syntax": ["-fsyntax-only"],
            # cc1 spells the IR-text action `-emit-llvm` on its own; adding
            # `-S` is rejected as a second action.
            "ir": ["-emit-llvm"],
            "obj": ["-emit-obj"],
        }
        for opt in ("-O0", "-O2"):
            statuses = {}
            for mode, mode_args in modes.items():
                out = Path(tmp) / f"{name}.{mode}.out"
                out.unlink(missing_ok=True)
                result = subprocess.run(
                    cc1 + [
                        "-triple", "mcs251-unknown-none",
                        f"-std={std}",
                        "-Wno-unused-value",
                        "-mllvm", "-mcs251-object-format=elf",
                        opt,
                        *mode_args,
                        str(src),
                        "-o", str(out),
                    ],
                    capture_output=True,
                    text=True,
                )
                statuses[mode] = result.returncode
                label = f"{name}{opt}.{mode}"
                if result.returncode != 1:
                    failures.append(
                        f"{label}: expected status 1, got {result.returncode}"
                    )
                # Only an object-emitting run may leave a file, and a rejected
                # run must not leave one on ANY mode.
                if out.exists():
                    failures.append(f"{label}: output artifact left behind")
                if result.stderr.strip() == "":
                    failures.append(f"{label}: no diagnostic on stderr")
                for needle in CRASH_TEXT:
                    if needle in result.stderr:
                        failures.append(f"{label}: crash text {needle!r} present")
                if "not supported on MCS251" not in result.stderr:
                    failures.append(
                        f"{label}: unexpected diagnostic: {result.stderr[:300]}"
                    )
            if len(set(statuses.values())) != 1:
                failures.append(
                    f"{name}{opt}: modes disagree: {statuses}"
                )

if failures:
    print("FAIL")
    for line in failures:
        print("  " + line)
    sys.exit(1)
print(f"rejected counterparts: exact status 1, no artifact, "
      f"{len(REJECTED)} shapes x 2 optimizations; relocation section pinned "
      f"to {EXPECTED_RELOC_SECTION} with rename negative control")
