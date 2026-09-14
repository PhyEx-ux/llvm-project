#!/usr/bin/env python3
# isr-fixture.py - independent YAML ELF32/MSB/v1 fixture generator for the
# MCS251 ISR vector tests (T07, ISR-TASK-BREAKDOWN.md).
#
# Frozen interface:
#   isr-fixture.py --out FILE --slots 0,6,8,24,36,49,50,51
#   isr-fixture.py --out FILE --slots 7 --allow-invalid-slot
#   isr-fixture.py --out FILE --slots 1 --mutation NAME
#
# Output: one complete ELF32/MSB/ET_REL/EM_MCS251 object (yaml2obj input) with
#   - every ISR as the frozen 41-byte empty-handler machine code (T04),
#   - STT_FUNC symbols with correct sizes,
#   - one ENTRY + one REGISTER metadata record per registered slot,
#   - one zero-width type9 association per record,
#   - the original v1 ABI note,
#   - an ordinary _main in plain CSEG terminated by `80 FE` (so the CRT's
#     undefined _main resolves), never marked as an ISR,
#   - no CRT inside.
#
# The generator deliberately re-derives nothing from the product: the legal
# slot list below is an independent frozen copy for argument sanity only, and
# the expected image layout lives in isr-check-image.py.

import argparse
import struct
import sys

# Frozen 41-byte empty ISR (A6): push PSW + 9 x DR pushes, inverse pops,
# pop PSW, RETI (0x32).
ISR_BODY = bytes.fromhex(
    'c0d0ca0bca1bca2bca3bca4bca5bca6bca7bcaeb'
    'daebda7bda6bda5bda4bda3bda2bda1bda0bd0d032')
assert len(ISR_BODY) == 41

# Frozen 4-byte default (fail-stop) body (A5). Used by the
# default-as-user-isr mutation so the only broken property is the reference
# rule: the kind3-associated function itself keeps valid default shape.
DEFAULT_BODY = bytes.fromhex('C2AF80FE')

# Independent legal-slot copy for generator-side argument sanity.  G1 profile
# (PM ruling 2026-09-13): 127 slots, Legal = 109.  This is a deliberate second
# copy so the generator cannot silently follow a product-table regression.
LEGAL = tuple(
    s for s in range(127)
    if s not in (7, 13, 14, 15, 22, 23, 32, 33, 34, 35,
                 81, 92, 93, 94, 95, 100, 101, 113))
assert len(LEGAL) == 109, len(LEGAL)

# G1 x P-4: the generated object carries the A4 v2 identity (a
# `.mcs251.attributes` carrier) rather than the v1 `.note.mcs251.abi`, because
# the only approved IRQ CRT is a v2 object and a v1/v2 mix is refused.  Tag 28
# records the ordinary `_main` definition so the P-4 per-object coverage rule
# is satisfied; the ISR handler symbols are local and stay out of the
# signature domain.
ATTRS_CARRIER_HEX = ('41000000C14D43533235310001000000B6048104000000020581040000000206810400000001078104000000030881040000F3FF098104000000200A8104000000200B8104000000200C8104000000020D8104000000080E8104000000020F810400000002108104000000021181040000000212810400000000138104000000001481040000000018830C010400000020010400000008198104000000011A8104000000201B8104000000001C8413010001000000000001000002015F6D61696E00')

RECORD = '>HHBBBBHHIII'  # 24 bytes, all big-endian (A3.2).


def rec(kind, entry, hw, save, slot, asset):
    # G1 raised the ISR protocol version to 2 (PM ruling 2026-09-13); the
    # 24-byte layout is unchanged.
    return struct.pack(RECORD, 2, 24, kind, entry, hw, save, slot, 1, 0,
                       asset, 0)


def user_pair(slot):
    """Valid ENTRY + REGISTER record bytes for a compiler ISR."""
    return (rec(1, 1, 1, 1, slot, 0), rec(2, 1, 1, 1, slot, 0))


def default_rec():
    return rec(3, 2, 1, 0, 0xFFFF, 1)


