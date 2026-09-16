#!/usr/bin/env python3
"""G13b G-T12: independent byte oracle for synthesized XDATA_INIT records.

The oracle deliberately knows nothing about the linker implementation: it
re-reads the produced ET_EXEC artifact from disk (raw ELF32 big-endian
struct parsing, no llvm tools), locates the linker-owned XDATA_INIT table
through the map's boundary symbols, and compares every synthesized record
byte against an EXPECTED table supplied on the command line (the design
derivation table in G13B-XDATA-DESIGN-draft.md section 3.1.3 -- never the
linker's own output).  All comparisons are integer comparisons.

Checks (every invocation):
  * the map reports s_XDATA_INIT / l_XDATA_INIT and both parse as integers;
  * a PT_LOAD segment starts exactly at s_XDATA_INIT and carries exactly
    l_XDATA_INIT bytes (the ROM image really contains the table);
  * every 7-byte record parses: payload_size == 0 (clear only),
    1 <= object_size <= 0xffff, destination stays inside ONE 64K window;
  * the record destinations TILE [section-start, section-end) exactly:
    first destination == section start, no gaps, no overlaps, last end ==
    section end (the synthesis-specific coverage invariant that
    validateXDATAInit does not check -- design section 2.7.4);
  * with --expect (a comma list of ADDR:LEN in hex, absolute addresses),
    the record (destination, object_size) sequence equals that table
    exactly, including order and count.

Usage:
  g13b-record-oracle.py --elf OUT.elf --map OUT.map \
      --section-start 0x010000 --section-end 0x020000 \
      --expect 0x010000:0xffff,0x01ffff:0x1

Exit 0 = all checks passed; any mismatch exits 1 with a printed diff.
"""
import argparse
import re
import struct
import sys

PT_LOAD = 1


def parse_map_boundary(map_path, name):
    pat = re.compile(r"^\s*" + re.escape(name) + r"\s*=\s*0x([0-9a-fA-F]+)\s*$",
                     re.M)
    m = pat.search(open(map_path, "r", errors="replace").read())
    if not m:
        raise SystemExit("oracle: %s not found in %s" % (name, map_path))
    return int(m.group(1), 16)


def load_segment_bytes(elf_path, vaddr, size):
    """Return the file bytes of the PT_LOAD segment at vaddr."""
    blob = open(elf_path, "rb").read()
    if len(blob) < 52 or blob[:4] != b"\x7fELF":
        raise SystemExit("oracle: %s is not an ELF file" % elf_path)
    e_ident_class, e_ident_data = blob[4], blob[5]
    if e_ident_class != 1 or e_ident_data != 2:
        raise SystemExit("oracle: %s is not ELF32 big-endian" % elf_path)
    e_type, = struct.unpack_from(">H", blob, 16)
    if e_type != 2:
        raise SystemExit("oracle: %s is not ET_EXEC" % elf_path)
    e_phoff, = struct.unpack_from(">I", blob, 28)
    e_phentsize, e_phnum = struct.unpack_from(">HH", blob, 42)
    for i in range(e_phnum):
        ph = blob[e_phoff + i * e_phentsize:]
        p_type, p_offset, p_vaddr = struct.unpack_from(">III", ph, 0)
        p_filesz, = struct.unpack_from(">I", ph, 16)
        if p_type == PT_LOAD and p_vaddr == vaddr:
            if p_filesz != size:
                raise SystemExit(
                    "oracle: PT_LOAD at 0x%x carries %d bytes, map says %d"
                    % (vaddr, p_filesz, size))
            return blob[p_offset:p_offset + p_filesz]
    raise SystemExit("oracle: no PT_LOAD segment at 0x%x in %s"
                     % (vaddr, elf_path))


def parse_records(table):
    records = []
    off = 0
    while off != len(table):
        if len(table) - off < 7:
            raise SystemExit("oracle: truncated record at table offset %d"
                             % off)
        bank, whi, wlo, ohi, olo, phi, plo = table[off:off + 7]
        records.append({
            "dest": (bank << 16) | (whi << 8) | wlo,
            "size": (ohi << 8) | olo,
            "payload": (phi << 8) | plo,
        })
        off += 7
    return records


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--section-start", required=True)
    ap.add_argument("--section-end", required=True)
    ap.add_argument("--expect", default=None,
                    help="expected record table ADDR:LEN (hex), comma list")
    args = ap.parse_args()
    sec_start = int(args.section_start, 0)
    sec_end = int(args.section_end, 0)

    s_init = parse_map_boundary(args.map, "s_XDATA_INIT")
    l_init = parse_map_boundary(args.map, "l_XDATA_INIT")
    if l_init == 0:
        raise SystemExit("oracle: l_XDATA_INIT is zero -- no table emitted")

    table = load_segment_bytes(args.elf, s_init, l_init)
    records = parse_records(table)

    # Structural checks (independent of the expected table).
    cursor = sec_start
    for i, r in enumerate(records):
        if r["payload"] != 0:
            raise SystemExit("oracle: record %d is not clear-only "
                             "(payload_size=%d)" % (i, r["payload"]))
        if not 1 <= r["size"] <= 0xFFFF:
            raise SystemExit("oracle: record %d length %d outside the u16 "
                             "record limit" % (i, r["size"]))
        if (r["dest"] ^ (r["dest"] + r["size"] - 1)) & 0xFF0000:
            raise SystemExit("oracle: record %d [0x%x,0x%x) spans a 64K "
                             "window" % (i, r["dest"], r["dest"] + r["size"]))
        if r["dest"] != cursor:
            raise SystemExit(
                "oracle: record %d starts at 0x%x, expected the contiguous "
                "cursor 0x%x (gap/overlap/wrong start)" % (i, r["dest"],
                                                           cursor))
        cursor += r["size"]
    if cursor != sec_end:
        raise SystemExit("oracle: record table covers [0x%x,0x%x) but the "
                         "section is [0x%x,0x%x) (under/over-coverage)"
                         % (sec_start, cursor, sec_start, sec_end))

    # Exact comparison against the independent expected table.
    if args.expect:
        want = []
        for cell in args.expect.split(","):
            addr, ln = cell.split(":")
            want.append((int(addr, 0), int(ln, 0)))
        got = [(r["dest"], r["size"]) for r in records]
        if got != want:
            raise SystemExit("oracle: record table mismatch\n  expected: "
                             + "\n            ".join("(0x%x,0x%x)" % w
                                                     for w in want)
                             + "\n  got:      "
                             + "\n            ".join("(0x%x,0x%x)" % g
                                                     for g in got))
        if len(records) != len(want):
            raise SystemExit("oracle: record count %d != expected %d"
                             % (len(records), len(want)))
    print("oracle: %d record(s), table [0x%x,0x%x) at 0x%x: OK"
          % (len(records), sec_start, sec_end, s_init))
    return 0


if __name__ == "__main__":
    sys.exit(main())
