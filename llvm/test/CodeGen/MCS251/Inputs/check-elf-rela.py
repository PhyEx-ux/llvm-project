#!/usr/bin/env python3
"""Check raw MCS251 RELA fields independently of readobj's text rendering."""
import struct
import sys
from pathlib import Path

blob = Path(sys.argv[1]).read_bytes()
assert blob[:7] == b"\x7fELF\x01\x02\x01"
header = struct.unpack_from(">HHIIIIIHHHHHH", blob, 16)
assert header[0:2] == (1, 0x9999)
# A4/W3b (PM ruling 2026-09-13 #2): the ELF identity is the contract
# generation. This helper validates RELA fields and area payloads, not
# identity; it accepts the v1 note word (0x1, explicit v1 contract) and the
# v2 attributes word (0x102, any specified v2 contract incl. the default).
assert header[6] in (1, 0x102), header[6]
sections = [
    struct.unpack_from(">IIIIIIIIII", blob, header[5] + i * header[10])
    for i in range(header[11])
]
widths = {1: 2, 2: 3, 3: 1, 4: 1, 5: 1, 6: 1}
count = 0
for section in sections:
    if section[1] != 4:  # SHT_RELA
        continue
    assert section[9] == 12
    target = sections[section[7]]
    assert target[1] == 1  # SHT_PROGBITS, never reservations
    for pos in range(section[4], section[4] + section[5], 12):
        offset, info, addend = struct.unpack_from(">IIi", blob, pos)
        kind = info & 255
        width = widths[kind]
        assert offset + width <= target[5]
        field = blob[target[4] + offset:target[4] + offset + width]
        assert field == bytes(width), (offset, kind, addend, field.hex())
        count += 1
assert count or len(sys.argv) == 3, "test object has no relocations"
print("RELA fields are zero; offsets use actual machine bytes")

if len(sys.argv) == 3:
    # Compare machine-byte payloads, not ELF container bytes or ASxxxx's
    # expanded three-byte placeholders. This is a validation-only reader of
    # the llc-emitted REL subset; it performs no symbol resolution/linking.
    areas = []
    data = None
    for line in Path(sys.argv[2]).read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "A":
            areas.append([parts[1], int(parts[3], 16), {}])
        elif parts[0] == "T":
            values = [int(value, 16) for value in parts[1:]]
            offset = int.from_bytes(bytes(values[:3]), "big")
            data = values[3:]
        elif parts[0] == "R":
            raw = [int(value, 16) for value in parts[1:]]
            area = areas[(raw[2] << 8) | raw[3]]
            pos = 4
            removed = set()
            while pos < len(raw):
                mode = raw[pos]
                pos += 1
                if mode & 0xF0 == 0xF0:
                    mode = ((mode & 15) << 8) | raw[pos]
                    pos += 1
                index = raw[pos] - 3
                pos += 3  # T index and reference u16
                if mode & 1:
                    assert mode & 0x100, hex(mode)
                    # The selected byte is immaterial after zeroing the
                    # field; retain one byte and discard two placeholders.
                    data[index:index + 3] = [0, 0, 0]
                    removed.update([index, index + 1])
                else:
                    width = 3 if mode & 0x80 else 2
                    data[index:index + width] = [0] * width
            payload = [value for i, value in enumerate(data) if i not in removed]
            for i, value in enumerate(payload):
                assert offset + i not in area[2]
                area[2][offset + i] = value
            data = None

    strings = sections[header[12]]
    names = blob[strings[4]:strings[4] + strings[5]]
    expected = {}
    for name, size, payload in areas:
        if name != "_CODE":
            expected.setdefault(name, []).append((size, payload))
    actual = {}
    for section in sections:
        if not section[2] & 2:  # SHF_ALLOC
            continue
        name = names[section[0]:].split(b"\0", 1)[0].decode()
        if name == ".text":
            area = "CSEG"
        elif name == ".mcs251.dseg":
            area = "DSEG"
        elif name == ".mcs251.xinit":
            area = "XINIT"
        elif name.startswith(".mcs251."):
            area = name.split(".")[2]
        else:
            assert not section[5], name
            continue
        payload = {} if section[1] == 8 else dict(enumerate(
            blob[section[4]:section[4] + section[5]]))
        actual.setdefault(area, []).append((section[5], payload))
    assert actual == expected, (actual, expected)
    print("REL and ELF section sizes and zero-relocated machine bytes agree")
