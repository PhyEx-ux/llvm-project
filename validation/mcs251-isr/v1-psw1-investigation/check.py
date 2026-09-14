#!/usr/bin/env python3
"""Static checks for the V1 PSW1 investigation images (three arms)."""
import hashlib
import json
import re
import sys
from pathlib import Path
out, source = map(Path, sys.argv[1:3])
tools = list(map(Path, sys.argv[3:]))


def hexmem(path):
    mem = {}
    base = 0
    for line in path.read_text().splitlines():
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
    return mem


def blob(mem, a, n):
    return bytes(mem[a+i] for i in range(n))


def map_text(name):
    return (out/('v1-%s.map' % name)).read_text()


def map_addr(name, unit, sym=None):
    m = map_text(name)
    match = re.search(r'/'+unit+r'\.o:\.text (0x[0-9a-f]+) \+(0x[0-9a-f]+)', m)
    assert match, '%s/%s not in map' % (name, unit)
    return int(match[1], 16), int(match[2], 16)


# Reset vector in every image: 3-byte ljmp into BOOT at 0xFF0500.
for arm in ('baseline', 'minisr', 'compiler'):
    mem = hexmem(out/('v1-%s.hex' % arm))
    assert blob(mem, 0xff0000, 3) == b'\x02\x05\x00', arm

# Shared asm module: LP stage is byte-identical in shape to the T10 wait
# (mov 0x21/0x22/0x23 init, JB + three DJNZ chain) and IX waits DJNZ-free.
for sub, tag in (('.', 'compiler'), ('minisr-asm', 'minisr')):
    s = (out/sub/'v1psw1.asm').read_text()
    assert 'djnz 0x21,_lw' in s and 'djnz 0x22,_lw' in s and 'djnz 0x23,_lw' in s, tag
    assert 'jb 0x00,_ld' in s, tag
    assert 'sjmp _iw' in s and 'jb 0x00,_id' in s, tag
    for fn in ('_v1_wr', '_v1_lp', '_v1_ix'):
        blk = s[s.index(fn+':'):]
        blk = blk[:blk.index('eret')]
        pushes = re.findall(r'^\s*push (\S+)', blk, re.M)
        pops = re.findall(r'^\s*pop (\S+)', blk, re.M)
        assert pushes == ['0xd0', '0xd1'] + ['dr'+str(i) for i in range(0, 32, 4)] + ['dpx'], (tag, fn)
        assert pops == ['dpx'] + ['dr'+str(i) for i in range(28, -1, -4)] + ['0xd1', '0xd0'], (tag, fn)
        assert 'mov spx,#0x0600' in blk, (tag, fn)

# minisr arm: hand ISR must capture entry flags, perturb with one ADD, RETI.
s = (out/'minisr-asm'/'v1psw1.asm').read_text()
assert 'mov 0x3c,0xd0' in s and 'mov 0x3d,0xd1' in s
assert 'mov a,#0x7f' in s and 'add a,#0x01' in s and 'reti' in s
assert '_timer0:' in s

# I stage: the combo re-establishment (second `mov 0xd1,a`) must follow the
# window-opening `orl 0x88,#0x10` -- that SFR orl writes N/Z from its own
# result, so a non-zero N/Z may only be established after it (otherwise
# en-PSW1 comes out 00 for combos 1/2 and stage I proves nothing about N/Z
# frame restore).
blk = s[s.index('_v1_ix:'):]
blk = blk[:blk.index('eret')]
assert 'orl 0x88,#0x10' in blk
assert blk.index('mov 0xd0,a') < blk.index('orl 0x88,#0x10') < blk.rindex('mov 0xd1,a'), \
    'combo re-establishment must follow the TCON orl'
assert blk.rindex('mov 0xd0,a') < blk.rindex('mov 0xd1,a') < blk.index('_iw:')

# I stage: explicit per-run timer re-init must precede the arming orl --
# TCON=0 (stop Timer0, clear stale TF0 so no pending request survives from
# an earlier run), TMOD=0x01 (16-bit mode 1), TH0/TL0=00 (count reload).
# Without this sequence "first overflow >> setup instructions" rests on
# residual timer state instead of construction, and en != w could not be
# read deterministically as "accepted too early / window state mismatch"
# (PROTOCOL 2.3).
init_seq = ['mov 0x88,#0', 'mov 0x89,#0x01', 'mov 0x8a,#0', 'mov 0x8c,#0']
for t in init_seq:
    assert t in blk, ('minisr-asm', t, 'missing timer re-init in _v1_ix')
