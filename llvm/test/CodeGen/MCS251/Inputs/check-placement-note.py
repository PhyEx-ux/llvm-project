#!/usr/bin/env python3
"""Independent decoder/oracle for the G11-B `.mcs251.placement` NOTE table.

Usage: check-placement-note.py [--no-init-for sym1[,sym2...]] <object.o> \
           <stable> <class> <entity> <own> <addr> <size> <align> <flags> \
           [more records...]

Decodes the ELF SHT_NOTE section named .mcs251.placement directly from the
object file (no library dependency: minimal ELF32 big-endian reader), checks
the envelope (namesz=7, name "MCS251\\0" + one pad byte, type=1, descsz ==
the remaining bytes), walks every record with the frozen v1 layout, verifies
the CANONICAL record_size arithmetic -- record_size must equal exactly
alignTo(25 + stable_len, 4); zero padding beyond the canonical multiple is a
malformed record, not an accepted encoding (review 2026-09-16 [建议]: the
old check accepted a record with four extra zero bytes and a bumped
record_size/descsz) -- recomputes SHA-256 H_source over
BE(schema_version, storage_class, entity, ownership, address, align, flags)
(size and the stable symbol are excluded by design, rev 7 B1), and asserts
the SECTION-level schema: exactly one .mcs251.placement section, SHT_NOTE,
sh_flags == 0, sh_addralign == 4. The positional records are then matched
one-to-one against the decoded table. Any mismatch exits nonzero (a lit
FAIL, never a SKIP).

With --no-init-for the oracle additionally parses every relocation in
.mcs251.xinit / .mcs251.xdata_init (the startup clear/copy record tables),
resolves each destination symbol through the linked symbol table, and fails
when any listed symbol is an init-record destination -- the strong "noinit
entity is never initialized" assertion (an interval ELF-NOT cannot see a
record the section list does not mention).
"""

import hashlib
import struct
import sys


def die(msg):
    sys.stderr.write("check-placement-note: FAIL: %s\n" % msg)
    sys.exit(1)


def read_sections(data):
    """Return the ELF32-BE section table as a list of dicts."""
    if data[:4] != b"\x7fELF":
        die("not an ELF object")
    if data[4] != 1 or data[5] != 2:
        die("not ELF32 big-endian (class=%d data=%d)" % (data[4], data[5]))
    e_shoff, = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    if not e_shoff or not e_shnum:
        die("no section headers")
    strtab_off = struct.unpack_from(">I", data, e_shoff + e_shstrndx * e_shentsize + 0x10)[0]
    strtab_size = struct.unpack_from(">I", data, e_shoff + e_shstrndx * e_shentsize + 0x14)[0]
    strtab = data[strtab_off:strtab_off + strtab_size]

    def name_at(off):
        end = strtab.index(b"\0", off)
        return strtab[off:end].decode()

    sections = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        sh = {}
        sh["idx"] = i
        sh["name"] = name_at(struct.unpack_from(">I", data, base)[0])
        sh["type"], sh["flags"] = struct.unpack_from(">II", data, base + 0x04)
        sh["off"], sh["size"] = struct.unpack_from(">II", data, base + 0x10)
        sh["link"], = struct.unpack_from(">I", data, base + 0x18)
        sh["addralign"], = struct.unpack_from(">I", data, base + 0x20)
        sh["data"] = data[sh["off"]:sh["off"] + sh["size"]]
        sections.append(sh)
    return sections


def read_note_section(sections):
    hits = [s for s in sections if s["name"] == ".mcs251.placement"]
    if not hits:
        die("no .mcs251.placement section")
    # Schema-level checks: the NOTE carrier is unique and has the frozen
    # section identity (SHT_NOTE, flags 0, align 4).
    if len(hits) != 1:
        die("%d .mcs251.placement sections (must be exactly 1)" % len(hits))
    s = hits[0]
    if s["type"] != 7:  # SHT_NOTE
        die(".mcs251.placement is not SHT_NOTE")
    if s["flags"] != 0:
        die(".mcs251.placement has sh_flags=0x%X (want 0)" % s["flags"])
    if s["addralign"] != 4:
        die(".mcs251.placement has sh_addralign=%d (want 4)" % s["addralign"])
    return s["data"]