def build(slots, mutation):
    # One function per distinct slot; repeated list entries add one more
    # REGISTER for the same exact function (single factor: an extra
    # registration, the ENTRY stays unique).
    unique = []
    for s in slots:
        if s not in unique:
            unique.append(s)
    shared = slots[0] if mutation == 'default-as-user-isr' else None

    def body(s):
        return DEFAULT_BODY if s == shared else ISR_BODY

    offset = {}
    off = 0
    for s in unique:
        offset[s] = off
        off += len(body(s))
    text = b''.join(body(s) for s in unique) + b'\x80\xfe'  # + ordinary _main

    records = []   # list of record byte strings
    relocs = []    # list of (offset-in-meta, symbol-name, addend)

    def add(recbytes, sym):
        records.append(recbytes)
        relocs.append((len(records) * 24 - 24 + 12, sym, 0))

    seen = set()
    for s in slots:
        if s not in seen:
            seen.add(s)
            add(rec(1, 1, 1, 1, s, 0), '_irq%d' % s)  # ENTRY (once)
        add(rec(2, 1, 1, 1, s, 0), '_irq%d' % s)      # REGISTER per entry
    if mutation == 'default-as-user-isr':
        # The single extra factor: a kind3 record associated with the
        # already-registered ISR's reference (which carries the valid 4-byte
        # default body, so only the reference rule is broken).
        add(default_rec(), '_irq%d' % shared)

    symbols = [('_irq%d' % s, 'STT_FUNC', 'STB_LOCAL', '.text',
                offset[s], len(body(s))) for s in unique]
    symbols.append(('_main', 'STT_FUNC', 'STB_GLOBAL', '.text',
                    off, 2))

    if mutation:
        first = slots[0]
        if mutation == 'version':
            # The legal version is 2; the old 1 is now the rejected value.
            records[0] = b'\x00\x01' + records[0][2:]
        elif mutation == 'record-size':
            records[0] = records[0][:2] + b'\x00\x19' + records[0][4:]
        elif mutation == 'caps':
            records[0] = records[0][:10] + b'\x00\x02' + records[0][12:]
        elif mutation == 'reserved-field':
            records[0] = records[0][:20] + b'\x00\x00\x00\x01'
        elif mutation == 'missing-ref':
            relocs = relocs[1:]
        elif mutation == 'duplicate-ref':
            # Re-point the REGISTER record's relocation at the ENTRY record's
            # association offset: two type9 refs on one record, counts intact.
            relocs[1] = (relocs[0][0], relocs[1][1], relocs[1][2])
        elif mutation == 'ref-addend':
            relocs[0] = (relocs[0][0], relocs[0][1], 1)
        elif mutation == 'ref-section-symbol':
            relocs[0] = (relocs[0][0], '.mcs251.isr', 0)
            symbols.append(('.mcs251.isr', 'STT_SECTION', 'STB_LOCAL',
                            '.mcs251.isr', 0, 0))
        elif mutation == 'ref-in-alloc':
            pass  # handled below: a type9 inside .rela.text
        elif mutation == 'wrong-entry-kind':
            records[0] = records[0][:5] + b'\x02' + records[0][6:]
        elif mutation == 'wrong-save-profile':
            records[1] = records[1][:7] + b'\x00' + records[1][8:]
        elif mutation == 'registration-without-entry':
            records = records[1:]
            relocs = [r for r in relocs if r[0] != 12]
            relocs = [(o - 24, n, a) for (o, n, a) in relocs]
        elif mutation == 'entry-without-registration':
            records = records[:1]
            relocs = [r for r in relocs if r[0] != 36]
        elif mutation == 'default-as-user-isr':
            # Handled during construction above: the kind3 record is attached
            # to the already-registered ISR's reference, and that function
            # carries the valid 4-byte default body, so the reference rule is
            # the single broken factor.
            pass
        elif mutation == 'undefined-entry':
            relocs[0] = (relocs[0][0], '_irq%d_undef' % first, 0)
            symbols.append(('_irq%d_undef' % first, 'STT_FUNC', 'STB_GLOBAL',
                            None, 0, 0))
        else:
            sys.exit('unknown mutation: ' + mutation)

    sections = [
        ('.text', 'SHT_PROGBITS', '[ SHF_ALLOC, SHF_EXECINSTR ]', 1,
         "Content: '%s'" % text.hex().upper(), None),
        ('.mcs251.isr', 'SHT_PROGBITS', '[ ]', 4,
         "Content: '%s'" % b''.join(records).hex().upper(), None),
    ]
    if mutation == 'ref-in-alloc':
        sections.append(
            ('.rela.text', 'SHT_RELA', '[ ]', 4, None,
             'Link: .symtab\n    Info: .text\n    Relocations:\n'
             '      - Offset: 0x0\n        Type: R_MCS251_ISR_REF\n'
             '        Symbol: _irq%d\n        Addend: 0' % first))
    sections.append(
        ('.rela.mcs251.isr', 'SHT_RELA', '[ ]', 4, None,
         'Link: .symtab\n    Info: .mcs251.isr\n    Relocations:\n' +
         ''.join('      - Offset: 0x%X\n        Type: R_MCS251_ISR_REF\n'
                 '        Symbol: %s\n        Addend: %d\n'
                 % (o, n, a) for (o, n, a) in relocs)))
    sections.append(
        ('.mcs251.attributes', '0x70000003', '[ ]', 1,
         "Content: '%s'" % ATTRS_CARRIER_HEX, None))

    out = []
    out.append('--- !ELF')
    out.append('FileHeader:')
    out.append('  Class: ELFCLASS32')
    out.append('  Data: ELFDATA2MSB')
    out.append('  Type: ET_REL')
    out.append('  Machine: EM_MCS251')
    out.append('  Flags: [ EF_MCS251_ABI_V1 ]')
    out.append('Sections:')
    for name, stype, flags, align, content, extra in sections:
        out.append('  - Name: %s' % name)
        out.append('    Type: %s' % stype)
        out.append('    Flags: %s' % flags)
        out.append('    AddressAlign: %d' % align)
        if content:
            out.append('    %s' % content)
        if extra:
            out.append('    %s' % extra)
    out.append('Symbols:')
    for name, stype, bind, sec, value, size in symbols:
        out.append('  - Name: %s' % name)
        out.append('    Type: %s' % stype)
        out.append('    Binding: %s' % bind)
        if sec is not None:
            out.append('    Section: %s' % sec)
        out.append('    Value: 0x%X' % value)
        out.append('    Size: %d' % size)
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--slots', required=True)
    ap.add_argument('--allow-invalid-slot', action='store_true')
    ap.add_argument('--mutation')
    args = ap.parse_args()
    try:
        slots = [int(x) for x in args.slots.split(',')]
    except ValueError:
        sys.exit('bad --slots list')
    if not slots:
        sys.exit('empty --slots list')
    for s in slots:
        if not 0 <= s <= 0xFFFF:
            sys.exit('slot %d does not fit the 16-bit vector_slot field' % s)
        if not args.allow_invalid_slot and s not in LEGAL:
            sys.exit('slot %d is not a legal slot; pass --allow-invalid-slot '
                     'to emit it anyway' % s)
    with open(args.out, 'w') as f:
        f.write(build(slots, args.mutation))


if __name__ == '__main__':
    main()