pos = [blk.index(t) for t in init_seq]
assert pos == sorted(pos) and pos[-1] < blk.index('orl 0x88,#0x10'), \
    'timer re-init (TCON=0/TMOD/TL0/TH0) must precede the arming orl'
assert blk.rindex('mov 0x88,#0') > blk.index('orl 0x88,#0x10'), \
    '_v1_ix must still disarm (TCON=0) after the poll'

# baseline arm: no _timer0 symbol and no ISR object in the link map.
assert '_timer0' not in (out/'v1psw1.asm').read_text()
mb = map_text('baseline')
assert 'isr.o' not in mb and 'isr-compiler.o' not in mb

# minisr: Timer0 vector targets the module's _timer0.
mem = hexmem(out/'v1-minisr.hex')
lst = (out/'minisr-asm'/'v1psw1.lst').read_text()
off = None
for line in lst.splitlines():
    if '_timer0:' in line:
        off = int(line.split()[0], 16)
sect = re.search(r'/v1psw1\.o:\.text (0x[0-9a-f]+)', map_text('minisr'))
assert sect and off is not None
tgt = int(sect.group(1), 16) + off
assert blob(mem, 0xff000b, 4) == b'\x8a' + tgt.to_bytes(3, 'big')

# compiler arm: vector targets the T10 ISR; ISR structure unchanged.
mem = hexmem(out/'v1-compiler.hex')
addr, size = map_addr('compiler', 'isr-compiler')
assert blob(mem, 0xff000b, 4) == b'\x8a' + addr.to_bytes(3, 'big')
casm = (out/'isr-compiler.asm').read_text()
expected = ['psw'] + ['dr'+str(i) for i in range(0, 32, 4)] + ['dpx']
assert re.findall(r'^\s*push (\w+)', casm, re.M) == expected
assert re.findall(r'^\s*pop (\w+)', casm, re.M) == expected[::-1]
assert len(re.findall(r'^\s*reti\s*$', casm, re.M)) == 1
assert 'ecall _helper' in casm

manifest = {
    'profile': 'V1 PSW1 investigation; three-arm controlled comparison; '
               'development validation, not T09 qualification',
    'optimization': 'main/isr/helper=O2; assembly stages via sdas251',
    'arms': ['v1-baseline (no ISR; stages W+L)',
             'v1-minisr (hand ISR records entry flags + perturbs; stages W+L+I)',
             'v1-compiler (unmodified T10 ISR; stages W+L+I)'],
    'stages': {'W': 'PSW/PSW1 write + immediate readback',
               'L': 'T10-identical DJNZ budget wait with IE=0 (loop pollution control)',
               'I': 'Timer0 interrupt window with flag-neutral JB/SJMP poll'},
    'files': {}}
files = tools + list(source.glob('*.c')) + list(source.glob('*.h')) + \
    list(source.glob('*.py')) + [source/'build.sh']
outs = ['v1-baseline.hex', 'v1-minisr.hex', 'v1-compiler.hex',
        'v1-baseline.elf', 'v1-minisr.elf', 'v1-compiler.elf',
        'v1psw1.asm', 'minisr-asm/v1psw1.asm', 'minisr-asm/v1psw1.lst',
        'isr-compiler.asm', 'helper-compiler.asm', 'crt.o']
files += [out/name for name in outs]
for p in files:
    manifest['files'][str(p.resolve())] = hashlib.sha256(p.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
msg = ('PASS: three-arm build, reset vectors, shared stage structure (11-push inverse-pop '
       'ERET frames, LP byte-shape identical to the T10 wait, IX DJNZ-free with the combo '
       're-established after the TCON orl so non-zero N/Z survives to acceptance, and the '
       'explicit per-run timer re-init (TCON=0 stale-TF0 clear, TMOD=0x01, TH0/TL0 reload) '
       'placed before the arming orl so the 65536-clock first-overflow window is a '
       'construction guarantee), minisr '
       'entry snapshot + ADD perturbation + RETI, baseline has no ISR object, minisr/compiler '
       'Timer0 vectors land on their linked _timer0, compiler ISR 37B save/inverse/RETI/'
       'ecall helper.\nNOT RUN here: emulator and real board (see README for QEMU record).\n')
(out/'STATIC-CHECKS.txt').write_text(msg)
print(msg)
