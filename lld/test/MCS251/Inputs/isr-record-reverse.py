#!/usr/bin/env python3
"""Permute the records of a `.mcs251.isr` section, and audit which bytes moved.

The file keeps its round-6 name (`isr-record-reverse.py`) so the existing call
site and the ledger entry stay valid; reversal is now one of several modes.

Round-6 review item (7) / round-7 items (2) and (4): the candidate evaluation
order of the reentrancy analysis must come from the slot/root iteration,
never from the order in which ISR registration records happen to sit in the
metadata section.  The A3.3 contract
(`validation/mcs251-models/proposals/ISR-TASK-BREAKDOWN.md`) states the
pairing rule as "the two records must reference the same exact function
symbol and the same slot" -- it does NOT require the ENTRY and REGISTER
records to be physically adjacent, and the linker pairs them BY SLOT and
EXACT SYMBOL (`LinkerCore.cpp`, `EntryBySlot`/`RegBySlot`).  So two shapes
have to be told apart:

  * a LEGAL physical reorder -- records moved within the section, each
    record's symbol association remapped along with it.  This must LINK
    SUCCESSFULLY regardless of where a record sits.
  * an IDENTITY MISMATCH -- records moved with their symbol associations
    left behind, so one slot's ENTRY and REGISTER end up naming different
    functions.  This must be REJECTED.

Modes
-----
  isr-record-reverse.py <in.o> <out.o>
      Reverse the section in units of the ENTRY+REGISTER pair and remap the
      association relocations (the original round-6 behaviour; kept so the
      existing call site in isr-reentrancy-addr.test is unchanged).

  isr-record-reverse.py --permute 0,2,4,1,3,5 [--no-remap] <in.o> <out.o>
      Reorder the 24-byte records to the given new order: new position `j`
      holds old record `order[j]`.  With remapping (the default) each moved
      record keeps its own symbol association -- a LEGAL reorder.  With
      `--no-remap` the associations stay where they were, so a permutation
      that separates a pair's two records makes the slot's ENTRY and
      REGISTER name different functions -- an IDENTITY MISMATCH.

  isr-record-reverse.py --audit <a.o> <b.o>
      Report every byte that differs between two objects together with the
      section it lies in, and FAIL (non-zero) if any difference falls
      OUTSIDE the two regions a record permutation is allowed to touch:
      the `.mcs251.isr` section body, and the `r_offset` field of the
      association relocations in `.rela.mcs251.isr`.  This is what makes
      "only the metadata section and its association offsets changed" an
      independently checked claim rather than an assumption.

  isr-record-reverse.py --set-all-slots N <in.o> <out.o>
      MUTATION helper for the negative regression: write slot `N' into every
      ENTRY and REGISTER record, leaving the symbol associations where they
      are, so the section ends up with several ENTRY (and several REGISTER)
      records on one slot.  That is a shape the linker refuses, and the
      point of the mode is to let a test assert that `validate()' refuses it
      too instead of silently collapsing the duplicate records.

Premises
--------
Every mode first validates the input object's premises explicitly instead of
assuming them: the section is a whole number of 24-byte records; each record
declares `record_size == 24`, a known `record_kind` and a non-`FFFF` slot;
the records form, for each slot, EXACTLY one ENTRY and EXACTLY one REGISTER
-- a slot with two ENTRY records (or two REGISTER records) is refused rather
than silently collapsed, because the linker refuses that shape too -- whose
symbol associations name the SAME symbol index; and every relocation in the
RELA section that targets `.mcs251.isr` is an `R_MCS251_ISR_REF` (type 9)
sitting at `record base + 12` with a unique offset, one per record.

Nothing else is touched: the code sections, the symbol table, the string
table, the section headers and every other section stay byte-identical, so
the only variable is the metadata record order.
"""
import struct
import sys

EHDR_SIZE = 52  # ELF32
SHDR_SIZE = 40  # ELF32
SHT_RELA = 4
ISR_RECORD_SIZE = 24
ASSOC_FIELD = 12  # RecordOffset::SymbolReference
R_MCS251_ISR_REF = 9
RK_ISR_ENTRY = 1
RK_ISR_REGISTER = 2
RK_IRQ_DEFAULT = 3
RK_IRQ_RESET = 4
KNOWN_KINDS = (RK_ISR_ENTRY, RK_ISR_REGISTER, RK_IRQ_DEFAULT, RK_IRQ_RESET)


