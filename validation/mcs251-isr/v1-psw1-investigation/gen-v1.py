#!/usr/bin/env python3
"""PSW1 investigation fixture assembly module (realhw-demo gen-sentinel.py
wrapper scheme, relocation-free machine code -> ELF object).

Three stage entry points, all argument-less (C writes the combo immediates
to 0x40/0x41 first):

  _v1_wr  -- write PSW/PSW1 combo, read back immediately.  No interrupts.
  _v1_lp  -- write combo, snapshot before, run the T10-identical
             JB+DJNZ budget wait with interrupts disabled, snapshot after.
             Measures how much the wait loop itself moves PSW/PSW1.
  _v1_ix  -- write combo, snapshot before, explicitly re-init Timer0
             (TCON=0 stops the counter and clears a stale TF0, TMOD=0x01
             forces 16-bit mode 1, TH0/TL0=00 reload), arm Timer0
             (EA+ET0, TR0), RE-ESTABLISH the combo after the TCON orl
             (that SFR orl writes N/Z from its result and would otherwise
             zero them before acceptance), then wait on DONE with a
             flag-neutral JB/SJMP poll, snapshot after.
             Entry snapshot slots 0x3c/0x3d are zeroed before the window
             and filled by the linked ISR when it records them.
             Behaviour depends on which ISR is linked:
               minisr    -- the hand-written ISR below (records entry
                            PSW/PSW1 to 0x3c/0x3d, perturbs flags with a
                            single ADD, sets DONE, RETI)
               compiler  -- the unmodified T10 compiler-generated ISR
             The baseline arm links no ISR object; IE is never set here so
             the stage is simply not used by that image.

The hand-written ISR is emitted only when arm == 'minisr'.
"""
from pathlib import Path
import subprocess
import sys

out = Path(sys.argv[1])
sdas, sdld, yaml2obj = sys.argv[2:5]
arm = sys.argv[5]

lines = ['.module v1psw1', '.source', '.area CSEG (CODE)']


def frame(save_test_sp=True):
    """Common C-context save/restore snippets (11 pushes, inverse pops)."""
    pro = []
    for reg in ['0xd0', '0xd1'] + ['dr' + str(i) for i in range(0, 32, 4)] + ['dpx']:
        pro.append('push ' + reg)
    pro += ['mov 0x3a,0x81', 'mov 0x3b,0x85', 'mov spx,#0x0600']
    ep = ['mov 0x85,0x3b', 'mov 0x81,0x3a']
    for reg in ['dpx'] + ['dr' + str(i) for i in range(28, -1, -4)] + ['0xd1', '0xd0']:
        ep.append('pop ' + reg)
    ep.append('eret')
    return pro, ep


# ---------------- _v1_wr: write/read coherence, no interrupt ----------------
pro, ep = frame()
lines += ['.globl _v1_wr', '_v1_wr:'] + pro
lines += ['mov a,0x40', 'mov 0xd0,a',       # PSW := combo.psw
          'mov a,0x41', 'mov 0xd1,a',       # PSW1 := combo.psw1
          'mov 0x42,0xd0', 'mov 0x43,0xd1']  # readback -> 0x42/0x43
lines += ep

# ---------------- _v1_lp: T10-identical DJNZ wait, interrupts off ---------
pro, ep = frame()
lines += ['.globl _v1_lp', '_v1_lp:'] + pro
lines += ['mov a,0x40', 'mov 0xd0,a',
          'mov a,0x41', 'mov 0xd1,a',
          'mov 0x44,0xd0', 'mov 0x45,0xd1',          # before
          'mov 0x21,#0', 'mov 0x22,#0', 'mov 0x23,#0x40',
          'mov 0x20,#0',
          '_lw:', 'jb 0x00,_ld', 'djnz 0x21,_lw', 'djnz 0x22,_lw',
          'djnz 0x23,_lw', '_ld:',
          'mov 0x46,0xd0', 'mov 0x47,0xd1']           # after
lines += ep

# ---------------- _v1_ix: interrupt window, JB/SJMP poll ------------------
pro, ep = frame()
lines += ['.globl _v1_ix', '_v1_ix:'] + pro
lines += ['mov a,0x40', 'mov 0xd0,a',
          'mov a,0x41', 'mov 0xd1,a',
          'mov 0x44,0xd0', 'mov 0x45,0xd1',          # before
          'mov 0x3c,#0', 'mov 0x3d,#0',              # entry snapshot markers
          'mov 0x20,#0',
          # Explicit per-run timer re-init BEFORE arming: TCON=0 stops the
          # counter and clears any stale TF0/pending request, TMOD=0x01
          # forces 16-bit mode 1, TH0/TL0=00 reloads the count.  The first
          # overflow is then exactly 65536 timer clocks after the TR0 orl
          # below -- a construction guarantee, not a property of whatever
          # state the previous stage happened to leave -- i.e. ~3 orders of
          # magnitude beyond the dozen-odd MOVs that follow, so the
          # interrupt can only be taken after the combo re-establishment.
          # Without this sequence an en != w observation would be
          # confounded (accepted-before-rebuild vs unmodelled flag writes);
          # with it, en != w means "accepted too early / window state
          # mismatch" and goes to dedicated triage first (PROTOCOL 2.3).
          'mov 0x88,#0',
          'mov 0x89,#0x01',
          'mov 0x8a,#0', 'mov 0x8c,#0',
          'mov 0xa8,#0x82', 'orl 0x88,#0x10',
          # Re-establish the combo AFTER the window-opening ORL.  Per the
          # model this SFR orl writes N/Z from its own result (TCON|0x10 ->
          # N=0, Z=0), which used to erase a non-zero N/Z before the
          # interrupt could take it into the hardware frame: combos 1/2 came
          # out with en-PSW1=00, so stage I never proved that a non-zero N/Z
          # is frame-restored.  With the re-init above the first overflow is
          # deterministically 65536 timer clocks away, so the values below
          # are what the frame saves at acceptance, and en-PSW1 == w-PSW1
          # (non-zero for combos 1/2/3/6) is the stage's main key.
          'mov a,0x40', 'mov 0xd0,a',
          'mov a,0x41', 'mov 0xd1,a',
          '_iw:', 'jb 0x00,_id', 'sjmp _iw', '_id:',
          'mov 0x48,0xd0', 'mov 0x49,0xd1',          # after
          'mov 0xa8,#0', 'mov 0x88,#0']
