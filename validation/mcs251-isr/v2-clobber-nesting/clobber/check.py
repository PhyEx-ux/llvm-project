#!/usr/bin/env python3
"""Static checks for the V2 clobber fixture: never runs an emulator or a board."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
out, source = map(Path, sys.argv[1:3])
tools = list(map(Path, sys.argv[3:]))
elf = (out/'v2-clob.elf').read_bytes()
assert elf[:6] == b'\x7fELF\x01\x02'
m = (out/'v2-clob.map').read_text()

# ISR / helper function addresses from the map (final ELF has no symbols).
addrs = {}
for unit, name in [('isr', '_timer0')]:
    match = re.search(r'/'+unit+r'\.o:\.text (0x[0-9a-f]+) \+(0x[0-9a-f]+)', m)
    assert match, unit + ' not in map'
    addrs[name] = (int(match[1], 16), int(match[2], 16))
# The wrapped assembly module: _sentinel at its base, _clobber_hi behind it.
match = re.search(r'/v2clob\.o:\.text (0x[0-9a-f]+) \+(0x[0-9a-f]+)', m)
assert match, 'v2clob .text not in map'
blob_base = int(match[1], 16)
isr, isr_size = addrs['_timer0']

# HEX -> flat memory image.
mem = {}
base = 0
for line in (out/'v2-clob.hex').read_text().splitlines():
    b = bytes.fromhex(line[1:])
    assert sum(b) % 256 == 0 and len(b) == b[0] + 5
    if b[3] == 4:
        base = int.from_bytes(b[4:-1], 'big') << 16
    elif b[3] == 0:
        a = base + int.from_bytes(b[1:3], 'big')
        for i, v in enumerate(b[4:-1]):
            assert a + i not in mem
            mem[a+i] = v
assert min(mem) >= 0xff0000 and max(mem) <= 0xffffff
def blob(a, n):
    return bytes(mem[a+i] for i in range(n))

# Reset vector: 3-byte ljmp into BOOT.  Timer0 slot 1: EJMP to _timer0.
assert blob(0xff0000, 3) == b'\x02\x05\x00'
assert blob(0xff000b, 1) == b'\x8a'
assert blob(0xff000c, 3) == isr.to_bytes(3, 'big')

# Compiler ISR structure: full save set, inverse restore, exactly one RETI,
# and the active clobber call.
asm = (out/'isr.asm').read_text()
expected = ['psw'] + ['dr'+str(i) for i in range(0, 32, 4)] + ['dpx']
assert re.findall(r'^\s*push (\w+)', asm, re.M) == expected
assert re.findall(r'^\s*pop (\w+)', asm, re.M) == expected[::-1]
assert len(re.findall(r'^\s*reti\s*$', asm, re.M)) == 1
assert 'ecall __clobber_hi' in asm or 'ecall _clobber_hi' in asm

# Hand-written module structure.
s = (out/'v2clob.asm').read_text()
sent = s[s.index('_sentinel:'):s.index('.globl _clobber_hi')]
assert re.findall(r'^\s*push (\S+)', sent, re.M) == \
    ['0xd0', '0xd1'] + ['dr'+str(i) for i in range(0, 32, 4)] + ['dpx']
assert re.findall(r'^\s*pop (\S+)', sent, re.M) == \
    ['dpx'] + ['dr'+str(i) for i in range(28, -1, -4)] + ['0xd1', '0xd0']
assert sent.count('eret') == 1
assert 'jb 0x00,_done' in sent and 'sjmp _wait' in sent
assert 'djnz' not in s, 'wait loop must stay flag-neutral (no DJNZ)'
clob = s[s.index('_clobber_hi:'):]
assert 'push' not in clob and 'pop' not in clob, \
    'clobber_hi must not save what it clobbers (that is the ISR under test)'
for a in range(0x70, 0x80):
    assert ('mov 0x%02x,r5' % a) in clob, 'missing pattern window store 0x%02x' % a
for dr in ('dr16', 'dr20', 'dr24', 'dr28'):
    assert ('mov dr0,' + dr) in clob, 'missing DR-view read-back of ' + dr
for dr in ('dr16', 'dr20', 'dr24', 'dr28'):
    assert re.search(r'mov %s,0x7[0-9a-f]' % dr, clob), 'missing dword direct8 load of ' + dr
assert 'mov 0x84,r5' in clob, 'DPXL not actively clobbered'
assert clob.count('eret') == 1

# The ISR calls the clobber at its linked address (EJMP-free direct ECALL).
clob_addr = None
for line in (out/'v2clob.lst').read_text().splitlines():
    if '_clobber_hi:' in line:
        clob_addr = blob_base + int(line.split()[0], 16)
assert clob_addr is not None
assert b'\x9a' + clob_addr.to_bytes(3, 'big') in blob(isr, isr_size), \
    'ISR does not contain the linked clobber_hi ECALL'

# Reserved RAM regions and C stack capacity from the map.
assert 'stack H=0x900 SPX=0x90f capacity=1776' in m
assert '.mcs251.DATA.fixture 0x0030 +0x35' in m
assert '.mcs251.DATA.patwin 0x0070 +0x10' in m
assert '.mcs251.DATA.result 0x0100 +0x10' in m
assert '.mcs251.DATA.teststack 0x0580 +0x380' in m

manifest = {
    'profile': 'V2 active-clobber fixture; development validation, not T09 qualification',
    'optimization': 'isr=O2; main=O2 with -O0 fallback (BRCC relaxation backend state)',
    'coverage': 'R0-R31 actively clobbered inside the ISR via ABI-legal helper; '
                'DPXL + PSW clobbered; sentinel restore checked per round; '
                'flags raw-reported only (see V1 fixture for PSW1 attribution)',
    'files': {}}
files = tools + list(source.glob('*.c')) + list(source.glob('*.h')) + \
    list(source.glob('*.py')) + [source/'build.sh']
files += [out/name for name in ['v2-clob.hex', 'v2-clob.elf', 'v2-clob.map',
                                'isr.asm', 'v2clob.asm', 'v2clob.lst', 'crt.o']]
for p in files:
    manifest['files'][str(p.resolve())] = hashlib.sha256(p.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
msg = ('PASS: HEX checksum/range, reset+Timer0 vectors, compiler ISR 37B save/inverse '
       'restore/one RETI/clobber ECALL at linked address, sentinel 11-push inverse-pop '
       'ERET, flag-neutral JB/SJMP wait (DJNZ-free), 16 active R16-R31 writes with DR-view '
       'read-back, DPXL clobber, no push/pop inside clobber_hi, RAM reservations, C stack '
       'capacity.\nNOT RUN here: emulator and real board (see README for the QEMU record).\n')
(out/'STATIC-CHECKS.txt').write_text(msg)
print(msg)
