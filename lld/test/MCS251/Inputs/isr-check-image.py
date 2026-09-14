#!/usr/bin/env python3
# isr-check-image.py - independent image checker for the MCS251 IRQ vector
# link output (T07). It parses the ELF32/MSB PT_LOAD segments itself and never
# imports product code; the map is only used as a secondary consistency
# witness, never as proof of machine bytes.
#
#   isr-check-image.py IMAGE.elf IMAGE.map --registered 0,6,8,...
#
# Checks, all from the frozen A4/A5 constants (hardcoded copies):
#   - the 13 non-legal slots occupy no PT_LOAD payload at all,
#   - every one of the 109 legal slots holds an EJMP at the address formula,
#   - legal-slot tail 4 bytes carry no payload,
#   - all unregistered legal slots jump to one shared default target whose
#     machine bytes are the frozen C2AF80FE fail-stop word,
#   - the reset at the entry point is exactly 3 bytes (ljmp) abutting the
#     vector base and jumping into the BOOT range (>= 0xFF0500),
#   - the map's 127 IRQ rows agree with the formula and classification.

import re
import struct
import sys

BASE = 0xFF0003
STRIDE = 8
COUNT = 127
HANDLER_STRIDE = 41  # fixture convention: one 41-byte handler per slot
NON_LEGAL = frozenset({7, 13, 14, 15, 22, 23, 32, 33, 34, 35,
                         81, 92, 93, 94, 95, 100, 101, 113})
SYSTEM = frozenset({14, 15})
DEFAULT_BYTES = bytes.fromhex('C2AF80FE')
BOOT_FLOOR = 0xFF0500


def die(msg):
    sys.exit('isr-check-image: FAIL: ' + msg)


def be(b, off, n):
    return int.from_bytes(b[off:off + n], 'big')


