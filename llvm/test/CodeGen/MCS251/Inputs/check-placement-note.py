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

G11-N4 (design rev 8 §8.3): the v1 layout above is ASSERTED UNCHANGED even
when the association NOTE is present. The `.mcs251.placement.names` NOTE is
decoded separately and independently: SHT_NOTE, flags 0, align 4, namesz=7,
name "MCS251\\0" + pad, type=2, desc = BE u32 association_version (==1) +
BE u32 entry_count + entries of BE u32 placement_record_index, BE u32
elf_name_len, elf_name bytes, zero pad to a canonical 4-byte multiple. The
oracle then closes the association against the object itself, not against the
writer's word: record_index must cover 0..count-1 exactly once and equal the
v1 record count; every name must be nonempty and NUL-free; every name must
exist in .symtab; an owned record's name must be the UNIQUE principal symbol
(defined, non-SECTION type) of the `.mcu.fixed.<stable>` section derived from
the same-index v1 record; a bind record's name must be a GLOBAL UNDEFINED
symbol (the "must exist as an undefined external even without a code
reference" contract). Association failures are FAILs.

With --no-init-for the oracle additionally parses every relocation in
.mcs251.xinit / .mcs251.xdata_init (the startup clear/copy record tables),
resolves each destination symbol through the linked symbol table, and fails
when any listed symbol is an init-record destination -- the strong "noinit
entity is never initialized" assertion (an interval ELF-NOT cannot see a
record the section list does not mention).