def fail(msg):
    sys.stderr.write("isr-record-reverse: %s\n" % msg)
    sys.exit(1)


def parse_sections(data):
    """Return (headers, name_of_section, index_by_name) for an ELF32 file."""
    if len(data) < EHDR_SIZE:
        fail("file is shorter than an ELF32 header")
    if data[:4] != b"\x7fELF":
        fail("not an ELF file")
    if data[4] != 1:
        fail("not ELFCLASS32")
    if data[5] != 2:
        fail("not ELFDATA2MSB; the MCS251 object format is big-endian")
    e_shoff = struct.unpack_from(">I", data, 32)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 46)
    if e_shentsize != SHDR_SIZE:
        fail("unexpected e_shentsize %d" % e_shentsize)
    headers = [struct.unpack_from(">10I", data, e_shoff + i * e_shentsize)
               for i in range(e_shnum)]
    sh = headers[e_shstrndx]
    names = data[sh[4]:sh[4] + sh[5]]

    def name(off):
        return bytes(names[off:]).split(b"\0")[0].decode()

    return headers, name, {name(h[0]): i for i, h in enumerate(headers)}


def isr_section(data):
    headers, name, index = parse_sections(data)
    if ".mcs251.isr" not in index:
        fail("no .mcs251.isr section")
    i = index[".mcs251.isr"]
    h = headers[i]
    if h[1] != 1:  # SHT_PROGBITS
        fail("`.mcs251.isr` is not SHT_PROGBITS")
    off, size = h[4], h[5]
    return headers, name, index, i, off, size


def association_relocs(data, headers, name, isr_i):
    """(rela_section_index, byte_offset_of_the_r_offset_field, value) triples."""
    out = []
    for i, h in enumerate(headers):
        if h[1] != SHT_RELA or h[7] != isr_i:
            continue
        entsize = h[9] or 12
        for off in range(h[4], h[4] + h[5], entsize):
            r_offset, r_info = struct.unpack_from(">II", data, off)
            out.append((i, off, r_offset, r_info))
    return out


def validate(data):
    """Validate the input premises explicitly; return the parsed layout."""
    headers, name, index, isr_i, off, size = isr_section(data)
    if size == 0 or size % ISR_RECORD_SIZE != 0:
        fail(".mcs251.isr is not a whole number of 24-byte records "
             "(size %d)" % size)
    n = size // ISR_RECORD_SIZE

    # ---- per-record field premises -------------------------------------
    slots = {}
    for i in range(n):
        r = data[off + i * ISR_RECORD_SIZE:off + (i + 1) * ISR_RECORD_SIZE]
        proto, rsize = struct.unpack_from(">HH", r, 0)
        kind, slot = r[4], struct.unpack_from(">H", r, 8)[0]
        caps = struct.unpack_from(">H", r, 10)[0]
        if rsize != ISR_RECORD_SIZE:
            fail("record %d declares record_size=%d (expected 24)" % (i, rsize))
        if kind not in KNOWN_KINDS:
            fail("record %d has unknown record_kind=%d" % (i, kind))
        if proto == 0:
            fail("record %d has protocol_version=0" % i)
        if caps != 1:
            fail("record %d has required_caps=0x%04x (expected 0001)"
                 % (i, caps))
        if kind in (RK_ISR_ENTRY, RK_ISR_REGISTER):
            if slot == 0xFFFF:
                fail("record %d is kind %d with the non-slot sentinel slot "
                     "FFFF" % (i, kind))
            # A plain dict assignment would silently OVERWRITE an earlier
            # record of the same (slot, kind), which would make the
            # one-ENTRY-one-REGISTER-per-slot check below vacuous for a
            # section that carries two ENTRY records for one slot.  The
            # linker refuses that shape (MCS251 ISR: two ENTRY records for
            # slot N), so the fixture must refuse it too rather than accept
            # it and hand on an object the linker will reject.
            prev = slots.setdefault(slot, {}).get(kind)
            if prev is not None:
                fail("slot %d has more than one %s record (records %d and "
                     "%d)" % (slot,
                              "ENTRY" if kind == RK_ISR_ENTRY else "REGISTER",
                              prev, i))
            slots[slot][kind] = i

    # ---- exactly one ENTRY and exactly one REGISTER per slot ------------
    for slot, kinds in sorted(slots.items()):
        if RK_ISR_ENTRY not in kinds:
            fail("slot %d has a REGISTER but no ENTRY" % slot)
        if RK_ISR_REGISTER not in kinds:
            fail("slot %d has an ENTRY but no REGISTER" % slot)
        extra = sorted(set(kinds) - {RK_ISR_ENTRY, RK_ISR_REGISTER})
        if extra:
            fail("slot %d carries unexpected record kind(s) %s"
                 % (slot, extra))

    # ---- association relocations: one R_MCS251_ISR_REF per record at
    #      record base + 12, all offsets unique -------------------------
    relocs = association_relocs(data, headers, name, isr_i)
    record_of = {}
    for sec_i, roff_field, r_offset, r_info in relocs:
        r_type = r_info & 0xFF
        if r_type != R_MCS251_ISR_REF:
            fail("relocation in %s has type %d (expected R_MCS251_ISR_REF=%d)"
                 % (name(headers[sec_i][0]), r_type, R_MCS251_ISR_REF))
        if r_offset % ISR_RECORD_SIZE != ASSOC_FIELD:
            fail("association relocation at 0x%x is not at record base + %d"
                 % (r_offset, ASSOC_FIELD))
        rec = r_offset // ISR_RECORD_SIZE
        if rec >= n:
            fail("association relocation 0x%x is past the last record"
                 % r_offset)
        if rec in record_of:
            fail("two association relocations target record %d" % rec)
        record_of[rec] = r_info >> 8
    if len(record_of) != n:
        fail("expected one association relocation per record: got %d for %d "
             "records" % (len(record_of), n))

    # ---- the pair's two records must name the same symbol -------------
    for slot, kinds in sorted(slots.items()):
        a = record_of[kinds[RK_ISR_ENTRY]]
        b = record_of[kinds[RK_ISR_REGISTER]]
        if a != b:
            fail("slot %d: the ENTRY record names symbol index %d and the "
                 "REGISTER record names %d; the input is not a valid "
                 "ENTRY+REGISTER pair" % (slot, a, b))
    return {
        "headers": headers, "name": name, "index": index, "isr_i": isr_i,
        "off": off, "size": size, "records": n,
        "relocs": relocs, "record_symbol": record_of,
    }


