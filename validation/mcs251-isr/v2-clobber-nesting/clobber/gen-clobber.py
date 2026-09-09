#!/usr/bin/env python3
"""Generate the relocation-free assembly module for the V2 clobber fixture.

Two functions are emitted into one ELF object (same wrapper scheme as the
frozen realhw-demo gen-sentinel.py):

  _sentinel    -- register sentinels + flag-neutral wait + post-round
                  snapshot for the interrupt round.
  _clobber_hi  -- ABI-legal active clobber of R16-R31, DPXL and PSW.

Only instruction forms proven in this repo are used: classic 8051 core ops,
the native dword direct8 move (mov drN,dir) for runtime data into R16-R31,
mov drN,#imm16 / movh drN,#imm16, mov drD,drS ferry, jb/sjmp polling (no
DJNZ anywhere, so the wait loop cannot touch N/Z), push/pop, ecall/eret.
No absolute jumps or external calls inside the module, so the machine code
can be wrapped relocation-free (same constraint as realhw-demo).
"""
from pathlib import Path
import subprocess
import sys

out = Path(sys.argv[1])
sdas, sdld, yaml2obj = sys.argv[2:]

lines = ['.module v2clob', '.source', '.area CSEG (CODE)']

# ---------------- _sentinel ----------------
lines.append('.globl _sentinel')
lines.append('_sentinel:')
# Save the C environment: PSW, PSW1, the full register file, DPX.
for reg in ['0xd0', '0xd1'] + ['dr' + str(i) for i in range(0, 32, 4)] + ['dpx']:
    lines.append('push ' + reg)
lines += ['mov 0x35,0x81', 'mov 0x36,0x85',      # saved C stack pointer
          'mov spx,#0x0600',
          'mov 0x37,0x81', 'mov 0x38,0x85',      # test stack baseline
          'mov 0x21,#0', 'mov 0x22,#0', 'mov 0x23,#0',  # scratch, unused
          'mov 0x82,#0x56', 'mov 0x83,#0x04', 'mov 0x84,#0',  # DPTR + DPXL
          'mov 0x39,0x82', 'mov 0x3a,0x83', 'mov 0x3b,0x84']
# R0-R31 sentinel pattern 0x41 + 5*i (frozen, same formula as the T10 demo).
for i in range(16):
    lines.append(f'mov r{i},#0x{(0x41 + i * 5) & 255:02x}')
for i in range(16, 32, 4):
    v = int.from_bytes(bytes((0x41 + j * 5) & 255 for j in range(i, i + 4)), 'big')
    lines += [f'mov dr{i},#0x{v & 65535:04x}', f'movh dr{i},#0x{v >> 16:04x}']
# Flag baseline: force PSW/PSW1 to zero, snapshot, then arm Timer0.
lines += ['mov 0xd0,#0', 'mov 0xd1,#0',
          'mov 0x3c,0xd0', 'mov 0x3d,0xd1',
          'mov 0xa8,#0x82', 'orl 0x88,#0x10']
# Flag-neutral wait: JB/SJMP only.  No DJNZ/INC/logical op touches N/Z here.
lines += ['_wait:', 'jb 0x00,_done', 'sjmp _wait', '_done:',
          'mov 0x3e,0xd0', 'mov 0x3f,0xd1']
# Direct stores do not need a scratch register and do not alter flags.
for i in range(16):
    lines.append(f'mov 0x{0x40 + i:02x},r{i}')
# DR0 ferries the high register bytes out of the DR view.
for i in range(16, 32, 4):
    lines.append(f'mov dr0,dr{i}')
    for j in range(4):
        lines.append(f'mov 0x{0x40 + i + j:02x},r{j}')
lines += ['mov 0x60,0x81', 'mov 0x61,0x85',
          'mov 0x62,0x82', 'mov 0x63,0x83', 'mov 0x64,0x84',
          'mov 0xa8,#0', 'mov 0x88,#0',
          'mov 0x85,0x36', 'mov 0x81,0x35']
