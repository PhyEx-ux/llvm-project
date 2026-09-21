#!/usr/bin/env python3
"""Generate an MCS251 ELF32/MSB reentrancy fixture as a yaml2obj input.

E1/WP2 R11-1 helper.  The fixture places TWO two-argument leaf functions'
static parameter slots in two OVERLAID OSEG sections, so their final
addresses intersect.  Each slot is written by its own registered ISR
(`_irq1' -> `left', `_irq2' -> `right').  An optional third, never-called
ordinary function `_unused' can be added, either calling `left' or only
materializing `left's slot address.

The point of this fixture family is that `_unused' supplies only PRESUMED
foreground context (it is not traced to the reset chain).  It must never
turn the independently established ISR-vs-ISR combination into a
conditional finding.

Usage: isr-two-slot-fixture.py <out.yaml> <mode>
  mode = none         two registered ISRs, no extra root
         unused-call  add an uncalled `_unused' that CALLS `left'
         unused-read  add an uncalled `_unused' that READS `left's slot
         presumed     ONE registered ISR plus an uncalled `_unused' reader
                      (only presumed foreground, no ISR-vs-ISR combination)
         fg-call      two registered ISRs plus a positively rooted `_main'
                      calling `left' (BOTH combinations apply)

Both slots are 2 bytes at offset 0 of their respective OSEG section, so the
ISR-side slot ranges are byte-identical.  The generator deliberately reuses
the repo's frozen producer encodings (`7E 18 mm ll' + `7A 1C hh 00' for a
24-bit absolute slot address, `9A' + R_MCS251_24 for a confirmed ecall) and
the repo's real IRQ CRT, so no product behaviour is re-derived here.
"""

import json
import pathlib
import re
import sys

# The v2 signature carrier, reused from lld/test/MCS251/isr-reentrancy-addr.test
# (the same file-level convention the other reentrancy fixtures follow).
ATTRS_PREFIX = (
    '41000000D14D43533235310001000000C6048104000000020581040000000206810400000001078104000000030881040000F3FF098104000000200A8104000000200B8104000000200C8104000000020D8104000000080E8104000000020F810400000002108104000000021181040000000212810400000000138104000000001481040000000018830C010400000020010400000008198104000000011A8104000000201B810400000000')

SLOT_L = '_left_PARM_2'
SLOT_R = '_right_PARM_2'
SEC_L = '.mcs251.OSEG.left'
SEC_R = '.mcs251.OSEG.right'

RET = [('raw', 'AA')]   # eret
IRET = [('raw', '32')]  # reti