def remap(data, layout, new_index_of_old):
    """Point each record's association relocation at its record's new place."""
    n = layout["records"]
    touched = []
    for sec_i, roff_field, r_offset, r_info in layout["relocs"]:
        rec = r_offset // ISR_RECORD_SIZE
        new_rec = new_index_of_old[rec]
        new_offset = new_rec * ISR_RECORD_SIZE + ASSOC_FIELD
        struct.pack_into(">I", data, roff_field, new_offset)
        touched.append((sec_i, roff_field, r_offset, new_offset))
    if len(touched) != n:
        fail("remapped %d relocations for %d records" % (len(touched), n))
    return touched


def apply_order(data, layout, new_index_of_old):
    off, size = layout["off"], layout["size"]
    original = bytes(data[off:off + size])
    n = layout["records"]
    body = bytearray(size)
    for new_rec in range(n):
        old_rec = new_index_of_old.index(new_rec)
        src = original[old_rec * ISR_RECORD_SIZE:(old_rec + 1) * ISR_RECORD_SIZE]
        body[new_rec * ISR_RECORD_SIZE:(new_rec + 1) * ISR_RECORD_SIZE] = src
    data[off:off + size] = bytes(body)


def audit(a_path, b_path):
    a = open(a_path, "rb").read()
    b = open(b_path, "rb").read()
    if len(a) != len(b):
        fail("objects differ in size: %d vs %d" % (len(a), len(b)))
    la = validate(a)
    # The second object must ALSO be structurally valid with the same section
    # layout, so that the allowed regions computed from `a` are the right
    # regions for `b` too.  Without this the audit would compare against
    # ranges derived from a file whose layout `b` might not share.
    lb = validate(b)
    if (la["off"], la["size"]) != (lb["off"], lb["size"]):
        fail("the two objects disagree on the `.mcs251.isr` section layout")
    if len(la["relocs"]) != len(lb["relocs"]):
        fail("the two objects disagree on the number of association relocations")
    # The two allowed regions.
    allowed = []
    allowed.append(("isr_body", la["off"], la["off"] + la["size"]))
    for sec_i, roff_field, _r_off, _r_info in la["relocs"]:
        allowed.append(("rela_r_offset", roff_field, roff_field + 4))

    def region_of(i):
        for nm, lo, hi in allowed:
            if lo <= i < hi:
                return nm
        return None

    diff = [(i, region_of(i)) for i, (x, y) in enumerate(zip(a, b)) if x != y]
    outside = [d for d in diff if d[1] is None]
    sys.stdout.write("AUDIT total_bytes_differ=%d\n" % len(diff))
    # One line per DISTINCT region, listing that region's own differing bytes.
    seen = set()
    for i, rn in diff:
        if rn is None or rn in seen:
            continue
        seen.add(rn)
        hits = [j for j, r in diff if r == rn]
        sys.stdout.write("AUDIT region %s differing_bytes=%d offs=%s\n"
                         % (rn, len(hits),
                            ",".join("0x%x" % j for j in sorted(hits))))
    # For the association offsets, also report the FIELD-INTERNAL byte index,
    # so "which byte of r_offset moved" is checkable rather than assumed.
    field_bytes = sorted(set((i - next(f for _s, f, _o, _i
                                      in la["relocs"]
                                      if f <= i < f + 4)) % 4
                             for i, r in diff if r == "rela_r_offset"))
    sys.stdout.write("AUDIT rela_r_offset field_internal_bytes=%s\n"
                     % ",".join(str(x) for x in field_bytes))
    sys.stdout.write("AUDIT outside_allowed=%d\n" % len(outside))
    if outside:
        for i, _ in outside:
            sys.stdout.write("AUDIT outside byte 0x%x\n" % i)
        fail("differences fall outside the allowed regions")
    return 0


