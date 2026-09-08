#!/usr/bin/env python3
"""Generate a YAML ELF object with two XINIT sections totalling >65535 bytes."""
import sys

out = sys.argv[1]
rec = bytes.fromhex('004000010000') * 6000  # 36000 bytes each, total 72000
hex_a = rec.hex()
hex_b = rec.hex()

yaml = f"""--- !ELF
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
    Content: '00'
  - Name: .mcs251.DSEG.slot
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Size: 1
  - Name: .mcs251.xinit.a
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 1
    Content: '{hex_a}'
  - Name: .mcs251.xinit.b
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 1
    Content: '{hex_b}'
  - Name: .note.mcs251.abi
    Type: SHT_NOTE
    AddressAlign: 4
    Notes:
      - Name: MCS251
        Type: 1
        Desc: '000000010000000100000000000000020000F3FF000000070000000000000000'
Symbols:
  - Name: __mcs251_globals_init
    Binding: STB_GLOBAL
    Type: STT_FUNC
    Section: .text
"""
with open(out, 'w') as f:
    f.write(yaml)
