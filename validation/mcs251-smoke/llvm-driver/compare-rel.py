#!/usr/bin/env python3

"""Semantically compare two ASxxxx XH3 .rel files (sdas251 vs llc -filetype=obj).

The writers order S records differently on purpose (sdas uses hash-table
order, llc uses undefined-sorted-then-defined-by-offset), and sdas adds inert
empty T/R pairs at label boundaries, so a byte diff is not meaningful.  This
script canonicalizes both files and compares what sdld consumes:

  * XH3 radix line, H counts, M module name, O signature (exact strings)
  * A records by area name (size/flags/addr)
  * S records by symbol name (Ref/Def + value)
  * T payload bytes per absolute address (union; conflicting writes fail)
  * R entries as (address, mode, target) multisets, with the 16-bit
    reference index resolved to a symbol name (or the area name)

The comparator assumes the Phase-13a subset: no inflated 1-byte relocatable
fields, so T payload offsets equal address offsets.  It verifies the payload
byte count matches the area size, which catches any violation loudly.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field


@dataclass
class RelFile:
    radix: str = ""
    areas_count: str = ""
    globals_count: str = ""
    module: str = ""
    options: str = ""
    areas: dict[str, tuple[str, str, str]] = field(default_factory=dict)
    area_order: list[str] = field(default_factory=list)
    symbols: dict[str, tuple[str, int]] = field(default_factory=dict)
    sym_index: dict[int, str] = field(default_factory=dict)
    payload: dict[int, int] = field(default_factory=dict)
    relocs: list[tuple[int, int, str]] = field(default_factory=list)


def parse(path: str) -> RelFile:
    result = RelFile()
    lines = open(path).read().splitlines()
    i = 0
    last_t_addr = 0
    last_t_len = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if not line:
            continue
        tag, _, rest = line.partition(" ")
        if tag == "XH3":
            result.radix = line
        elif tag == "H":
            parts = rest.split()
            result.areas_count = parts[0]
            result.globals_count = parts[2]
        elif tag == "M":
            result.module = rest
        elif tag == "O":
            result.options = rest
        elif tag == "S":
            parts = rest.split()
            name = parts[0]
            kindval = parts[1]
            kind = kindval[:3]
            value = int(kindval[3:], 16)
            idx = len(result.sym_index)
            result.symbols[name] = (kind, value)
            result.sym_index[idx] = name
        elif tag == "A":
            parts = rest.split()
            name = parts[0]
            result.areas[name] = (parts[2], parts[4], parts[6])
            result.area_order.append(name)
        elif tag == "T":
            toks = rest.split()
            addr = int("".join(toks[:3]), 16)
            payload = toks[3:]
            last_t_addr = addr
            last_t_len = len(payload)
            for off, tok in enumerate(payload):
                absaddr = addr + off
                value = int(tok, 16)
                if absaddr in result.payload and result.payload[absaddr] != value:
                    raise RuntimeError(
                        f"{path}: conflicting T bytes at 0x{absaddr:06x}"
                    )
                result.payload[absaddr] = value
        elif tag == "R":
            toks = rest.split()
            # header: 00 00 <area index 16-bit>
            area_idx = int(toks[2] + toks[3], 16)
            area_name = result.area_order[area_idx]
            j = 4
            while j < len(toks):
                mode_tok = toks[j]
                j += 1
                if mode_tok.startswith("F"):
                    mode = (int(mode_tok[1], 16) << 8) | int(toks[j], 16)
                    j += 1
                else:
                    mode = int(mode_tok, 16)
                t_index = int(toks[j], 16)
                j += 1
                ref = int(toks[j] + toks[j + 1], 16)
                j += 2
                address = last_t_addr + (t_index - 3)
                if address >= last_t_addr + last_t_len:
                    raise RuntimeError(
                        f"{path}: R index {t_index} beyond its T line"
                    )
                if mode & 0x02:
                    target = result.sym_index[ref]
                else:
                    target = f"area:{area_name}"
                result.relocs.append((address, mode, target))
    return result


def compare(a_path: str, b_path: str) -> list[str]:
    a = parse(a_path)
    b = parse(b_path)
    errors = []

    def check(what: str, x, y) -> None:
        if x != y:
            errors.append(f"{what}: {x!r} != {y!r}")

    check("radix", a.radix, b.radix)
    check("H areas", a.areas_count, b.areas_count)
    check("H globals", a.globals_count, b.globals_count)
    check("M", a.module, b.module)
    check("O", a.options, b.options)
    check("areas", a.areas, b.areas)
    check("symbols", a.symbols, b.symbols)
    check("payload", a.payload, b.payload)
    check("relocs", sorted(a.relocs), sorted(b.relocs))

    # Self-consistency: payload size must equal the CSEG area size (no
    # inflated fields in the Phase-13a subset).
    for label, r in (("A", a), ("B", b)):
        size = int(r.areas.get("CSEG", ("0",))[0], 16)
        if len(r.payload) != size:
            errors.append(
                f"{label}: payload bytes {len(r.payload)} != CSEG size {size}"
            )
    return errors


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <a.rel> <b.rel>", file=sys.stderr)
        return 2
    errors = compare(sys.argv[1], sys.argv[2])
    if errors:
        for e in errors:
            print(f"DIFF: {e}")
        return 1
    print(f"MATCH: {sys.argv[1]} == {sys.argv[2]} (semantically)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
