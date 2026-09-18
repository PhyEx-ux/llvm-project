#!/usr/bin/env python3
"""G11-D audit-test helper: mutate a SUCCESSFUL artifact set (final ELF,
placement report, input object, link map) so the independent verifier can be
exercised directly on the damaged artifact.

Contract §5 requires the negative cases to mutate a set that already verified
and then run the VERIFIER, not the linker: re-linking would let the producer
refuse the mutation first and hide the verifier's own detection power.

Usage: audit-mutate.py <command> <in> <out> [args...]
Commands (all offsets are discovered from the ELF/section tables, never
hard-coded to one fixture):
  elf-stvalue   <in> <out> <sym> <hex>      set st_value
  elf-size      <in> <out> <sym> <hex>      set st_size
  elf-syminfo   <in> <out> <sym> <i> <hex>  set st_info (bind<<4|type) of the
                                            i-th (0-based) row named <sym>
  elf-symshndx  <in> <out> <sym> <i> <dec>  set st_shndx (e.g. 65521 = ABS)
  elf-rename    <in> <out> <sym> <newname>  repoint st_name at a NEW strtab entry
  elf-codebyte  <in> <out> <addr-hex>       flip the load-image byte at addr
  elf-poke      <in> <out> <addr-hex> <hex> write raw bytes at a final address
  elf-phnum     <in> <out> <hex>            set e_phnum
  elf-secnum    <in> <out> <total>          grow the section header table to
                                            <total> headers by appending real
                                            non-ALLOC, zero-size NOBITS headers
                                            (table relocated to EOF; used to
                                            probe whether a reserved st_shndx
                                            gets re-interpreted as an ordinary
                                            section index)
  elf-undefined <in> <out> <sym>            make a symbol undefined NOTYPE
  elf-dup-sym   <in> <out> <name>           append an exact duplicate of the
                                            first symbol row named <name>
  elf-add-phdr  <in> <out> <type> <offset> <vaddr> <filesz> <memsz>
                                            append one program header (a copy
                                            of entry 0 with those fields)
  elf-add-phdr-carrier <in> <out> <type> <delta> <vaddr> <memsz>
                                            append a filesz=0 program header
                                            whose p_offset is delta bytes into
                                            the positioning carrier's file
                                            range (G11-D2 R8: the carrier never
                                            intersects a non-empty Foff)
  sec-addr      <in> <out> <section> <hex>  final section sh_addr (B-MAPPING)
  sec-type      <in> <out> <section> <hex>  input section sh_type
  sec-align     <in> <out> <section> <hex>  input section sh_addralign
  rela-type     <in> <out> <out-section> <index> <hex>
                                            set one RELA entry's r_type
  rela-offset   <in> <out> <out-section> <index> <hex>
                                            set one RELA entry's r_offset
  rela-addend   <in> <out> <out-section> <index> <hex>
                                            set one RELA entry's r_addend
  report-raw    <in> <out> <stable> <col> <literal>
                                            set a report column WITHOUT
                                            recomputing the row hash (the
                                            overflow cases must fail on the
                                            range check, not on a stale hash)
  report-dup    <in> <out> <stable> <col> <literal>
                                            duplicate a row with one changed
                                            column (hash recomputed), keeping
                                            the (file, sym, stable) triple
  map-section   <in> <out> <file:section> <hex>
                                            rewrite one `file:section 0x..`
                                            address row
  note-addr     <in> <out> <stable> <hex>   NOTE address, record hash recomputed
  note-align    <in> <out> <stable> <hex>   NOTE align, record hash recomputed
  note-namesz   <in> <out> <hex>            envelope namesz
  note-pad      <in> <out> <stable>         last padding byte of one record
  note-truncate <in> <out>                  shrink sh_size/descsz by one byte
  note-clear    <in> <out>                  empty the record table (envelope only)
  note-flip-hash<in> <out> <stable>         one bit of the stored H_source
  sec-flags     <in> <out> <section> <hex>  input section sh_flags
  report-field  <in> <out> <stable> <field> <value>
                                            field in class/entity/ownership/
                                            align/flags/size/bound_only/address;
                                            layout_hash is RECOMPUTED so a
                                            field mismatch cannot be masked by
                                            (or confused with) a hash failure
  report-flip-hash <in> <out> <stable>      one bit of a stored H_report
  report-drop   <in> <out> <stable>         delete one row
  report-add    <in> <out>                  append a row with no source
  report-reverse<in> <out>                  reverse the row order
  map-bound     <in> <out> <sym> <hex>      rewrite one `sym = 0x...` boundary row
  map-bound-rename <in> <out> <src> <dst>   rename one boundary row, value kept
  elf-symrename <in> <out> <sym> <newname>  rename one output symbol
  elf-add-carrier-ptnote <in> <out>         append a PT_NOTE over the carrier
  carrier-digest <elf-in> <elf-out> <ordinal> <object>
                                            recompute the positioning carrier's
                                            fingerprint of <ordinal> from the
                                            CURRENT bytes of <object> (G11-D2:
                                            a case that must reach a later check
                                            after mutating an input object keeps
                                            the carrier's digest consistent)
  carrier-addr  <elf-in> <elf-out> <ordinal> <object> <section> <hex>
                                            also move that section's record to
                                            <hex> (single-slice form)
  names-poke    <in> <out> <off> <hexbytes> write bytes at a section-relative
                                            offset of `.mcs251.placement.names`
  names-size    <in> <out> <delta>          grow/shrink the names section SH_SIZE
  names-null-carrier <in> <out> <type> <flags> <align>
                                            park the exact names carrier on
                                            section header index 0 (the ELF
                                            null header), retiring the
                                            original name as `.comment`
  sec-alias     <in> <out> <src> <dst>      rename section <dst> to <src>'s name
  print-notes   <object>                    dump records (debug)

G11-D2 positioning-carrier structural variants (design section 11.2); every
mode is a fixed-point byte change on the frozen section-3 layout:
  pos-note      <in> <out> <field> <val>    envelope/descriptor u32:
                                           namesz descsz type version
                                           objects records reserved
  pos-name      <in> <out> <idx> <byte>     poke byte 12..19 (name + pad)
  pos-sec       <in> <out> <field> <val>    carrier section header:
                                           name type flags addr offset size
                                           link info align entsize
  pos-digest    <in> <out> <idx> <off> <b>  poke one fingerprint byte
  pos-record    <in> <out> field <i> <f> <v>  record u32/u8 field:
                                           size object shndx space8 res8
                                           res16 input slices
  pos-record    <in> <out> swap|dup|drop <i> [j]  reorder/duplicate/drop
  pos-record    <in> <out> append <i>       append a copy of record i
  pos-slice     <in> <out> <rec> <k> <f> <v>  slice field offset addr length
  pos-slice-op  <in> <out> <rec> drop|dup <k>  remove/duplicate a slice
  pos-trunc     <in> <out> <n>              cut n bytes off the carrier tail
  pos-move      <in> <out> <tailhex|->      relocate carrier to EOF (+tail)
  pos-dup-sec   <in> <out>                  second carrier section header
"""
import hashlib
import struct
import sys


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def p32(b, o, v):
    struct.pack_into('>I', b, o, v)