for reg in ['dpx'] + ['dr' + str(i) for i in range(28, -1, -4)] + ['0xd1', '0xd0']:
    lines.append('pop ' + reg)
lines.append('eret')

# ---------------- _clobber_hi ----------------
# u8 clobber_hi(u8 seed): dpl carries the seed in and the error count out.
# Builds four per-round pattern bytes in the reserved direct window
# 0x70..0x7F, loads them into R16-R31 through the model-supported dword
# direct8 move (mov drN,dir), reads every byte back through the DR0 ferry,
# and counts mismatches.  Also clobbers DPXL and leaves PSW polluted.
# Saves nothing: preserving the interrupted context is the ISR's job, and
# the frozen ABI allows a callee to clobber the whole register file, PSW
# and DPX.
PAT = [(0x70, 0x00), (0x74, 0x11), (0x78, 0x33), (0x7c, 0x55)]
lines.append('.globl _clobber_hi')
lines.append('_clobber_hi:')
lines += ['mov r7,dpl', 'anl r7,#0x0f', 'orl r7,#0xe0', 'mov r6,#0']
label = 0
for base, x in PAT:
    lines += ['mov r5,r7', 'xrl r5,#0x%02x' % x]
    for k in range(4):
        lines.append(f'mov 0x{base + k:02x},r5')
for (base, x), dr in zip(PAT, ('dr16', 'dr20', 'dr24', 'dr28')):
    lines.append(f'mov {dr},0x{base:02x}')     # dword direct8 load
for (base, x), dr in zip(PAT, ('dr16', 'dr20', 'dr24', 'dr28')):
    lines += ['mov r5,r7', 'xrl r5,#0x%02x' % x, f'mov dr0,{dr}']
    for k in range(4):
        lines += [f'mov a,r{k}', 'clr c', 'subb a,r5',
                  'jz _k%d' % label, 'inc r6', '_k%d:' % label]
        label += 1
lines += ['mov 0x84,r5',        # DPXL actively clobbered (P28 pattern)
          'mov dpl,r6',         # return error count
          'eret']

(out / 'v2clob.asm').write_text('\n'.join(lines) + '\n')
subprocess.run([sdas, '-los', str(out / 'v2clob.rel'), str(out / 'v2clob.asm')],
               check=True)
(out / 'v2clob.lk').write_text('-i ' + str(out / 'v2clob.ihx') +
                               '\n-b CSEG=0\n' + str(out / 'v2clob.rel') + '\n-e\n')
subprocess.run([sdld, '-f', str(out / 'v2clob.lk')], check=True)
mem = {}
for line in (out / 'v2clob.ihx').read_text().splitlines():
    b = bytes.fromhex(line[1:])
    assert sum(b) % 256 == 0
    if b[3] == 0:
        a = int.from_bytes(b[1:3], 'big')
        mem.update({a + i: v for i, v in enumerate(b[4:-1])})
code = bytes(mem[i] for i in range(len(mem)))

# Locate _clobber_hi inside the blob (first byte of its prologue: mov r7,dpl).
# The sdas listing gives exact addresses, so parse it instead of guessing.
clob_off = None
for line in (out / 'v2clob.lst').read_text().splitlines():
    if '_clobber_hi:' in line:
        clob_off = int(line.split()[0], 16)
        break
assert clob_off is not None, 'clobber_hi symbol not found in listing'

yaml = '''--- !ELF
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
  - Name: .mcs251.DATA.patwin
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
  - Name: _clobber_hi
    Type: STT_FUNC
    Binding: STB_GLOBAL
    Section: .text
    Value: 0x%x
    Size: %d
''' % (code.hex(), len(code), clob_off, len(code) - clob_off)
(out / 'v2clob.yaml').write_text(yaml)
subprocess.run([yaml2obj, str(out / 'v2clob.yaml'), '-o', str(out / 'v2clob.o')],
               check=True)