lines += ep

# ---------------- hand-written minimal ISR (minisr arm only) --------------
if arm == 'minisr':
    lines += ['.globl _timer0', '_timer0:',
              'mov 0x3c,0xd0',     # PSW at interrupt entry
              'mov 0x3d,0xd1',     # PSW1 at interrupt entry
              'mov a,#0x7f',       # deliberately perturb flags: A := 0x80
              'add a,#0x01',       # 0x7f+1 -> N=1, OV=1, AC=1, C=0, Z=0
              'mov 0x20,#0x01',    # DONE
              'reti']              # hardware frame restores PSW1 (or not)

(out / 'v1psw1.asm').write_text('\n'.join(lines) + '\n')
subprocess.run([sdas, '-los', str(out / 'v1psw1.rel'), str(out / 'v1psw1.asm')],
               check=True)
(out / 'v1psw1.lk').write_text('-i ' + str(out / 'v1psw1.ihx') +
                               '\n-b CSEG=0\n' + str(out / 'v1psw1.rel') + '\n-e\n')
subprocess.run([sdld, '-f', str(out / 'v1psw1.lk')], check=True)
mem = {}
for line in (out / 'v1psw1.ihx').read_text().splitlines():
    b = bytes.fromhex(line[1:])
    assert sum(b) % 256 == 0
    if b[3] == 0:
        a = int.from_bytes(b[1:3], 'big')
        mem.update({a + i: v for i, v in enumerate(b[4:-1])})
code = bytes(mem[i] for i in range(len(mem)))

# Function offsets from the listing.
offs = {}
for line in (out / 'v1psw1.lst').read_text().splitlines():
    for sym in ('_v1_wr:', '_v1_lp:', '_v1_ix:', '_timer0:'):
        if sym in line:
            offs[sym[:-1]] = int(line.split()[0], 16)
symbols = []
for sym in ('_v1_wr', '_v1_lp', '_v1_ix', '_timer0'):
    if sym in offs:
        symbols.append((sym, offs[sym]))
symbols.sort(key=lambda t: t[1])

sym_yaml = ''
for i, (sym, off) in enumerate(symbols):
    end = symbols[i + 1][1] if i + 1 < len(symbols) else len(code)
    sym_yaml += '''  - Name: %s
    Type: STT_FUNC
    Binding: STB_GLOBAL
    Section: .text
    Value: 0x%x
    Size: %d
''' % (sym, off, end - off)

# The linker registers real handlers only through .mcs251.isr metadata
# records; without them the vector falls back to the IRQ_DEFAULT fail-stop
# entry.  When the module carries the hand-written _timer0, emit the same
# 48 bytes (two 24-byte records, slot 1) the compiler emits for an
# interrupt(1) function, with the two R_MCS251_ISR_REF relocations at
# record offset +12 resolving to _timer0.
isr_meta = ''
if '_timer0' in offs:
    isr_meta = """  - Name: .mcs251.isr
    Type: SHT_PROGBITS
    AddressAlign: 4
    Content: '000100180101010100010001000000000000000000000000000100180201010100010001000000000000000000000000'
  - Name: .rela.mcs251.isr
    Type: SHT_RELA
    Info: 1
    Relocations:
      - Offset: 0x0C
        Symbol: _timer0
        Type: R_MCS251_ISR_REF
      - Offset: 0x24
        Symbol: _timer0
        Type: R_MCS251_ISR_REF
"""

text_sec = """  - Name: .text
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 1
    Content: '%s'
""" % code.hex()

yaml = """--- !ELF
FileHeader:
  Class: ELFCLASS32
  Data: ELFDATA2MSB
  Type: ET_REL
  Machine: EM_MCS251
  Flags: [ EF_MCS251_ABI_V1 ]
Sections:
%s%s  - Name: .mcs251.DATA.fixture
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Size: 0x35
  - Name: .mcs251.DATA.result
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Size: 0x220
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
%s""" % (isr_meta, text_sec, sym_yaml)
(out / 'v1psw1.yaml').write_text(yaml)
subprocess.run([yaml2obj, str(out / 'v1psw1.yaml'), '-o', str(out / 'v1psw1.o')],
               check=True)
