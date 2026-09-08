#!/usr/bin/env python3
"""Generate a YAML ELF object with XINIT sections totalling an exact byte count.

Usage: gen-xinit-boundary.py <output.yaml> <total_bytes>

Each XINIT record is 6 bytes header + payload.  Records with payload_size=0
are 6 bytes.  To reach arbitrary totals, the last record may include a
3-byte payload (9 bytes total).
"""
import sys

out = sys.argv[1]
total = int(sys.argv[2])

# DSEG slot must be large enough to hold the largest object_size.
# All records use dest=0x0040, obj_size=1, so a 4-byte DSEG slot suffices.
slot_size = 4

# Build records: each is 6 bytes (header only, payload_size=0).
# If total is not divisible by 6, the last record has a payload to make
# the total exact.  last_record = 6 + payload_size, where payload_size
# is chosen so that (num_full * 6) + (6 + payload_size) = total.
remainder = total % 6
if remainder == 0:
    num_full = total // 6
    records = bytes.fromhex('004000010000') * num_full
else:
    # num_full * 6 + (6 + remainder) = total
    # num_full = (total - 6 - remainder) / 6 = (total // 6) - 1
    num_full = total // 6 - 1
    records = bytes.fromhex('004000010000') * num_full
    payload = b'\xaa' * remainder
    header = bytes([
        0x00, 0x40,           # dest = 0x0040
        (remainder >> 8) & 0xff, remainder & 0xff,  # obj_size
        (remainder >> 8) & 0xff, remainder & 0xff,  # payload_size
    ])
    records += header + payload

# Verify
assert len(records) == total, f"Expected {total}, got {len(records)}"

# Split into two sections at a record boundary.
# All records except possibly the last are 6 bytes.  Split at a multiple of 6
# that is close to half the total.
split = (len(records) // 2) // 6 * 6
if split == 0:
    split = len(records)  # single section for small totals
hex_a = records[:split].hex()
hex_b = records[split:].hex()

# DSEG slot needs to be at least max(obj_size) = max(1, remainder)
# Use slot_size which is >= any obj_size we use
sections_yaml = ""
if hex_b:
    sections_yaml = f"""  - Name: .mcs251.xinit.a
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 1
    Content: '{hex_a}'
  - Name: .mcs251.xinit.b
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 1
    Content: '{hex_b}'
"""
else:
    sections_yaml = f"""  - Name: .mcs251.xinit
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 1
    Content: '{hex_a}'
"""

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
    Size: {slot_size}
{sections_yaml}  - Name: .note.mcs251.abi
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