def sections(b):
    shoff = u32(b, 32)
    shent = struct.unpack_from('>H', b, 46)[0]
    shnum = struct.unpack_from('>H', b, 48)[0]
    shstr = struct.unpack_from('>H', b, 50)[0]
    out = []
    for i in range(shnum):
        o = shoff + i * shent
        out.append(dict(i=i, nameoff=u32(b, o), type=u32(b, o + 4),
                        flags=u32(b, o + 8), addr=u32(b, o + 12),
                        off=u32(b, o + 16), size=u32(b, o + 20),
                        link=u32(b, o + 24), info=u32(b, o + 28)))
    so = out[shstr]

    def nm(x):
        s = b[so['off'] + x['nameoff']:so['off'] + so['size']]
        return s.split(b'\0')[0].decode()

    for s in out:
        s['name'] = nm(s)
        s['hdroff'] = shoff + s['i'] * shent
    return out


def find_sec(b, name):
    for s in sections(b):
        if s['name'] == name:
            return s
    raise SystemExit('no section ' + name)


def syms(b):
    st = find_sec(b, '.symtab')
    strt = sections(b)[st['link']]

    def nm(off):
        e = b.find(b'\0', strt['off'] + off)
        return b[strt['off'] + off:e].decode()

    out = []
    for i in range(st['size'] // 16):
        o = st['off'] + i * 16
        out.append(dict(i=i, off=o, name=nm(u32(b, o)), value=u32(b, o + 4),
                        size=u32(b, o + 8), info=b[o + 12],
                        shndx=struct.unpack_from('>H', b, o + 14)[0]))
    return out


def sym(b, name):
    hit = [s for s in syms(b) if s['name'] == name]
    if not hit:
        raise SystemExit('no symbol ' + name)
    return hit[0]


def image_off(b, addr):
    """File offset of a final load-image address (via the PT_LOAD table)."""
    phoff = u32(b, 28)
    phnum = struct.unpack_from('>H', b, 44)[0]
    for i in range(phnum):
        o = phoff + i * 32
        vaddr = u32(b, o + 8)
        if u32(b, o) == 1 and vaddr <= addr < vaddr + u32(b, o + 16):
            return u32(b, o + 4) + (addr - vaddr)
    raise SystemExit('address not in a loadable segment: %#x' % addr)


def layout_hash(cls, ent, own, a, al, fl):
    return hashlib.sha256(bytes([1, cls, ent, own]) +
                          struct.pack('>III', a, al, fl)).digest()[28:32]


def notes(b):
    sec = find_sec(b, '.mcs251.placement')
    o = sec['off'] + 20
    end = sec['off'] + sec['size']
    i = 0
    while o < end:
        rsz = u32(b, o)
        yield dict(idx=i, base=o, recsize=rsz, ver=b[o + 4], cls=b[o + 5],
                   ent=b[o + 6], own=b[o + 7], addr=u32(b, o + 8),
                   size=u32(b, o + 12), align=u32(b, o + 16),
                   flags=u32(b, o + 20), hash=u32(b, o + 24), slen=b[o + 28],
                   stable=b[o + 29:o + 29 + b[o + 28]].decode())
        o += 4 + rsz
        i += 1


def note(b, stable):
    for r in notes(b):
        if r['stable'] == stable:
            return r
    raise SystemExit('no NOTE record ' + stable)


def rows(text):
    return [l for l in text.split('\n') if l]


def main():
    cmd = sys.argv[1]
    src = sys.argv[2]
    if cmd == 'print-notes':
        for r in notes(bytearray(open(src, 'rb').read())):
            print(r)
        return
    dst = sys.argv[3]

    if src.endswith('.placement') or 'report' in cmd or 'map' in cmd:
        data = open(src, 'rb').read()
    else:
        data = open(src, 'rb').read()

    if cmd == 'elf-stvalue':
        b = bytearray(data)
        p32(b, sym(b, sys.argv[4])['off'] + 4, int(sys.argv[5], 16))
    elif cmd == 'elf-size':
        b = bytearray(data)
        p32(b, sym(b, sys.argv[4])['off'] + 8, int(sys.argv[5], 16))
    elif cmd in ('elf-syminfo', 'elf-symshndx'):
        # G11-D2 rule-matrix negatives: change the retained output HEADER of
        # the i-th (0-based) row named <sym>, leaving value/size/shndx alone.
        b = bytearray(data)
        sel = [s for s in syms(b) if s['name'] == sys.argv[4]]
        i = int(sys.argv[5], 0)
        if i >= len(sel):
            raise SystemExit('no symbol row %d named %s' % (i, sys.argv[4]))
        if cmd == 'elf-syminfo':
            b[sel[i]['off'] + 12] = int(sys.argv[6], 0)
        else:
            struct.pack_into('>H', b, sel[i]['off'] + 14, int(sys.argv[6], 0))
    elif cmd == 'elf-rename':
        # Point st_name past the end of the linked .strtab: the symbol keeps its
        # value/size/shndx but loses its name, which is what "the entity's
        # output symbol was removed" looks like to the verifier.
        b = bytearray(data)
        st = find_sec(b, '.symtab')
        strt = sections(b)[st['link']]
        p32(b, sym(b, sys.argv[4])['off'], strt['size'] + 1)
    elif cmd == 'elf-poke':
        b = bytearray(data)
        off = image_off(b, int(sys.argv[4], 16))
        raw = bytes.fromhex(sys.argv[5])
        b[off:off + len(raw)] = raw
    elif cmd == 'elf-phnum':
        b = bytearray(data)
        struct.pack_into('>H', b, 44, int(sys.argv[4], 0))
    elif cmd == 'elf-secnum':
        # G11-D2 R9-1 fixture support: enlarge the section header table to
        # <total> headers with real, non-ALLOC, zero-size SHT_NOBITS headers
        # (sh_name 0, sh_addr/sh_offset/sh_size 0, sh_addralign 1).  The whole
        # table is relocated to EOF (contiguous) so every existing header keeps
        # its words; the physical section count is what changes, not any symbol
        # field.  This isolates "does a reserved st_shndx become an ordinary
        # section definition once the table grows past it?".
        b = bytearray(data)
        shoff = u32(b, 32)
        shent = struct.unpack_from('>H', b, 46)[0]
        shnum = struct.unpack_from('>H', b, 48)[0]
        total = int(sys.argv[4], 0)
        if shent != 40:
            raise SystemExit('unsupported sh_entsize %d' % shent)
        if total < shnum:
            raise SystemExit('elf-secnum only grows (%d -> %d)' % (shnum, total))
        table = bytes(b[shoff:shoff + shent * shnum])
        newsh = (len(b) + 3) & ~3
        b += b'\0' * (newsh - len(b))
        b += table
        extra = struct.pack('>10I', 0, 8, 0, 0, 0, 0, 0, 0, 1, 0)
        b += extra * (total - shnum)
        p32(b, 32, newsh)
        struct.pack_into('>H', b, 48, total)
    elif cmd == 'elf-phdr':
        # Fixed-point program-header edit: entry index, one u32 field.
        b = bytearray(data)
        i, field, val = int(sys.argv[4], 0), sys.argv[5], \
            int(sys.argv[6], 0)
        phoff = u32(b, 28)
        slot = {'type': 0, 'offset': 4, 'vaddr': 8, 'paddr': 12,
                'filesz': 16, 'memsz': 20, 'flags': 24, 'align': 28}[field]
        p32(b, phoff + i * 32 + slot, val)
    elif cmd == 'elf-undefined':
        b = bytearray(data)
        s = sym(b, sys.argv[4])
        b[s['off'] + 12] = 0x10        # STB_GLOBAL | STT_NOTYPE
        b[s['off'] + 13] = 0
        struct.pack_into('>H', b, s['off'] + 14, 0)   # SHN_UNDEF
    elif cmd == 'elf-dup-sym':
        # G11-D2 R5-2 fixture support: append an EXACT duplicate of the first
        # symbol row named <name>, so the final table carries two symbols with
        # the same name and value.  The whole symbol table is relocated to EOF
        # (its rows are contiguous) with one extra row; sh_link/sh_info stay.
        b = bytearray(data)
        st = find_sec(b, '.symtab')
        symrows = syms(b)
        hit = [s for s in symrows if s['name'] == sys.argv[4]]
        if not hit:
            raise SystemExit('no symbol ' + sys.argv[4])
        src = hit[0]
        new = len(b)
        b.extend(b[st['off']:st['off'] + st['size']])
        b.extend(b[src['off']:src['off'] + 16])
        p32(b, st['hdroff'] + 16, new)
        p32(b, st['hdroff'] + 20, st['size'] + 16)
    elif cmd == 'elf-add-carrier-ptnote':
        # G11-D2 R5-9 fixture support: append a PT_NOTE that points at the
        # positioning carrier's file range.  The producer never emits PT_NOTE
        # (design section 3.1); this constructs exactly that extra segment.
        b = bytearray(data)
        carrier = find_sec(b, '.mcs251.placement.positions')
        phoff = u32(b, 28)
        phnum = struct.unpack_from('>H', b, 44)[0]
        new = len(b)
        b.extend(b[phoff:phoff + 32 * phnum])
        b.extend(bytes(32))
        row = new + 32 * phnum
        p32(b, row, 4)                      # PT_NOTE
        p32(b, row + 4, carrier['off'])     # p_offset
        p32(b, row + 8, 0)                  # p_vaddr
        p32(b, row + 16, carrier['size'])   # p_filesz
        p32(b, row + 20, carrier['size'])   # p_memsz
        p32(b, 28, new)
        struct.pack_into('>H', b, 44, phnum + 1)
    elif cmd == 'elf-symrename':
        # Rename one output symbol: relocate `.strtab` to EOF with the old
        # content plus the new name appended (every existing st_name offset
        # stays valid because the content prefix is copied verbatim) and point
        # the symbol at the new entry.  Used to construct a boundary pair
        # under a DIFFERENT area name on a link that never had that area,
        # without touching the value.
        b = bytearray(data)
        st = find_sec(b, '.symtab')
        strt = sections(b)[st['link']]
        s = sym(b, sys.argv[4])
        newname = sys.argv[5].encode()
        new = len(b)
        b.extend(b[strt['off']:strt['off'] + strt['size']])
        b.extend(newname + b'\0')
        p32(b, strt['hdroff'] + 16, new)
        p32(b, strt['hdroff'] + 20, strt['size'] + len(newname) + 1)
        p32(b, s['off'], strt['size'])
    elif cmd == 'elf-add-phdr':
        # G11-D2 R5-9 fixture support: append a copy of program header 0 with
        # the given type/offset/vaddr/filesz/memsz, point e_phoff at the copy
        # and grow e_phnum.  The producer never emits PT_NOTE; this is the
        # "an extra segment points at the carrier" shape.
        b = bytearray(data)
        phoff = u32(b, 28)
        phnum = struct.unpack_from('>H', b, 44)[0]
        new = len(b)
        b.extend(b[phoff:phoff + 32 * phnum])
        b.extend(bytes(32))
        row = new + 32 * phnum
        for slot, v in zip((0, 4, 8, 16, 20), sys.argv[4:9]):
            p32(b, row + slot, int(v, 0))
        p32(b, 28, new)
        struct.pack_into('>H', b, 44, phnum + 1)
    elif cmd == 'elf-add-phdr-carrier':
        # R8 fixture support: append a filesz=0 PT_LOAD whose file offset lies
        # INSIDE the positioning carrier's range.  A zero-filesz segment is an
        # EMPTY file interval, so this must be accepted (design section 9.1)
        # even though a non-empty segment there would be B-CARRIER.
        b = bytearray(data)
        carrier = find_sec(b, '.mcs251.placement.positions')
        phoff = u32(b, 28)
        phnum = struct.unpack_from('>H', b, 44)[0]
        new = len(b)
        b.extend(b[phoff:phoff + 32 * phnum])
        b.extend(bytes(32))
        row = new + 32 * phnum
        p32(b, row, int(sys.argv[4], 0))            # p_type
        p32(b, row + 4, carrier['off'] + int(sys.argv[5], 0))  # p_offset
        p32(b, row + 8, int(sys.argv[6], 0))        # p_vaddr
        p32(b, row + 16, 0)                         # p_filesz
        p32(b, row + 20, int(sys.argv[7], 0))       # p_memsz
        p32(b, 28, new)
        struct.pack_into('>H', b, 44, phnum + 1)
    elif cmd == 'elf-codebyte':
        b = bytearray(data)
        addr = int(sys.argv[4], 16)
        phoff = u32(b, 28)
        phnum = struct.unpack_from('>H', b, 44)[0]
        for i in range(phnum):
            o = phoff + i * 32
            if u32(b, o) == 1 and u32(b, o + 8) <= addr < u32(b, o + 8) + u32(b, o + 16):
                b[u32(b, o + 4) + (addr - u32(b, o + 8))] ^= 0xff
                break
        else:
            raise SystemExit('address not in a loadable segment')
    elif cmd in ('note-addr', 'note-align'):
        b = bytearray(data)
        r = note(b, sys.argv[4])
        a = int(sys.argv[5], 16)
        if cmd == 'note-addr':
            p32(b, r['base'] + 8, a)
        else:
            p32(b, r['base'] + 16, a)
        p32(b, r['base'] + 24, struct.unpack('>I', layout_hash(
            r['cls'], r['ent'], r['own'], a if cmd == 'note-addr' else r['addr'],
            a if cmd == 'note-align' else r['align'], r['flags']))[0])
    elif cmd == 'note-namesz':
        b = bytearray(data)
        p32(b, find_sec(b, '.mcs251.placement')['off'], int(sys.argv[4], 16))
    elif cmd == 'note-pad':
        b = bytearray(data)
        r = note(b, sys.argv[4])
        last = r['base'] + 4 + r['recsize'] - 1
        if last < r['base'] + 4 + 25 + r['slen']:
            raise SystemExit('record has no padding')
        b[last] = 0x41
    elif cmd == 'note-truncate':
        b = bytearray(data)
        s = find_sec(b, '.mcs251.placement')
        p32(b, s['hdroff'] + 20, s['size'] - 1)
        p32(b, s['off'] + 4, s['size'] - 1 - 20)
    elif cmd == 'note-clear':
        b = bytearray(data)
        s = find_sec(b, '.mcs251.placement')
        p32(b, s['hdroff'] + 20, 20)   # keep the 20-byte envelope
        p32(b, s['off'] + 4, 0)        # descsz = 0
    elif cmd == 'note-flip-hash':
        b = bytearray(data)
        r = note(b, sys.argv[4])
        b[r['base'] + 24] ^= 0x01
    elif cmd == 'sec-flags':
        b = bytearray(data)
        p32(b, find_sec(b, sys.argv[4])['hdroff'] + 8, int(sys.argv[5], 16))
    elif cmd == 'sec-addr':
        b = bytearray(data)
        p32(b, find_sec(b, sys.argv[4])['hdroff'] + 12, int(sys.argv[5], 16))
    elif cmd == 'sec-type':
        b = bytearray(data)
        p32(b, find_sec(b, sys.argv[4])['hdroff'] + 4, int(sys.argv[5], 16))
    elif cmd == 'sec-align':
        b = bytearray(data)
        p32(b, find_sec(b, sys.argv[4])['hdroff'] + 32, int(sys.argv[5], 16))
    elif cmd == 'sec-alias':
        # G11-C2 fixture support: point <dst>'s section header at the SAME
        # sh_name offset as <src>, so the object carries two sections with the
        # exact same name (yaml2obj itself refuses duplicate names).  Used to
        # exercise the "at most one carrier per object" rule.
        b = bytearray(data)
        src = find_sec(b, sys.argv[4])
        dstc = find_sec(b, sys.argv[5])
        p32(b, dstc['hdroff'], src['nameoff'])
    elif cmd == 'rela-offset':
        b = bytearray(data)
        target = find_sec(b, sys.argv[4])['i']
        for s in sections(b):
            if s['type'] == 4 and s['info'] == target:
                off = s['off'] + int(sys.argv[5], 0) * 12
                p32(b, off, int(sys.argv[6], 16))
                break
        else:
            raise SystemExit('no RELA for ' + sys.argv[4])
    elif cmd == 'rela-addend':
        b = bytearray(data)
        target = find_sec(b, sys.argv[4])['i']
        for s in sections(b):
            if s['type'] == 4 and s['info'] == target:
                off = s['off'] + int(sys.argv[5], 0) * 12
                p32(b, off + 8, int(sys.argv[6], 16))
                break
        else:
            raise SystemExit('no RELA for ' + sys.argv[4])
    elif cmd == 'rela-type':
        # Locate the RELA whose sh_info names <out-section>, then rewrite the
        # r_type byte of one entry.  The byte layout of r_info is
        # `type & 0xff | sym << 8`, matching the verifier's own decode.
        b = bytearray(data)
        target = find_sec(b, sys.argv[4])['i']
        for s in sections(b):
            if s['type'] == 4 and s['info'] == target:
                off = s['off'] + int(sys.argv[5], 0) * 12
                rinfo = u32(b, off + 4)
                p32(b, off + 4, (rinfo & ~0xff) | int(sys.argv[6], 16))
                break
        else:
            raise SystemExit('no RELA for ' + sys.argv[4])
    elif cmd == 'report-field':
        stable, field, value = sys.argv[4], sys.argv[5], sys.argv[6]
        out = []
        for l in rows(data.decode()):
            c = l.split(' | ')
            if c[0] == stable:
                if 'retained' in c:
                    c.remove('retained')
                idx = {'class': 4, 'entity': 5, 'ownership': 6, 'address': 7,
                       'size': 8, 'align': 9, 'flags': 10, 'bound_only': 12}[field]
                c[idx] = '0x%08x' % int(value, 16) if field == 'address' \
                    else str(int(value, 0))
                h = layout_hash(int(c[4]), int(c[5]), int(c[6]), int(c[7], 16),
                                int(c[9]), int(c[10]))
                c[11] = '0x' + h.hex()
                if int(c[10]) & 1:
                    c.append('retained')
                l = ' | '.join(c)
            out.append(l)
        open(dst, 'w').write('\n'.join(out) + '\n')
        return
    elif cmd in ('report-raw', 'report-dup'):
        col = {'stable': 0, 'sym': 1, 'file': 2, 'section': 3, 'class': 4,
               'entity': 5, 'ownership': 6, 'address': 7, 'size': 8,
               'align': 9, 'flags': 10, 'hash': 11, 'bound_only': 12}[sys.argv[5]]
        stable, literal = sys.argv[4], sys.argv[6]
        out = []
        for l in rows(data.decode()):
            c = l.split(' | ')
            if c[0] == stable and cmd == 'report-dup':
                # Insert a SECOND row for the same (file, sym, stable)
                # triple, with one column changed and the hash recomputed.
                d = list(c)
                if 'retained' in d:
                    d.remove('retained')
                d[col] = literal
                if col not in (11, 0, 1, 2, 3):
                    h = layout_hash(int(d[4]), int(d[5]), int(d[6]),
                                    int(d[7], 16), int(d[9]), int(d[10]))
                    d[11] = '0x' + h.hex()
                if int(d[10]) & 1:
                    d.append('retained')
                out.append(l)
                l = ' | '.join(d)
            elif c[0] == stable and cmd == 'report-raw':
                c[col] = literal        # hash left untouched on purpose
                l = ' | '.join(c)
            out.append(l)
        open(dst, 'w').write('\n'.join(out) + '\n')
        return
    elif cmd == 'report-flip-hash':
        stable = sys.argv[4]
        out = []
        for l in rows(data.decode()):
            c = l.split(' | ')
            if c[0] == stable:
                c[11] = '0x%08x' % (int(c[11], 16) ^ 1)
            out.append(' | '.join(c))
        open(dst, 'w').write('\n'.join(out) + '\n')
        return
    elif cmd == 'report-drop':
        stable = sys.argv[4]
        open(dst, 'w').write('\n'.join(
            [l for l in rows(data.decode()) if l.split(' | ')[0] != stable]) + '\n')
        return
    elif cmd == 'report-add':
        h = layout_hash(0, 0, 0, 0x70, 1, 0)
        open(dst, 'w').write(data.decode() + (
            'zz | _zz | %s/r.o | .mcu.fixed.zz | 0 | 0 | 0 | 0x00000070 | 4 | 1 '
            '| 0 | 0x%s | 0\n' % (sys.argv[4], h.hex())))
        return
    elif cmd == 'report-reverse':
        open(dst, 'w').write('\n'.join(reversed(rows(data.decode()))) + '\n')
        return
    elif cmd == 'names-poke':
        b = bytearray(data)
        sec = find_sec(b, '.mcs251.placement.names')
        off = sec['off'] + int(sys.argv[4], 0)
        raw = bytes.fromhex(sys.argv[5])
        b[off:off + len(raw)] = raw
    elif cmd == 'names-size':
        b = bytearray(data)
        sec = find_sec(b, '.mcs251.placement.names')
        p32(b, sec['hdroff'] + 20, sec['size'] + int(sys.argv[4], 0))
    elif cmd == 'names-null-carrier':
        # G11-C2 MAJOR-1 fixture support: relocate the EXACT names carrier --
        # its name, its data range and its header words -- onto section header
        # index 0, the ELF null header.  The original named header is retired
        # as `.comment` so the object carries exactly ONE names carrier (a
        # second one would be caught by the duplicate rule and hide the shape
        # rule under test).  Retiring the name requires relocating the whole
        # section-name string table to EOF and appending the new entry: every
        # existing sh_name offset is relative, so the move keeps them valid.
        # A conforming ELF keeps index 0 SHT_NULL with the all-zero header, so
        # every shape written here must be refused before any decode.
        b = bytearray(data)
        sec = find_sec(b, '.mcs251.placement.names')
        shoff = u32(b, 32)
        shnum = struct.unpack_from('>H', b, 48)[0]
        assert shnum > sec['i']
        shstr = struct.unpack_from('>H', b, 50)[0]
        stroff = u32(b, shoff + 40 * shstr + 16)
        strsize = u32(b, shoff + 40 * shstr + 20)
        tab = bytes(b[stroff:stroff + strsize])
        newtab = tab + b'.comment\0'
        newstr = len(b)
        b.extend(newtab)
        p32(b, shoff + 40 * shstr + 16, newstr)
        p32(b, shoff + 40 * shstr + 20, len(newtab))
        # Index 0 carries the carrier's name and data range with the mutated
        # shape; the original header loses the name (`.comment`).
        p32(b, shoff, sec['nameoff'])            # sh_name
        p32(b, shoff + 4, int(sys.argv[4], 0))   # sh_type
        p32(b, shoff + 8, int(sys.argv[5], 0))   # sh_flags
        p32(b, shoff + 12, 0)                    # sh_addr
        p32(b, shoff + 16, sec['off'])           # sh_offset
        p32(b, shoff + 20, sec['size'])          # sh_size
        p32(b, shoff + 24, 0)                    # sh_link
        p32(b, shoff + 28, 0)                    # sh_info
        p32(b, shoff + 32, int(sys.argv[6], 0))  # sh_addralign
        p32(b, shoff + 36, 0)                    # sh_entsize
        # The original header keeps a LEGAL but inert shape (an empty
        # `.comment`), so this mutation isolates the index-0 question: no
        # unrelated input rule may reject the object first.
        p32(b, sec['hdroff'], len(tab))          # name -> `.comment`
        p32(b, sec['hdroff'] + 4, 1)             # SHT_PROGBITS
        p32(b, sec['hdroff'] + 8, 0)             # sh_flags
        p32(b, sec['hdroff'] + 12, 0)            # sh_addr
        p32(b, sec['hdroff'] + 16, 0)            # sh_offset
        p32(b, sec['hdroff'] + 20, 0)            # sh_size
        p32(b, sec['hdroff'] + 24, 0)            # sh_link
        p32(b, sec['hdroff'] + 28, 0)            # sh_info
        p32(b, sec['hdroff'] + 32, 1)            # sh_addralign
        p32(b, sec['hdroff'] + 36, 0)            # sh_entsize
    elif cmd == 'map-section':
        import re
        key = sys.argv[4]
        val = int(sys.argv[5], 16)
        t = data.decode()
        t2, n = re.subn(r'^%s 0x[0-9a-f]+' % re.escape(key),
                        '%s 0x%06x' % (key, val), t, flags=re.M)
        if not n:
            raise SystemExit('no section row ' + key)
        open(dst, 'w').write(t2)
        return
    elif cmd in ('carrier-digest', 'carrier-addr'):
        # G11-D2 fixture support (design section 11.2): update the final
        # ELF's positioning carrier so an object mutation keeps reaching the
        # check layer it was written for.  Other fields stay untouched.
        b = bytearray(data)
        ordinal = int(sys.argv[4], 0)
        objfile = sys.argv[5]
        obj = bytearray(open(objfile, 'rb').read())
        carrier = find_sec(b, '.mcs251.placement.positions')
        d = carrier['off'] + 20
        objn, recn = struct.unpack_from('>II', b, d + 4)
        if ordinal >= objn:
            raise SystemExit('ordinal out of range')
        digest_base = d + 16
        dg = hashlib.sha256(bytes(obj)).digest()
        b[digest_base + 32 * ordinal:
          digest_base + 32 * (ordinal + 1)] = dg
        if cmd == 'carrier-addr':
            want = None
            for s in sections(obj):
                if s['name'] == sys.argv[6]:
                    want = s['i']
            if want is None:
                raise SystemExit('no section ' + sys.argv[6] + ' in ' + objfile)
            addr = int(sys.argv[7], 16)
            o = digest_base + 32 * objn
            hit = False
            for _ in range(recn):
                rsz, oid, shndx = struct.unpack_from('>III', b, o)
                if oid == ordinal and shndx == want:
                    scount = u32(b, o + 20)
                    if scount != 1:
                        raise SystemExit('record has %d slices' % scount)
                    p32(b, o + 24 + 4, addr)
                    hit = True
                o += rsz
            if not hit:
                raise SystemExit('no carrier record for section ' + sys.argv[6])
    elif cmd == 'map-bound-rename':
        import re
        t = data.decode()
        t2, n = re.subn(r'^%s = (0x[0-9a-f]+)' % re.escape(sys.argv[4]),
                        '%s = \\1' % sys.argv[5], t, flags=re.M)
        if not n:
            raise SystemExit('no boundary row ' + sys.argv[4])
        open(dst, 'w').write(t2)
        return
    elif cmd == 'map-bound':
        import re
        t = data.decode()
        val = sys.argv[5][2:] if sys.argv[5].startswith('0x') else sys.argv[5]
        t2 = re.sub(r'^%s = 0x[0-9a-f]+' % re.escape(sys.argv[4]),
                    '%s = 0x%s' % (sys.argv[4], val), t, flags=re.M)
        if t2 == t:
            raise SystemExit('no boundary row ' + sys.argv[4])
        open(dst, 'w').write(t2)
        return
    elif cmd.startswith('pos-'):
        # G11-D2 structural carrier variants (design section 11.2): every
        # mutation is a fixed-point byte change on the frozen section-3
        # layout; no producer serializer is involved.
        b = bytearray(data)
        carrier = find_sec(b, '.mcs251.placement.positions')
        base = carrier['off']
        d = base + 20
        objn, recn = struct.unpack_from('>II', b, d + 4)
        rec_base = d + 16 + 32 * objn

        def rec_off(i):
            o = rec_base
            for k in range(i):
                o += u32(b, o)
            return o

        def write_file():
            open(dst, 'wb').write(bytes(b))

        if cmd == 'pos-note':
            field = sys.argv[4]
            val = int(sys.argv[5], 0)
            slot = {'namesz': (base, 0), 'descsz': (base, 4),
                    'type': (base, 8), 'version': (d, 0),
                    'objects': (d, 4), 'records': (d, 8),
                    'reserved': (d, 12)}[field]
            p32(b, slot[0] + slot[1], val)
        elif cmd == 'pos-name':
            idx = int(sys.argv[4], 0)
            val = int(sys.argv[5], 0) & 0xff
            b[base + 12 + idx] = val
        elif cmd == 'pos-sec':
            field = sys.argv[4]
            val = int(sys.argv[5], 0)
            o = carrier['hdroff']
            slot = {'name': 0, 'type': 4, 'flags': 8, 'addr': 12,
                    'offset': 16, 'size': 20, 'link': 24, 'info': 28,
                    'align': 32, 'entsize': 36}[field]
            p32(b, o + slot, val)
        elif cmd == 'pos-digest':
            idx = int(sys.argv[4], 0)
            off = int(sys.argv[5], 0)
            val = int(sys.argv[6], 0) & 0xff
            b[d + 16 + 32 * idx + off] = val
        elif cmd == 'pos-record':
            op = sys.argv[4]
            if op == 'field':
                i, field, val = int(sys.argv[5], 0), sys.argv[6], \
                    int(sys.argv[7], 0)
                o = rec_off(i)
                slot = {'size': 0, 'object': 4, 'shndx': 8, 'space8': 12,
                        'res8': 13, 'res16': 14, 'input': 16,
                        'slices': 20}[field]
                if field == 'space8' or field == 'res8':
                    b[o + slot] = val & 0xff
                elif field == 'res16':
                    struct.pack_into('>H', b, o + slot, val & 0xffff)
                else:
                    p32(b, o + slot, val)
            elif op in ('swap', 'dup', 'drop', 'append'):
                # Size-changing ops rebuild the record area; the carrier is
                # the last content before the section-header table, so the
                # table shifts by the same delta.
                i = int(sys.argv[5], 0)
                j = int(sys.argv[6], 0) if op == 'swap' else 0
                ro = rec_off(i)
                rsz = u32(b, ro)
                if op == 'swap':
                    rj = rec_off(j)
                    rszj = u32(b, rj)
                    lo, hi = min(ro, rj), max(ro, rj)
                    blk = bytes(b[lo:hi + (rsz if ro < rj else rszj)])
                    a = bytes(b[ro:ro + rsz])
                    c = bytes(b[rj:rj + rszj])
                    b[ro:ro + rsz] = c
                    b[rj:rj + rszj] = a
                elif op == 'dup':
                    blk = bytes(b[ro:ro + rsz])
                    # header fields FIRST: the structural insert below shifts
                    # the section-header table, invalidating hdroff.
                    p32(b, d + 8, recn + 1)
                    p32(b, base + 4, u32(b, base + 4) + rsz)
                    p32(b, carrier['hdroff'] + 20, carrier['size'] + rsz)
                    p32(b, 32, u32(b, 32) + rsz)  # e_shoff
                    b[ro + rsz:ro + rsz] = blk
                elif op == 'append':
                    blk = bytes(b[ro:ro + rsz])
                    end = rec_off(recn)
                    p32(b, d + 8, recn + 1)
                    p32(b, base + 4, u32(b, base + 4) + rsz)
                    p32(b, carrier['hdroff'] + 20, carrier['size'] + rsz)
                    p32(b, 32, u32(b, 32) + rsz)
                    b[end:end] = blk
                elif op == 'drop':
                    p32(b, d + 8, recn - 1)
                    p32(b, base + 4, u32(b, base + 4) - rsz)
                    p32(b, carrier['hdroff'] + 20, carrier['size'] - rsz)
                    p32(b, 32, u32(b, 32) - rsz)
                    del b[ro:ro + rsz]
            else:
                raise SystemExit('unknown pos-record op ' + op)
        elif cmd == 'pos-slice':
            i, k, field = int(sys.argv[4], 0), int(sys.argv[5], 0), \
                sys.argv[6]
            val = int(sys.argv[7], 0)
            o = rec_off(i) + 24 + 12 * k
            slot = {'offset': 0, 'addr': 4, 'length': 8}[field]
            p32(b, o + slot, val)
        elif cmd == 'pos-slice-op':
            i, op, k = int(sys.argv[4], 0), sys.argv[5], int(sys.argv[6], 0)
            o = rec_off(i)
            rsz = u32(b, o)
            so = o + 24 + 12 * k
            if op == 'drop':
                p32(b, o, rsz - 12)
                p32(b, o + 20, u32(b, o + 20) - 1)
                p32(b, base + 4, u32(b, base + 4) - 12)
                p32(b, carrier['hdroff'] + 20, carrier['size'] - 12)
                p32(b, 32, u32(b, 32) - 12)
                del b[so:so + 12]
            elif op == 'dup':
                blk = bytes(b[so:so + 12])
                p32(b, o, rsz + 12)
                p32(b, o + 20, u32(b, o + 20) + 1)
                p32(b, base + 4, u32(b, base + 4) + 12)
                p32(b, carrier['hdroff'] + 20, carrier['size'] + 12)
                p32(b, 32, u32(b, 32) + 12)
                b[so + 12:so + 12] = blk
            else:
                raise SystemExit('unknown pos-slice-op ' + op)
        elif cmd == 'pos-trunc':
            n = int(sys.argv[4], 0)
            p32(b, carrier['hdroff'] + 20, carrier['size'] - n)
            p32(b, 32, u32(b, 32) - n)
            del b[base + carrier['size'] - n:base + carrier['size']]
        elif cmd == 'pos-move':
            # Relocate the carrier to the end of the file (optionally with
            # extra tail bytes), moving the section-header table after it.
            # The header edits happen BEFORE the table snapshot so the
            # relocated table carries them.
            tail = bytes.fromhex(sys.argv[4]) if sys.argv[4] != '-' else b''
            body = bytes(b[base:base + carrier['size']])
            newoff = (len(b) + 3) & ~3
            p32(b, carrier['hdroff'] + 16, newoff)
            p32(b, carrier['hdroff'] + 20, len(body) + len(tail))
            shoff = u32(b, 32)
            shnum = struct.unpack_from('>H', b, 48)[0]
            shdrs = bytes(b[shoff:shoff + 40 * shnum])
            b += b'\0' * (newoff - len(b))
            b += body + tail
            newsh = (len(b) + 3) & ~3
            b += b'\0' * (newsh - len(b))
            b += shdrs
            p32(b, 32, newsh)
        elif cmd == 'pos-dup-sec':
            # Append a SECOND header for the same carrier section: the whole
            # table is relocated to EOF (contiguous) with the copy appended.
            shoff0 = u32(b, 32)
            shnum = struct.unpack_from('>H', b, 48)[0]
            table = bytes(b[shoff0:shoff0 + 40 * shnum])
            hdr = bytes(b[carrier['hdroff']:carrier['hdroff'] + 40])
            newsh = (len(b) + 3) & ~3
            b += b'\0' * (newsh - len(b))
            b += table + hdr
            p32(b, 32, newsh)
            struct.pack_into('>H', b, 48, shnum + 1)
        else:
            raise SystemExit('unknown command ' + cmd)
        write_file()
        return
    else:
        raise SystemExit('unknown command ' + cmd)
    open(dst, 'wb').write(bytes(b))


main()
