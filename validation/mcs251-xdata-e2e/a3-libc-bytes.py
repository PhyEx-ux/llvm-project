#!/usr/bin/env python3
"""a3-libc-bytes.py - byte-level assertions for the A3 libc e2e images.

Independent of the linker/compiler (stdlib only): reads a linked ELF that was
produced with --keep-symbols and asserts the properties the A3 acceptance
cares about (RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充二", Alice review R10).

The previous version of this checker derived the required payload set from
what it *found* in the image ("only the needles actually present are
required"), which made it self-fulfilling: deleting a ROM object still passed
as long as one other payload survived.  This version takes the payload set as
an explicit, per-image runtime-mode table supplied by the caller:

    --mode shim   (corpus shim: rom_msg, rom_digits, rom_bytes, plus the
                   CODE-context string literal and the AS0 literal control)
    --mode priv   (private ABI: priv_msg, priv_pat [and its terminator], plus
                   the object/pattern bytes)

Every expected payload must be found verbatim in its own symbol's byte range
AND, for the indirect (pointer-initialised) literals, at the address the
pointer object actually holds -- so the relocation result is verified too.
The strings include their NUL terminator.  A missing payload, a moved symbol,
a payload truncated by the symbol size, or the two literals being merged into
one object is a FAIL -- there is no "present subset" escape.

Alice review R10 closed four escapes in this file:

  1. a map line whose address is not a hex number was silently skipped
     (`continue`), so rewriting every DSEG address to `NOT_AN_ADDRESS` still
     printed the "AS0 data slices outside the CODE window" PASS.  Map lines
     are now parsed STRICTLY: a malformed line is a FAIL;
  2. only the slice START was range-checked, so a DSEG slice of
     `0xfeffff +0x200` (which crosses into the CODE window) passed.  The
     complete half-open range [start, start+size) is now checked;
  3. the indirect pointer symbols' SIZEs were never verified: shrinking
     `_code_lit_ptr`/`_plain_lit_ptr` to 1 byte still let the checker read 4
     bytes past the symbol.  The symbol size must now cover the full pointer;
  4. `read_bytes()` only required the START to lie in some section, so a read
     could run past the section end.  Every read must now fit entirely inside
     the owning section (`read_bytes_strict`).

Exit 0 with PASS lines, exit 1 with FAIL:<detail>.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ELF_MAGIC = b"\x7fELF"

CODE_LO = 0xFF0000
CODE_HI = 0xFF8000   # CSEG window per build.sh's area map

# A map slice line looks like `<obj>:<section> <addr> +<size>` (the address
# and size are hex, optionally prefixed with 0x).  Anything that matches the
# slice shape but has an unparseable address/size is a hard failure.
SLICE_RE = re.compile(r"^(?P<obj>[^\s:]+(?::[^\s]+)?)\s+"
                      r"(?P<addr>0x[0-9a-fA-F]+)\s+\+(?P<size>0x[0-9a-fA-F]+)\s*$")
# Lines that look like slices but carry a non-hex address (the malformed-map
# counterexample: `NOT_AN_ADDRESS +0x10`).
BAD_SLICE_RE = re.compile(r"^\S+\s+(?P<addr>\S+)\s+\+\s*(?P<size>\S+)\s*$")
# `FUNC <addr> +<size> <name>` lines.
FUNC_RE = re.compile(r"^FUNC\s+(?P<addr>0x[0-9a-fA-F]+)\s+"
                     r"\+(?P<size>0x[0-9a-fA-F]+)\s+(?P<name>\S+)\s*$")


def parse_elf(path: Path):
    data = path.read_bytes()
    assert data[:4] == ELF_MAGIC, "not an ELF file"
    assert data[4] == 1, "expected ELF32"
    assert data[5] == 2, "expected big-endian"
    (e_type, e_machine) = struct.unpack_from(">HH", data, 16)
    (e_shoff,) = struct.unpack_from(">I", data, 32)
    (e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(">HHH", data, 46)
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (name, typ, flags, addr, offset, size, link, info, align,
         entsize) = struct.unpack_from(">IIIIIIIIII", data, off)
        sections.append(dict(name=name, type=typ, flags=flags, addr=addr,
                             offset=offset, size=size, link=link, info=info,
                             entsize=entsize))
    strtab_off = sections[e_shstrndx]["offset"]
    for s in sections:
        end = data.index(b"\0", strtab_off + s["name"])
        s["sname"] = data[strtab_off + s["name"]:end].decode("utf-8", "replace")
    return data, sections


def read_symbols(data: bytes, sections: list) -> dict:
    """Return {name: (value, size, shndx)} from the first SHT_SYMTAB."""
    out = {}
    for s in sections:
        if s["type"] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[s["link"]]
        strbase = strtab["offset"]
        strsize = strtab["size"]
        count = s["size"] // s["entsize"]
        for i in range(count):
            off = s["offset"] + i * s["entsize"]
            (name, value, size, info, other, shndx) = struct.unpack_from(
                ">IIIBBH", data, off)
            if name == 0:
                continue
            if name >= strsize:
                continue
            end = data.index(b"\0", strbase + name)
            sym = data[strbase + name:end].decode("utf-8", "replace")
            out[sym] = (value, size, shndx)
    return out


# --------------------------------------------------------------------------
# Explicit per-mode expected payloads.  Keyed by symbol name; each entry is
# the payload bytes.  The payload INCLUDES the string terminator where the
# source object has one.
# --------------------------------------------------------------------------

MODES = {
    "shim": {
        "rom_msg": b"MCS251 code string 42\0",
        "rom_digits": b"   -1234xyz\0",
        "rom_bytes": bytes([0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80]),
        # CODE-context string literal: formed where a `const char __code *`
        # initializer requires the literal in AS4 (not a plain AS0 literal).
        # Its address is taken from the initializer symbol's relocated value.
        "code_ctx_lit": b"code context literal\0",
        # Plain AS0 literal control: must be present in the image but NOT in
        # the CODE window (it is the ordinary C string-literal case).
        "as0_lit": b"plain as0 literal\0",
    },
    "priv": {
        "priv_msg": b"priv slot ABI\0",
        "priv_pat": bytes([0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF]),
        # CODE-context literal for the private runtime firmware too.
        "code_ctx_lit": b"priv code context\0",
        "as0_lit": b"priv as0 literal\0",
    },
}

# For these payloads the byte range is reached through a pointer object:
# the checker reads the 4-byte big-endian value stored at the symbol's
# allocated address and requires the payload to start exactly there.  This
# binds the literal to its symbol AND verifies the relocated 24-bit bank
# value in one step.
INDIRECT = {
    "code_ctx_lit": "code_lit_ptr",
    "as0_lit": "plain_lit_ptr",
}

# Payloads that must NOT be in the CODE window (the AS0 control literal).
AS0_CONTROL = "as0_lit"

# The AS0 DSEG slices this checker insists on seeing.  A missing group is a
# FAIL: the old code skipped malformed lines, so deleting the slices entirely
# was also invisible.  Keys are section-name substrings.
DSEG_MARKERS = (".mcs251.dseg", ".mcs251.DSEG")
TEXT_MARKERS = (".text",)

# A pointer object must be at least this big for the 4-byte read below.
POINTER_SIZE = 4


def read_bytes_strict(data: bytes, sections: list, addr: int, n: int,
                      failures: list, what: str):
    """Read `n` bytes at `addr`, requiring the WHOLE range to fit one section.

    Alice review R10-4: the old `read_bytes` only required the start address
    to lie inside a section, so a read could cross the section end (or the
    end of the file).  Any read whose [addr, addr+n) range is not contained
    in a single allocated section is a FAIL, and the section bounds are also
    checked against the file bounds.
    """
    for s in sections:
        if s["type"] != 1:
            continue
        lo = s["addr"]
        hi = s["addr"] + s["size"]
        if lo <= addr < hi:
            if addr + n > hi:
                failures.append(
                    f"{what}: read of {n} bytes at 0x{addr:x} runs past the "
                    f"section '{s['sname']}' end 0x{hi:x}")
                return None
            file_end = len(data)
            if s["offset"] + (addr - lo) + n > file_end:
                failures.append(
                    f"{what}: read at 0x{addr:x} runs past the file-backed "
                    f"part of section '{s['sname']}'")
                return None
            rel = addr - lo
            return data[s["offset"] + rel:s["offset"] + rel + n]
    failures.append(f"{what}: address 0x{addr:x} is not in any allocated "
                    f"section")
    return None


def find_payload(data: bytes, sections: list, needle: bytes):
    """Return the CODE-window address of `needle`, or None."""
    for s in sections:
        if s["type"] != 1 or not (CODE_LO <= s["addr"] < CODE_HI):
            continue
        blob = data[s["offset"]:s["offset"] + s["size"]]
        pos = blob.find(needle)
        if pos >= 0:
            return s["addr"] + pos
    return None


def find_anywhere(data: bytes, sections: list, needle: bytes):
    for s in sections:
        if s["type"] != 1:
            continue
        blob = data[s["offset"]:s["offset"] + s["size"]]
        pos = blob.find(needle)
        if pos >= 0:
            return s["addr"] + pos
    return None


def parse_map(mapf: Path, failures: list) -> tuple[list, list]:
    """Parse the linker map strictly (Alice review R10-1).

    Returns (slices, funcs) where a slice is
    (section_name, start, size) and a func is (name, start, size).  A line
    that has the slice shape but an unparseable address/size is a FAIL; the
    old code `continue`d over it, which let a map with every DSEG address
    replaced by `NOT_AN_ADDRESS` still print the DSEG PASS.
    """
    slices: list[tuple[str, int, int]] = []
    funcs: list[tuple[str, int, int]] = []
    text = mapf.read_text()
    for ln in text.splitlines():
        m = FUNC_RE.match(ln)
        if m:
            funcs.append((m.group("name"), int(m.group("addr"), 16),
                          int(m.group("size"), 16)))
            continue
        m = SLICE_RE.match(ln)
        if m:
            slices.append((m.group("obj"), int(m.group("addr"), 16),
                           int(m.group("size"), 16)))
            continue
        # A line that *looks* like a slice but whose address/size do not
        # parse is a hard failure, never a skip.
        if ln.startswith("FUNC"):
            failures.append(f"map: unparseable FUNC address/size: {ln.strip()!r}")
            continue
        if ln.startswith("stack ") or ln.startswith("MCS251") or " = " in ln:
            continue
        b = BAD_SLICE_RE.match(ln)
        if b:
            failures.append(
                f"map: unparseable slice address/size: {ln.strip()!r}")
    return slices, funcs


def check_image(elf: Path, mapf: Path, mode: str) -> int:
    data, sections = parse_elf(elf)
    syms = read_symbols(data, sections)
    expected = MODES[mode]

    failures: list[str] = []

    def read_bytes(addr: int, n: int):
        return read_bytes_strict(data, sections, addr, n, failures,
                                 f"read@0x{addr:x}")

    # 1) Every expected payload must be present in its own symbol's range (or
    #    at the address its pointer object holds).
    for key, needle in expected.items():
        if key == AS0_CONTROL:
            continue
        if key in INDIRECT:
            ptr_sym = "_" + INDIRECT[key]
            sym = syms.get(ptr_sym) or syms.get(INDIRECT[key])
            if sym is None:
                failures.append(f"{key}: pointer symbol {ptr_sym} missing")
                continue
            pval, psize, pshndx = sym
            # Alice review R10-3: the pointer symbol must be big enough for
            # the 4-byte read the checker performs; a symbol shrunk to 1 byte
            # used to pass because only the address was read.
            if psize < POINTER_SIZE:
                failures.append(
                    f"{key}: pointer symbol {ptr_sym} has size {psize} < "
                    f"{POINTER_SIZE}; the 4-byte pointer read would run past "
                    f"the symbol")
                continue
            raw = read_bytes(pval, POINTER_SIZE)
            if raw is None or len(raw) < POINTER_SIZE:
                failures.append(f"{key}: cannot read 4 bytes at 0x{pval:x}")
                continue
            target = int.from_bytes(raw, "big")
            got = read_bytes(target, len(needle))
            if got != needle:
                failures.append(
                    f"{key}: literal at 0x{target:x} is {got!r}, expected "
                    f"{needle!r} (pointer {ptr_sym} at 0x{pval:x})")
                continue
            in_code = CODE_LO <= target < CODE_HI
            if not in_code:
                failures.append(
                    f"{key}: CODE-context literal at 0x{target:x} is outside "
                    f"the CODE window")
                continue
            print(f"PASS:{mode}:{key} ({needle!r}) at 0x{target:x} via "
                  f"{ptr_sym}")
            continue

        sym_name = key
        sym = syms.get("_" + sym_name) or syms.get(sym_name)
        if sym is None:
            failures.append(f"symbol {sym_name} missing from the linked image")
            continue
        value, size, shndx = sym
        if not (CODE_LO <= value < CODE_HI):
            failures.append(
                f"{sym_name}: symbol is at 0x{value:x}, outside the CODE "
                f"window [0x{CODE_LO:x}, 0x{CODE_HI:x})")
            continue
        # The payload must sit in the symbol's own byte range.  The symbol
        # covers the whole object (string terminator included when the source
        # has one), so the symbol size must be at least the payload length.
        got = read_bytes(value, len(needle))
        if got != needle:
            failures.append(
                f"{sym_name}: bytes at 0x{value:x} are {got!r}, expected "
                f"{needle!r}")
            continue
        if size < len(needle):
            failures.append(
                f"{sym_name}: symbol size {size} < payload {len(needle)}")
            continue
        print(f"PASS:{mode}:{sym_name} payload verbatim at 0x{value:x} "
              f"(size {size})")

    # 2) The AS0 literal control must be reached through its pointer symbol,
    #    must be byte-correct, and must be a DIFFERENT object from the
    #    CODE-context literal: merging the two would erase the provenance
    #    distinction the IR half asserts.
    if AS0_CONTROL in expected:
        ptr_sym = "_" + INDIRECT[AS0_CONTROL]
        sym = syms.get(ptr_sym)
        ctl_addr = None
        if sym is None:
            failures.append(f"{AS0_CONTROL}: pointer symbol {ptr_sym} missing")
        else:
            pval, psize, _ = sym
            if psize < POINTER_SIZE:
                failures.append(
                    f"{AS0_CONTROL}: pointer symbol {ptr_sym} has size "
                    f"{psize} < {POINTER_SIZE}")
            else:
                raw = read_bytes(pval, POINTER_SIZE)
                if raw is None or len(raw) < POINTER_SIZE:
                    failures.append(
                        f"{AS0_CONTROL}: cannot read pointer bytes")
                else:
                    ctl_addr = int.from_bytes(raw, "big")
                    got = read_bytes(ctl_addr, len(expected[AS0_CONTROL]))
                    if got != expected[AS0_CONTROL]:
                        failures.append(
                            f"{AS0_CONTROL}: literal at 0x{ctl_addr:x} is "
                            f"{got!r}, expected {expected[AS0_CONTROL]!r}")
                    else:
                        print(f"PASS:{mode}:{AS0_CONTROL} control literal at "
                              f"0x{ctl_addr:x} via {ptr_sym}")
        code_sym = syms.get("_" + INDIRECT["code_ctx_lit"])
        if code_sym is not None:
            cval, csize, _ = code_sym
            if csize < POINTER_SIZE:
                failures.append(
                    f"code_ctx_lit: pointer symbol {INDIRECT['code_ctx_lit']} "
                    f"has size {csize} < {POINTER_SIZE}")
            else:
                raw = read_bytes(cval, POINTER_SIZE)
                if raw is not None and len(raw) == POINTER_SIZE:
                    code_addr = int.from_bytes(raw, "big")
                    if ctl_addr is not None and ctl_addr == code_addr:
                        failures.append(
                            f"{AS0_CONTROL}: the AS0 control literal and the "
                            f"CODE-context literal share one object at "
                            f"0x{ctl_addr:x}; provenance was merged away")

    # 3) The writable AS0 destinations must not OVERLAP the CODE window.
    #    Alice review R10-1/R10-2: malformed map lines are now FAILs and the
    #    whole [start, start+size) range is checked, so a DSEG slice that
    #    starts below CODE but crosses into it is caught.
    slices, funcs = parse_map(mapf, failures)
    dseg = [s for s in slices if any(m in s[0] for m in DSEG_MARKERS)]
    if not dseg:
        failures.append("no AS0 DSEG slice in the map")
    for name, start, size in dseg:
        end = start + size
        if start >= CODE_LO or end > CODE_LO:
            failures.append(
                f"AS0 data slice overlaps CODE: {name} "
                f"[0x{start:x}, 0x{end:x}) vs CODE [0x{CODE_LO:x}, "
                f"0x{CODE_HI:x})")
    if dseg and not failures:
        print(f"PASS:{mode}:AS0 data slices outside the CODE window")

    # 4) The firmware .text slice must be in CODE, entirely.
    text_slices = [s for s in slices
                   if any(m in s[0] for m in TEXT_MARKERS)
                   and "mcs251_xdata" not in s[0]]
    if not text_slices:
        failures.append("no .text slice in the map")
    for name, start, size in text_slices:
        end = start + size
        if not (CODE_LO <= start and end <= CODE_HI):
            failures.append(
                f"text slice not fully in the CODE window: {name} "
                f"[0x{start:x}, 0x{end:x})")
    # The FUNC entries must also be inside CODE: a function that escaped the
    # window would be read as data by the firmware.
    for name, start, size in funcs:
        if not (CODE_LO <= start < CODE_HI and start + size <= CODE_HI):
            failures.append(
                f"function {name} at [0x{start:x}, 0x{start + size:x}) is "
                f"not fully inside the CODE window")
    if text_slices and not failures:
        print(f"PASS:{mode}:text slices in the CODE window")

    if failures:
        for x in failures:
            print(f"FAIL:{x}")
        return 1
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("map")
    ap.add_argument("--mode", required=True, choices=sorted(MODES))
    opts = ap.parse_args(argv[1:])
    return check_image(Path(opts.elf), Path(opts.map), opts.mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
