#!/usr/bin/env python3
"""Generate a relocation-free assembly fixture and its ELF wrapper."""
from pathlib import Path
import subprocess
import sys
out = Path(sys.argv[1])
sdas, sdld, yaml2obj = sys.argv[2:]
lines = ['.module sentinel', '.source', '.area CSEG (CODE)', '_sentinel:']
# C context stays on the CRT stack; the interrupt uses a separate 0600 stack.
for reg in ['0xd0', '0xd1'] + ['dr'+str(i) for i in range(0,32,4)] + ['dpx']:
    lines.append('push '+reg)
lines += ['mov 0x35,0x81', 'mov 0x36,0x85', 'mov spx,#0x0600',
          'mov 0x37,0x81', 'mov 0x38,0x85', 'mov 0x21,#0', 'mov 0x22,#0',
          'mov 0x23,#0x40']
for i in range(16):
    lines.append(f'mov r{i},#0x{(0x41+i*5)&255:02x}')
for i in range(16, 32, 4):
    v = int.from_bytes(bytes((0x41+j*5)&255 for j in range(i,i+4)), 'big')
    lines += [f'mov dr{i},#0x{v & 65535:04x}', f'movh dr{i},#0x{v >> 16:04x}']
lines += ['mov 0x82,#0x56', 'mov 0x83,#0x04', 'mov 0x84,#0',
          'mov 0x39,0x82', 'mov 0x3a,0x83', 'mov 0x3b,0x84',
          'mov 0xd0,#0', 'mov 0xd1,#0',
          'mov 0x3c,0xd0', 'mov 0x3d,0xd1',
          'mov 0xa8,#0x82', 'orl 0x88,#0x10',
          '_wait:', 'jb 0x00,_done', 'djnz 0x21,_wait',
          'djnz 0x22,_wait', 'djnz 0x23,_wait', '_done:',
          'mov 0x3e,0xd0', 'mov 0x3f,0xd1']
# Direct stores do not need a scratch register or alter flags.
for i in range(16):
    lines.append(f'mov 0x{0x40+i:02x},r{i}')
# Low registers are already captured, so DR0 can ferry high register bytes.
for i in range(16, 32, 4):
    lines.append(f'mov dr0,dr{i}')
    for j in range(4):
        lines.append(f'mov 0x{0x40+i+j:02x},r{j}')
lines += ['mov 0x60,0x81', 'mov 0x61,0x85',
          'mov 0x62,0x82', 'mov 0x63,0x83', 'mov 0x64,0x84',
          'mov 0xa8,#0', 'mov 0x88,#0', 'mov 0x85,0x36', 'mov 0x81,0x35']
for reg in ['dpx'] + ['dr'+str(i) for i in range(28,-1,-4)] + ['0xd1','0xd0']:
    lines.append('pop '+reg)
lines.append('eret')
(out/'sentinel.asm').write_text('\n'.join(lines)+'\n')
subprocess.run([sdas,'-los',str(out/'sentinel.rel'),str(out/'sentinel.asm')],check=True)
(out/'sentinel.lk').write_text('-i '+str(out/'sentinel.ihx')+'\n-b CSEG=0\n'+str(out/'sentinel.rel')+'\n-e\n')
subprocess.run([sdld,'-f',str(out/'sentinel.lk')],check=True)
mem={}
for line in (out/'sentinel.ihx').read_text().splitlines():
    b=bytes.fromhex(line[1:]); assert sum(b)%256==0
    if b[3]==0:
        a=int.from_bytes(b[1:3],'big')
        mem.update({a+i:v for i,v in enumerate(b[4:-1])})
code=bytes(mem[i] for i in range(len(mem)))
yaml='''--- !ELF
FileHeader:
  Class: ELFCLASS32
  Data: ELFDATA2MSB
  Type: ET_REL
  Machine: EM_MCS251
  Flags: [ EF_MCS251_ABI_V1 ]
Sections:
  - Name: .text
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 1
    Content: '%s'
  - Name: .mcs251.DATA.fixture
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Size: 0x35
  - Name: .mcs251.DATA.result
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Size: 0x10
  - Name: .mcs251.DATA.teststack
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Size: 0x380
  - Name: .note.mcs251.abi
    Type: SHT_NOTE
    AddressAlign: 4
    Notes:
      - Name: MCS251
        Type: 1
        Desc: '000000010000000100000000000000020000F3FF000000070000000000000000'
Symbols:
  - Name: _sentinel
    Type: STT_FUNC
    Binding: STB_GLOBAL
    Section: .text
    Value: 0
    Size: %d
''' % (code.hex(),len(code))
(out/'sentinel.yaml').write_text(yaml)
subprocess.run([yaml2obj,str(out/'sentinel.yaml'),'-o',str(out/'sentinel.o')],check=True)