def uleb(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return out


def build(mode):
    a, b = SLOT_L, SLOT_R
    functions = [
        ('_irq1', [('addr', a), ('call', '_left')] + list(IRET)),
        ('_left', [('addr', a)] + list(RET)),
        ('helper', list(RET)),
        ('_main', list(RET)),
    ]
    if mode == 'presumed':
        functions = [
            ('_irq1', [('addr', a), ('call', '_left')] + list(IRET)),
            ('_left', [('addr', a)] + list(RET)),
            ('_unused', [('addr', a)] + list(RET)),
            ('helper', list(RET)),
            ('_main', list(RET)),
        ]
        irqs = ('_irq1',)
        slots = [(a, SEC_L, 0, 2, 2)]
    else:
        functions.insert(0, ('_irq2', [('addr', b), ('call', '_right')] + list(IRET)))
        functions.insert(2, ('_right', [('addr', b)] + list(RET)))
        if mode == 'unused-call':
            functions.append(('_unused', [('addr', a), ('call', '_left')] + list(RET)))
        elif mode == 'unused-read':
            functions.append(('_unused', [('addr', a)] + list(RET)))
        elif mode == 'fg-call':
            # Positively rooted foreground caller (`_main' is reached from the
            # CRT reset/boot contract) writing the ISR-visible slot.
            functions = [f for f in functions if f[0] != '_main']
            functions.insert(3, ('_main', [('addr', a), ('call', '_left')] + list(RET)))
        elif mode != 'none':
            raise SystemExit('unknown mode ' + mode)
        irqs = ('_irq1', '_irq2')
        slots = [(a, SEC_L, 0, 2, 2), (b, SEC_R, 0, 2, 2)]

    code = bytearray()
    rel = []
    syms = []
    sections = []

    def reloc(off, typ, sym, add=0):
        rel.append({'Offset': off, 'Type': 'R_MCS251_' + typ, 'Symbol': sym,
                    'Addend': add})

    for fn, ops in functions:
        start = len(code)
        for op in ops:
            off = len(code)
            if op[0] == 'addr':
                code.extend(bytes.fromhex('7E1800007A1C0000'))
                for delta, typ in [(2, 'MID8'), (3, 'LO8'), (7, 'HI8')]:
                    reloc(off + delta, typ, op[1], op[2] if len(op) > 2 else 0)
            elif op[0] == 'call':
                code.extend(bytes.fromhex('9A000000'))
                reloc(off + 1, '24', op[1])
            elif op[0] == 'raw':
                code.extend(bytes.fromhex(op[1]))
        syms.append({'Name': fn, 'Type': 'STT_FUNC', 'Binding': 'STB_GLOBAL',
                     'Section': '.text', 'Value': start,
                     'Size': len(code) - start})
    sections.append({'Name': '.text', 'Type': 'SHT_PROGBITS',
                     'Flags': ['SHF_ALLOC', 'SHF_EXECINSTR'],
                     'AddressAlign': 1, 'Content': code.hex()})
    for sn, sec, offset, size, secsize in slots:
        if not any(s['Name'] == sec for s in sections):
            sections.append({'Name': sec, 'Type': 'SHT_NOBITS',
                             'Flags': ['SHF_ALLOC', 'SHF_WRITE',
                                       'SHF_MCS251_OVERLAY'],
                             'AddressAlign': 1, 'Size': secsize})
        syms.append({'Name': sn, 'Type': 'STT_OBJECT', 'Binding': 'STB_GLOBAL',
                     'Section': sec, 'Value': offset, 'Size': size})

    entries = [(i + 1, n, kind) for i, n in enumerate(irqs) for kind in (1, 2)]
    records = b''.join(
        bytes.fromhex('00020018') + bytes([kind, 1, 1, 1]) +
        i.to_bytes(2, 'big') + bytes.fromhex('0001000000000000000000000000')
        for i, n, kind in entries)
    sections.append({'Name': '.mcs251.isr', 'Type': 'SHT_PROGBITS',
                     'AddressAlign': 4, 'Content': records.hex()})
    sections.append({'Name': '.rela.mcs251.isr', 'Type': 'SHT_RELA',
                     'Link': '.symtab', 'Info': '.mcs251.isr',
                     'Relocations': [
                         {'Offset': j * 24 + 12, 'Type': 'R_MCS251_ISR_REF',
                          'Symbol': n, 'Addend': 0}
                         for j, (i, n, kind) in enumerate(entries)]})
    if rel:
        sections.append({'Name': '.rela.text', 'Type': 'SHT_RELA',
                         'Link': '.symtab', 'Info': '.text',
                         'Relocations': rel})

    # v2 signature domain: one record per function, two bytes per name.
    blob = bytearray()
    recs = bytearray()
    for fn, ops in functions:
        recs.extend(len(blob).to_bytes(4, 'little') + bytes([1, 0, 0, 2, 1]))
        blob.extend(fn.encode() + b'\0')
    value = bytes([1, 0]) + len(functions).to_bytes(2, 'little') + recs + blob
    attr = bytearray.fromhex(ATTRS_PREFIX) + bytes.fromhex('1c84') + \
        uleb(len(value)) + value
    attr[1:5] = (len(attr) - 1).to_bytes(4, 'big')
    attr[13:17] = (len(attr) - 12).to_bytes(4, 'big')
    sections.append({'Name': '.mcs251.attributes', 'Type': 0x70000003,
                     'AddressAlign': 1, 'Content': attr.hex()})

    return {'FileHeader': {'Class': 'ELFCLASS32', 'Data': 'ELFDATA2MSB',
                           'Type': 'ET_REL', 'Machine': 'EM_MCS251',
                           'Flags': ['EF_MCS251_ABI_V1']},
            'Sections': sections, 'Symbols': syms}


def main():
    out = pathlib.Path(sys.argv[1])
    mode = sys.argv[2]
    out.write_text('--- !ELF\n' + json.dumps(build(mode), indent=2) + '\n')


if __name__ == '__main__':
    main()
