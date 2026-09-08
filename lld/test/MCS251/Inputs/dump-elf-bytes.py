#!/usr/bin/env python3
"""Print N bytes of a 32-bit big-endian ELF at a virtual address.

Usage: dump-elf-bytes.py <file.elf> <vaddr> <n>

Walks the program header table and dumps the bytes of the PT_LOAD segment
that maps <vaddr>, as space-separated lowercase hex.  Used by lit tests to
assert the actual relocation bytes written into the output image.
"""
import struct
import sys


def main():
    path, vaddr, count = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        print("not a 32-bit big-endian ELF: " + path, file=sys.stderr)
        return 1
    phoff, = struct.unpack_from(">I", data, 28)
    phentsize, phnum = struct.unpack_from(">HH", data, 42)
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, = struct.unpack_from(">I", data, off)
        if p_type != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr, _, p_filesz = struct.unpack_from(">IIII", data, off + 4)
        if p_vaddr <= vaddr and vaddr + count <= p_vaddr + p_filesz:
            start = p_offset + (vaddr - p_vaddr)
            print(" ".join("%02x" % b for b in data[start:start + count]))
            return 0
    print("vaddr 0x%x not mapped by any PT_LOAD" % vaddr, file=sys.stderr)
    return 1


sys.exit(main())