def main():
    argv = sys.argv[1:]
    if '--registered' not in argv or len(argv) != 4:
        die('usage: isr-check-image.py IMAGE MAP --registered LIST')
    image_path, map_path = argv[0], argv[1]
    registered = set(int(x) for x in argv[3].split(',') if x != '')
    for s in registered:
        if s in NON_LEGAL or not 0 <= s < COUNT:
            die('registered slot %d is not a legal slot' % s)

    data = open(image_path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 2:
        die('not an ELF32/MSB image')
    entry = be(data, 24, 4)
    phoff = be(data, 28, 4)
    phentsize = be(data, 42, 2)
    phnum = be(data, 44, 2)

    mem = {}
    segs = []
    for i in range(phnum):
        p = phoff + i * phentsize
        p_type, p_offset = be(data, p, 4), be(data, p + 4, 4)
        p_vaddr, p_filesz = be(data, p + 8, 4), be(data, p + 16, 4)
        if p_type != 1:
            continue
        segs.append((p_vaddr, p_filesz))
        for j in range(p_filesz):
            a = p_vaddr + j
            if a in mem:
                die('overlapping PT_LOAD bytes at 0x%x' % a)
            mem[a] = data[p_offset + j]

    def covered(lo, hi):
        return any(sa < hi and lo < sa + sz for (sa, sz) in segs)

    # Map witnesses: slot -> (addr, tag, name)
    rows = {}
    text_base = None
    for line in open(map_path):
        t = line.split()
        if len(t) >= 4 and t[0] == 'IRQ':
            slot_no = int(t[1])
            # A repeated slot number must fail loudly: with a plain dict
            # store a duplicate plus a missing slot would still pass the
            # len(rows) == COUNT check below.
            if slot_no in rows:
                die('duplicate IRQ map row for slot %d' % slot_no)
            rows[slot_no] = (int(t[2], 16), t[3],
                             t[4] if len(t) > 4 else '')
        # The fixture is the only input with a plain .text section; the map's
        # section row gives its independent load address (R4: precise handler
        # entry evidence, not derived from the EJMP bytes themselves).
        m = re.search(r'\.text (0x[0-9a-f]+) \+0x[0-9a-f]+', line)
        if m and text_base is None:
            text_base = int(m.group(1), 16)
    if len(rows) != COUNT:
        die('expected %d IRQ map rows, got %d' % (COUNT, len(rows)))

    defaults = []
    registered_targets = {}
    ejmps = 0
    for slot in range(COUNT):
        base = BASE + STRIDE * slot
        if rows[slot][0] != base:
            die('map row %d address 0x%x violates the formula' %
                (slot, rows[slot][0]))
        if slot in NON_LEGAL:
            if rows[slot][1] != ('SYSTEM' if slot in SYSTEM else 'RESERVED'):
                die('map row %d must be a reserved/system row' % slot)
            if any(a in mem for a in range(base, base + STRIDE)):
                die('non-legal slot %d has payload bytes' % slot)
            if covered(base, base + STRIDE):
                die('non-legal slot %d is covered by a PT_LOAD' % slot)
            continue
        tag = 'ISR' if slot in registered else 'DEFAULT'
        if rows[slot][1] != tag:
            die('map row %d tag %s, expected %s' % (slot, rows[slot][1], tag))
        if mem[base] != 0x8A:
            die('legal slot %d does not start with the EJMP opcode' % slot)
        ejmps += 1
        target = (mem[base + 1] << 16) | (mem[base + 2] << 8) | mem[base + 3]
        if any(a in mem for a in range(base + 4, base + STRIDE)):
            die('legal slot %d tail carries payload' % slot)
        if covered(base + 4, base + STRIDE):
            die('legal slot %d tail is covered by a PT_LOAD' % slot)
        if slot in registered:
            registered_targets[slot] = target
        else:
            defaults.append(target)
    if ejmps != COUNT - len(NON_LEGAL):
        die('expected %d EJMP vectors, saw %d' %
            (COUNT - len(NON_LEGAL), ejmps))
    # R4: every registered slot's EJMP must hit its own handler entry. The
    # expected entry comes from an independent chain: the fixture emits one
    # 41-byte handler per registered slot in ascending slot order followed by
    # _main, and the map's .text section row supplies the load base.
    if registered:
        if text_base is None:
            die('no fixture .text section row found in the map')
        if text_base not in mem:
            die('fixture .text base 0x%x has no payload' % text_base)
        for k, slot in enumerate(sorted(registered)):
            expected = text_base + HANDLER_STRIDE * k
            if registered_targets[slot] != expected:
                die('registered slot %d EJMP target 0x%x is not its handler '
                    'entry 0x%x' % (slot, registered_targets[slot], expected))
    # A fully-registered table is a legal final state: there are no
    # unregistered legal slots, hence no default vectors to check.
    if defaults:
        if len(set(defaults)) != 1:
            die('unregistered legal slots do not share one default target')
        default_target = defaults[0]
        if tuple(mem[default_target + i]
                 for i in range(4)) != tuple(DEFAULT_BYTES):
            die('default target 0x%x does not hold the frozen fail-stop word'
                % default_target)
        for slot, target in registered_targets.items():
            if target == default_target:
                die('registered slot %d jumps to the default entry' % slot)
            if target not in mem:
                die('registered slot %d target 0x%x has no payload' %
                    (slot, target))

    # Reset: exactly a 3-byte ljmp abutting the vector base, jumping into the
    # BOOT range.
    if entry + 3 != BASE:
        die('entry 0x%x is not a 3-byte reset abutting 0x%x' % (entry, BASE))
    if mem[entry] != 0x02:
        die('reset does not begin with the ljmp opcode')
    # J16 stores only the low 16 bits; the 64K bank is the instruction's own
    # (the linker enforces bank equality), so reconstruct the full target.
    field = (mem[entry + 1] << 8) | mem[entry + 2]
    bank = (entry + 3) & 0xFF0000
    boot = bank | field
    if boot < BOOT_FLOOR:
        die('reset J16 target 0x%x is below the BOOT floor 0x%x' %
            (boot, BOOT_FLOOR))
    if boot not in mem:
        die('reset J16 target 0x%x has no payload' % boot)

    print('ISR image check PASS')
    sys.exit(0)


if __name__ == '__main__':
    main()