With --names-self-test the association decoder itself is negatively
controlled (design rev 8 §8.3: "reader 用有界长度检查，拒绝截断、非规范填充、
缺条目或多条目"): the REAL NOTE bytes just accepted are mutated in place --
placement_record_index out of range, an ELF name absent from .symtab, a wrong
NOTE type, a wrong association_version -- and each mutation must be rejected
by the same cross-validation.  A mutation that still passes proves the oracle
does not constrain that field.
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


def read_symtab(sections):
    """Return [{name, value, size, info, shndx}] from the .symtab/.strtab."""
    hits = [s for s in sections if s["name"] == ".symtab"]
    if len(hits) != 1:
        die("%d .symtab sections (must be exactly 1)" % len(hits))
    s = hits[0]
    if s["type"] != 2:  # SHT_SYMTAB
        die(".symtab is not SHT_SYMTAB")
    strtab = sections[s["link"]]["data"]

    def name_at(off):
        end = strtab.index(b"\0", off)
        return strtab[off:end].decode()

    syms = []
    data = s["data"]
    for off in range(0, len(data) - len(data) % 16, 16):
        st_name, st_value, st_size, st_info, st_other, st_shndx = \
            struct.unpack_from(">IIIBBH", data, off)
        syms.append({
            "name": name_at(st_name),
            "value": st_value,
            "size": st_size,
            "info": st_info,
            "bind": st_info >> 4,
            "type": st_info & 0xF,
            "shndx": st_shndx,
        })
    return syms


def decode_names(sections):
    """Decode the G11-N4 `.mcs251.placement.names` association NOTE."""
    hits = [s for s in sections if s["name"] == ".mcs251.placement.names"]
    if not hits:
        die("no .mcs251.placement.names section (association carrier missing)")
    if len(hits) != 1:
        die("%d .mcs251.placement.names sections (must be exactly 1)"
            % len(hits))
    s = hits[0]
    if s["type"] != 7:  # SHT_NOTE
        die(".mcs251.placement.names is not SHT_NOTE")
    if s["flags"] != 0:
        die(".mcs251.placement.names has sh_flags=0x%X (want 0, non-ALLOC so "
            "it cannot enter the output image)" % s["flags"])
    if s["addralign"] != 4:
        die(".mcs251.placement.names has sh_addralign=%d (want 4)"
            % s["addralign"])
    note = s["data"]
    namesz, descsz, ntype = struct.unpack_from(">III", note, 0)
    if namesz != 7:
        die("names namesz=%d, want 7" % namesz)
    if ntype != 2:
        die("names note type=%d, want 2 (placement-name association)" % ntype)
    if note[12:19] != b"MCS251\x00" or note[19] != 0:
        die("names note name %r is not 'MCS251\\0' + one pad byte"
            % note[12:20])
    if descsz != len(note) - 20:
        die("names descsz=%d, table is %d bytes" % (descsz, len(note) - 20))
    if descsz < 8:
        die("names descsz=%d shorter than the two u32 prologue" % descsz)
    assoc_version, entry_count = struct.unpack_from(">II", note, 20)
    if assoc_version != 1:
        die("association_version=%d, want 1" % assoc_version)
    entries = []
    off = 28
    for _ in range(entry_count):
        if off + 8 > len(note):
            die("names entry at %d truncated (header)" % off)
        rec_index, name_len = struct.unpack_from(">II", note, off)
        off += 8
        if name_len == 0:
            die("names entry %d has an empty ELF name" % rec_index)
        if off + name_len > len(note):
            die("names entry %d truncated (name)" % rec_index)
        raw = note[off:off + name_len]
        if b"\0" in raw:
            die("names entry %d ELF name contains an embedded NUL" % rec_index)
        name = raw.decode()
        off += name_len
        pad = (4 - ((8 + name_len) % 4)) % 4
        if off + pad > len(note):
            die("names entry %d truncated (padding)" % rec_index)
        if note[off:off + pad] != b"\0" * pad:
            die("names entry %d padding is not zero" % rec_index)
        off += pad
        entries.append((rec_index, name))
    if off != len(note):
        die("names table has %d trailing bytes after %d entries"
            % (len(note) - off, entry_count))
    return entries


def check_names(sections, records):
    """Cross-validate the association NOTE against the v1 table + .symtab."""
    entries = decode_names(sections)
    count = len(records)
    if len(entries) != count:
        die("names entry_count=%d but the v1 table has %d records"
            % (len(entries), count))
    indices = sorted(i for i, _ in entries)
    if indices != list(range(count)):
        die("names placement_record_index set %r is not exactly 0..%d"
            % (indices, count - 1))
    syms = read_symtab(sections)
    by_name = {}
    for sym in syms:
        if sym["name"]:
            by_name.setdefault(sym["name"], []).append(sym)
    # A `.mcu.fixed.<stable>` section is addressed by its section index; map
    # section index -> section name once.
    sec_name = {s["idx"]: s["name"] for s in sections}
    for idx, name in entries:
        stable, sc, ent, own, addr, size, align, flags = records[idx]
        matches = by_name.get(name)
        if not matches:
            die("names entry %d: ELF name %r is not in .symtab" % (idx, name))
        if own == 0:
            want_sec = ".mcu.fixed." + stable
            defined = [m for m in matches
                       if m["shndx"] != 0 and m["type"] in (1, 2)]
            if len(defined) != 1:
                die("names entry %d: owned name %r has %d defined "
                    "OBJECT/FUNC symbols (want exactly 1)"
                    % (idx, name, len(defined)))
            if sec_name.get(defined[0]["shndx"]) != want_sec:
                die("names entry %d: owned name %r lives in %r, not the "
                    "fixed section %r"
                    % (idx, name, sec_name.get(defined[0]["shndx"]), want_sec))
            # The fixed section must have that ONE principal symbol only.
            principal = [m for m in syms
                         if m["shndx"] == defined[0]["shndx"]
                         and m["type"] in (1, 2)]
            if len(principal) != 1 or principal[0]["name"] != name:
                die("names entry %d: %r is not the unique principal symbol "
                    "of %s" % (idx, name, want_sec))
        else:
            undef = [m for m in matches if m["shndx"] == 0]
            if len(undef) != 1:
                die("names entry %d: bind name %r has %d undefined symbols "
                    "(want exactly 1)" % (idx, name, len(undef)))
            if undef[0]["bind"] != 1:
                die("names entry %d: bind name %r is not STB_GLOBAL "
                    "(binding=%d)" % (idx, name, undef[0]["bind"]))
    print("check-placement-note: association %d/%d entries OK"
          % (len(entries), count))


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


def names_self_test(sections, records):
    """Negative control for the association decoder (--names-self-test).

    Mutates the REAL `.mcs251.placement.names` NOTE of an object that was just
    accepted and requires the very same cross-validation to reject each
    mutation: an out-of-range placement_record_index, an ELF name absent from
    .symtab, a wrong NOTE type and a wrong association_version.  A mutation the
    decoder still accepts means the oracle is not actually constraining that
    field.  Same discipline as check-tfpu-golden.py --self-test.
    """
    hits = [i for i, s in enumerate(sections)
            if s["name"] == ".mcs251.placement.names"]
    if len(hits) != 1:
        die("names self-test: no unique association NOTE to mutate")
    # The clean bytes must be accepted first (otherwise the mutations prove
    # nothing).
    check_names(sections, records)
    base = hits[0]

    def rejected(patch):
        copy = [dict(s) for s in sections]
        blob = bytearray(sections[base]["data"])
        patch(blob)
        copy[base]["data"] = bytes(blob)
        try:
            check_names(copy, records)
        except SystemExit:
            return True
        return False

    # (1) placement_record_index of the first entry -> out of range.
    def bad_index(d):
        d[28:32] = struct.pack(">I", len(records) + 1)

    if not rejected(bad_index):
        die("names self-test: out-of-range placement_record_index accepted")

    # (2) ELF name of the first entry -> a first byte absent from .symtab.
    def bad_name(d):
        nlen, = struct.unpack_from(">I", d, 32)
        if nlen == 0:
            die("names self-test: first association name is empty")
        d[36] = ord("Z") if d[36] != ord("Z") else ord("Q")

    if not rejected(bad_name):
        die("names self-test: an ELF name absent from .symtab was accepted")

    # (3) NOTE type -> 3 (no longer the association type).
    def bad_type(d):
        d[8:12] = struct.pack(">I", 3)

    if not rejected(bad_type):
        die("names self-test: a wrong association NOTE type was accepted")

    # (4) association_version -> 2.
    def bad_version(d):
        d[20:24] = struct.pack(">I", 2)

    if not rejected(bad_version):
        die("names self-test: a wrong association_version was accepted")

    print("check-placement-note: names self-test 4/4 negative injections "
          "rejected")


def main():
    args = sys.argv[1:]
    forbidden = set()
    names_self_test_mode = False
    while args and args[0].startswith("--"):
        if args[0].startswith("--no-init-for="):
            forbidden = {s for s in args[0][len("--no-init-for="):].split(",")
                         if s}
        elif args[0] == "--names-self-test":
            names_self_test_mode = True
        else:
            die("unknown option %r" % args[0])
        args = args[1:]
    if len(args) < 2 or (len(args) - 1) % 8:
        die("usage: check-placement-note.py [--no-init-for s1,s2] "
            "[--names-self-test] <object> "
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
    # G11-N4: every modern placement-bearing object carries the association
    # NOTE; this closes it against the v1 record table and .symtab.
    check_names(sections, records)
    if names_self_test_mode:
        names_self_test(sections, records)


if __name__ == "__main__":
    main()
