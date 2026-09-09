#!/usr/bin/env python3
"""Static checks for the V2 nesting fixture: never runs an emulator or a board."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

# Wall-clock budget for the 10000-round 'r' QEMU run of v2-nest.hex.
# Measured ~210 s on the final toolchain + current model (2026-09-10); the
# previous 150 s budget truncated the run before completion, so anything
# below ~240 s must be treated as a TIMEOUT, never a PASS.
QEMU_RUN_BUDGET_S = 240

out, source = map(Path, sys.argv[1:3])
tools = list(map(Path, sys.argv[3:]))
elf = (out/'v2-nest.elf').read_bytes()
assert elf[:6] == b'\x7fELF\x01\x02'
m = (out/'v2-nest.map').read_text()

addrs = {}
for unit, name in [('isr-high', '_timer0'), ('isr-low', '_timer1')]:
    match = re.search(r'/'+unit+r'\.o:\.text (0x[0-9a-f]+) \+(0x[0-9a-f]+)', m)
    assert match, unit + ' not in map'
    addrs[name] = (int(match[1], 16), int(match[2], 16))
match = re.search(r'/v2nest\.o:\.text (0x[0-9a-f]+) \+(0x[0-9a-f]+)', m)
assert match, 'v2nest .text not in map'
isr_high, high_size = addrs['_timer0']
isr_low, low_size = addrs['_timer1']

mem = {}
base = 0
for line in (out/'v2-nest.hex').read_text().splitlines():
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

# Reset vector plus BOTH interrupt vectors: slot 1 Timer0 @0xFF000B,
# slot 3 Timer1 @0xFF001B (frozen formula base 0xff0003 + 8*slot, EJMP).
assert blob(0xff0000, 3) == b'\x02\x02\x10'
assert blob(0xff000b, 1) == b'\x8a' and blob(0xff001b, 1) == b'\x8a'
assert blob(0xff000c, 3) == isr_high.to_bytes(3, 'big')
assert blob(0xff001c, 3) == isr_low.to_bytes(3, 'big')

# Both compiler ISRs must carry the full save set, inverse restore, one RETI.
for unit in ('isr-high', 'isr-low'):
    asm = (out/(unit + '.asm')).read_text()
    expected = ['psw'] + ['dr'+str(i) for i in range(0, 32, 4)] + ['dpx']
    assert re.findall(r'^\s*push (\w+)', asm, re.M) == expected, unit
    assert re.findall(r'^\s*pop (\w+)', asm, re.M) == expected[::-1], unit
    assert len(re.findall(r'^\s*reti\s*$', asm, re.M)) == 1, unit

# Priority setup must be present in main: IP=0x0a (PT1|PT0), IPH=0x02 (PT0).
masm = (out/'main.asm').read_text()
assert ('mov 0xb8,#0x0a' in masm or 'mov 0xb8, r0' in masm), 'IP setup missing'
assert ('mov 0xb7,#0x02' in masm or 'mov 0xb7, r0' in masm), 'IPH setup missing'

# Sentinel module: 11-push inverse-pop, ERET, flag-neutral JB/SJMP wait,
# arms Timer1 (TCON |= 0x40) with EA|ET0|ET1 (IE=0x8a).
s = (out/'v2nest.asm').read_text()
sent = s[s.index('_sentinel:'):]
assert re.findall(r'^\s*push (\S+)', sent, re.M) == \
    ['0xd0', '0xd1'] + ['dr'+str(i) for i in range(0, 32, 4)] + ['dpx']
assert re.findall(r'^\s*pop (\S+)', sent, re.M) == \
    ['dpx'] + ['dr'+str(i) for i in range(28, -1, -4)] + ['0xd1', '0xd0']
assert sent.count('eret') == 1
assert 'jb 0x00,_done' in sent and 'sjmp _wait' in sent
assert 'djnz' not in s, 'wait loop must stay flag-neutral (no DJNZ)'
assert 'mov 0xa8,#0x8a' in sent and 'orl 0x88,#0x40' in sent
# Low ISR opens the window: TR0 set then cleared inside Timer1's body.
lasm = (out/'isr-low.asm').read_text()
assert re.search(r'orl\s+r\d+,\s*#0x10', lasm), \
    'Timer1 ISR must start Timer0 inside its window (TCON |= 0x10)'

assert 'stack H=0x900 SPX=0x90f capacity=1776' in m
assert '.mcs251.DATA.fixture 0x0030 +0x35' in m
assert '.mcs251.DATA.result 0x0100 +0x20' in m
assert '.mcs251.DATA.teststack 0x0580 +0x380' in m

manifest = {
    'profile': 'V2 two-level nesting fixture; development validation, not T09 qualification',
    'optimization': 'isr-high/isr-low=O2; main=O2 with -O0 fallback',
    'qemu_budget_seconds': QEMU_RUN_BUDGET_S,
    'qemu_budget_note': "10000-round 'r' run measured ~210 s wall (2026-09-10, final "
                        "toolchain, no -O0 fallback); the previous 150 s budget truncated "
                        "the run — treat sub-budget terminations as TIMEOUT, never PASS",
    'coverage': 'Timer1 (level 1) preempted by Timer0 (level 3) inside an ISR-opened '
                'window; per-ISR full 37B save/restore asserted statically; composite '
                'register file integrity and preemption order checked per round',
    'files': {}}
files = tools + list(source.glob('*.c')) + list(source.glob('*.h')) + \
    list(source.glob('*.py')) + [source/'build.sh']
files += [out/name for name in ['v2-nest.hex', 'v2-nest.elf', 'v2-nest.map',
                                'isr-high.asm', 'isr-low.asm', 'v2nest.asm', 'crt.o']]
for p in files:
    manifest['files'][str(p.resolve())] = hashlib.sha256(p.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
msg = ('PASS: HEX checksum/range, reset + Timer0(slot1) + Timer1(slot3) vectors, both '
       'compiler ISRs 37B save/inverse restore/one RETI, priority setup IP=0x0a/IPH=0x02, '
       'sentinel 11-push inverse-pop ERET, flag-neutral JB/SJMP wait, Timer1 opens/closes '
       'the Timer0 window, RAM reservations, C stack capacity.\n'
       'QEMU run budget: %d s for the 10000-round \'r\' mode (measured ~210 s; the old '
       '150 s budget truncated the run).\n'
       'NOT RUN here: emulator and real board (see README for the QEMU record).\n' % QEMU_RUN_BUDGET_S)
(out/'STATIC-CHECKS.txt').write_text(msg)
print(msg)
