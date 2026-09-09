#!/usr/bin/env python3
"""Static checks only: never launches an emulator or accesses a board."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
out, source = map(Path, sys.argv[1:3])
tools = list(map(Path, sys.argv[3:]))
elf = (out/'t10-g12.elf').read_bytes()
assert elf[:6] == b'\x7fELF\x01\x02'
shoff = struct.unpack_from('>I', elf, 32)[0]
entsize, count, strindex = struct.unpack_from('>HHH', elf, 46)
sections = [struct.unpack_from('>10I', elf, shoff+i*entsize) for i in range(count)]
def data(s): return elf[s[4]:s[4]+s[5]]
symbols = {}
for s in sections:
    if s[1] == 2:
        strings = data(sections[s[6]])
        for pos in range(s[4],s[4]+s[5],s[9]):
            name, value, size, info, other, index = struct.unpack_from('>IIIBBH',elf,pos)
            name = strings[name:].split(b'\0')[0].decode()
            if index: symbols[name]=(value,size)
mem={};base=0
for line in (out/'t10-g12.hex').read_text().splitlines():
    b=bytes.fromhex(line[1:]);assert sum(b)%256==0 and len(b)==b[0]+5
    if b[3]==4: base=int.from_bytes(b[4:-1],'big')<<16
    elif b[3]==0:
        a=base+int.from_bytes(b[1:3],'big')
        for i,v in enumerate(b[4:-1]): assert a+i not in mem;mem[a+i]=v
assert min(mem)>=0xff0000 and max(mem)<=0xffffff
def blob(a,n):return bytes(mem[a+i] for i in range(n))
m=(out/'t10-g12.map').read_text()
for unit, name in [('isr','_timer0'),('helper','_helper')]:
    match=re.search(r'/'+unit+r'\.o:\.text (0x[0-9a-f]+) \+(0x[0-9a-f]+)',m)
    assert match
    symbols[name]=(int(match[1],16),int(match[2],16))
isr=symbols['_timer0'][0]; helper=symbols['_helper'][0]
assert blob(0xff000b,4)==b'\x8a'+isr.to_bytes(3,'big')
assert blob(0xff0000,3)==b'\x02\x02\x10'
asm=(out/'isr.asm').read_text()
expected=['psw']+['dr'+str(i) for i in range(0,32,4)]+['dpx']
assert re.findall(r'^\s*push (\w+)',asm,re.M)==expected
assert re.findall(r'^\s*pop (\w+)',asm,re.M)==expected[::-1]
assert len(re.findall(r'^\s*reti\s*$',asm,re.M))==1
assert 'ecall _helper' in asm
assert b'\x9a'+helper.to_bytes(3,'big') in blob(isr,symbols['_timer0'][1])
assert blob(isr+symbols['_timer0'][1]-1,1)==b'\x32'
assert blob(helper+symbols['_helper'][1]-1,1)==b'\xaa' # ERET
assert asm.count('inc spx, #0x4')==2 and asm.count('dec spx, #0x4')==2
assert (out/'helper.asm').read_text().count('inc spx, #0x4')==1
m=(out/'t10-g12.map').read_text()
assert 'stack H=0x900 SPX=0x90f capacity=1776' in m
assert '.mcs251.DATA.fixture 0x0030 +0x35' in m
assert '.mcs251.DATA.result 0x0100 +0x10' in m
assert '.mcs251.DATA.teststack 0x0580 +0x380' in m
# Sentinel is linked position-independent: no CALL or absolute internal jumps.
s=(out/'sentinel.asm').read_text()
assert not re.search(r'^\s*(?:[ela]?call|[ela]jmp)\b',s,re.M)
assert s.count('push ')==s.count('pop ')==11
assert 'mov spx,#0x0600' in s
assert 'jb 0x00,_done' in s
assert len(re.findall(r'^mov 0x[45][0-9a-f],r',s,re.M))==32
manifest={'profile':'G12 hardware development validation; not T09 frozen qualification',
          'optimization':'O2', 'emulator':'NOT_RUN: user request', 'real_hardware':'NOT_RUN',
          'dynamic_negative_tests':'NOT_RUN', 'static_checks':'PASS',
          'coverage':'R0-R31, DPL/DPH/DPXL, SP, guards, helper result, shared byte; flags NOT_TESTED',
          'test_stack_peak_budget':{'hardware':4,'isr_save':37,'isr_local':8,'ecall':3,'helper_local':4,'total':56},
          'files':{}}
files=tools+list(source.glob('*.c'))+list(source.glob('*.h'))+list(source.glob('*.py'))+[source/'build.sh']
files += [out/name for name in ['t10-g12.hex','t10-g12.elf','t10-g12.map','isr.asm','helper.asm','sentinel.asm','sentinel.lst','crt.o']]
for p in files:
    manifest['files'][str(p.resolve())]=hashlib.sha256(p.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'STATIC-CHECKS.txt').write_text('PASS: HEX checksum/range, ELF header and map addresses, Timer0 vector, 37B save/inverse restore, 8B ISR local, helper ECALL/ERET, 4B helper local, RAM reservations, C stack capacity, sentinel snapshot structure.\nNOT RUN: all dynamic tests, emulator, real board.\n')
print((out/'STATIC-CHECKS.txt').read_text())