def check_no_init_for(sections, forbidden):
    """Fail when any .xinit/.xdata_init relocation names a forbidden symbol."""
    for s in sections:
        if s["name"] not in (".rela.mcs251.xinit", ".rela.mcs251.xdata_init"):
            continue
        if s["type"] != 4:  # SHT_RELA
            die("%s is not SHT_RELA" % s["name"])
        symtab = sections[s["link"]]
        strtab = sections[symtab["link"]]["data"]
        dests = set()
        for off in range(0, len(s["data"]) - len(s["data"]) % 12, 12):
            _, r_info = struct.unpack_from(">II", s["data"], off)
            sym_idx = r_info >> 8
            if sym_idx == 0:
                continue
            sym = symtab["data"][sym_idx * 16:(sym_idx + 1) * 16]
            if len(sym) != 16:
                die("%s: symbol index %d out of range" % (s["name"], sym_idx))
            st_name, = struct.unpack_from(">I", sym, 0)
            end = strtab.index(b"\0", st_name)
            dests.add(strtab[st_name:end].decode())
        bad = dests & forbidden
        if bad:
            die("init record table %s initializes noinit entities: %s"
                % (s["name"], ", ".join(sorted(bad))))


def decode(note):
    namesz, descsz, ntype = struct.unpack_from(">III", note, 0)
    if namesz != 7:
        die("namesz=%d, want 7" % namesz)
    if ntype != 1:
        die("note type=%d, want 1 (placement v1)" % ntype)
    if note[12:19] != b"MCS251\x00" or note[19] != 0:
        die("note name %r is not 'MCS251\\0' + one pad byte" % note[12:20])
    if descsz != len(note) - 20:
        die("descsz=%d, table is %d bytes" % (descsz, len(note) - 20))
    records = []
    off = 20
    while off < len(note):
        recsz, = struct.unpack_from(">I", note, off)
        r = note[off + 4:off + 4 + recsz]
        if len(r) != recsz:
            die("record at %d truncated" % off)
        if recsz % 4:
            die("record size %d not a multiple of 4" % recsz)
        schema, sc, ent, own = r[0], r[1], r[2], r[3]
        addr, size, align, flags, hsh = struct.unpack_from(">IIIII", r, 4)
        if schema != 1:
            die("schema_version=%d, want 1" % schema)
        if sc > 2 or ent > 1 or own > 1 or flags & ~3:
            die("unknown storage_class/entity/ownership/flags encoding")
        if own == 1 and (flags & 1):
            die("bind record with retain bit set (malformed)")
        slen = r[24]
        if 24 + 1 + slen > recsz:
            die("stable_len overflows record_size")
        # Canonical padding: the record size is EXACTLY the 4-byte multiple
        # of 25 + stable_len (24 fixed + stable_len byte + stable bytes).
        # Anything larger is non-canonical even when the tail is zero.
        canonical = (25 + slen + 3) // 4 * 4
        if recsz != canonical:
            die("record for %r has record_size=%d, canonical alignTo(25+%d,4)=%d"
                % (r[25:25 + slen].decode(errors="replace"), recsz, slen,
                   canonical))
        stable = r[25:25 + slen].decode()
        pad = recsz - 24 - 1 - slen
        if r[25 + slen:25 + slen + pad] != b"\x00" * pad:
            die("stable-symbol padding is not zero")
        digest = hashlib.sha256(
            struct.pack(">BBBBIII", 1, sc, ent, own, addr, align,
                        flags)).digest()
        calc = struct.unpack(">I", digest[28:32])[0]
        if hsh != calc:
            die("H_source mismatch for %s: stored %08X recomputed %08X"
                % (stable, hsh, calc))
        records.append((stable, sc, ent, own, addr, size, align, flags))
        off += 4 + recsz
    return records


def main():
    args = sys.argv[1:]
    forbidden = set()
    if args and args[0].startswith("--no-init-for="):
        forbidden = {s for s in args[0][len("--no-init-for="):].split(",") if s}
        args = args[1:]
    if len(args) < 2 or (len(args) - 1) % 8:
        die("usage: check-placement-note.py [--no-init-for s1,s2] <object> "
            "<stable> <class> <entity> <own> <addr> <size> <align> <flags> "
            "[xN]")
    with open(args[0], "rb") as f:
        data = f.read()
    sections = read_sections(data)
    records = decode(read_note_section(sections))
    if forbidden:
        check_no_init_for(sections, forbidden)
    want = []
    pos = args[1:]
    for i in range(0, len(pos), 8):
        want.append((pos[i], int(pos[i + 1]), int(pos[i + 2]),
                     int(pos[i + 3]), int(pos[i + 4], 0),
                     int(pos[i + 5]), int(pos[i + 6]), int(pos[i + 7])))
    if records != want:
        die("record mismatch:\n  decoded %r\n  want    %r" % (records, want))
    print("check-placement-note: %d records OK" % len(records))


if __name__ == "__main__":
    main()