def set_all_slots(src, slot, dst):
    """Write `slot' into every ENTRY/REGISTER record of a copy of `src'."""
    data = bytearray(open(src, "rb").read())
    headers, name, index, isr_i, off, size = isr_section(data)
    if size == 0 or size % ISR_RECORD_SIZE != 0:
        fail(".mcs251.isr is not a whole number of 24-byte records (size %d)"
             % size)
    n = size // ISR_RECORD_SIZE
    for i in range(n):
        r = off + i * ISR_RECORD_SIZE
        kind = data[r + 4]
        if kind in (RK_ISR_ENTRY, RK_ISR_REGISTER):
            struct.pack_into(">H", data, r + 8, slot)
    open(dst, "wb").write(bytes(data))
    return 0


def main():
    argv = sys.argv[1:]
    order = None
    do_remap = True
    if argv and argv[0] == "--audit":
        if len(argv) != 3:
            fail("usage: isr-record-reverse.py --audit <a.o> <b.o>")
        return audit(argv[1], argv[2])
    if argv and argv[0] == "--set-all-slots":
        if len(argv) != 4:
            fail("usage: isr-record-reverse.py --set-all-slots N <in.o> "
                 "<out.o>")
        return set_all_slots(argv[2], int(argv[1]), argv[3])
    if argv and argv[0] == "--permute":
        if len(argv) < 2:
            fail("usage: --permute i,j,k,... <in.o> <out.o>")
        order = [int(x) for x in argv[1].split(",") if x != ""]
        argv = argv[2:]
    if argv and argv[0] == "--no-remap":
        do_remap = False
        argv = argv[1:]
    if len(argv) != 2:
        fail("usage: isr-record-reverse.py [--permute i,j,..] [--no-remap] "
             "<in.o> <out.o>")
    src, dst = argv

    data = bytearray(open(src, "rb").read())
    layout = validate(bytes(data))
    n = layout["records"]
    if order is None:
        # Original behaviour: reverse in units of the ENTRY+REGISTER pair.
        if layout["size"] % (2 * ISR_RECORD_SIZE) != 0:
            fail(".mcs251.isr does not hold whole ENTRY+REGISTER pairs")
        pairs = n // 2
        if pairs < 2:
            fail("need at least two pairs to reverse")
        order = [((pairs - 1 - p) * 2 + w)
                 for p in range(pairs) for w in (0, 1)]
    if sorted(order) != list(range(n)):
        fail("--permute must be a permutation of 0..%d, got %s" % (n - 1, order))

    # new position `j` holds old record `order[j]`; invert it to find where
    # each OLD record went, which is what the remap needs.
    new_index_of_old = [order.index(i) for i in range(n)]
    apply_order(data, layout, new_index_of_old)
    if do_remap:
        remap(data, layout, new_index_of_old)
    open(dst, "wb").write(bytes(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
