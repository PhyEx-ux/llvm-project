#!/usr/bin/env python3
"""Structural companion checker for the WP5 A3 binary32 global-storage test.

Reads an ELF32 big-endian object directly (no objcopy/readelf dependency).

  * default mode reads `.mcs251.xinit` and prints one line per record in the
    frozen v1 grammar

        u16 DSEG address, u16 object size, u16 payload size, payload bytes

    as "<addr> <size> <paylen> <payload-hex>", so the RUN lines can assert on
    exact byte images without the 16-byte line wrapping of a hex dump;

  * `--section NAME` prints the raw bytes of one section as a single
    unwrapped hex string (used for the read-only `.text` image).

The script is fail-closed: a missing section, a truncated record, or a record
whose declared payload runs past the section is an error (nonzero exit), never
a silent skip.
"""

import struct
import sys


def find_section(path, wanted):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file: %s" % path)
    if data[4] != 1:  # ELFCLASS32
        raise SystemExit("expected ELFCLASS32: %s" % path)
    if data[5] != 2:  # ELFDATA2MSB
        raise SystemExit("expected big-endian ELF: %s" % path)
    (e_shoff,) = struct.unpack_from(">I", data, 0x20)
    (e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(">HHH", data, 0x2E)
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sections.append(struct.unpack_from(">IIIIIIIIII", data, off))
    strtab = sections[e_shstrndx]
    strdata = data[strtab[4]:strtab[4] + strtab[5]]
    for sec in sections:
        name = strdata[sec[0]:strdata.index(b"\0", sec[0])].decode()
        if name == wanted:
            return data[sec[4]:sec[4] + sec[5]]
    raise SystemExit("no %s section: %s" % (wanted, path))


def main():
    args = sys.argv[1:]
    if args and args[0] == "--section":
        if len(args) != 3:
            raise SystemExit("usage: %s --section NAME <elf-object>" % sys.argv[0])
        print(find_section(args[2], args[1]).hex())
        return
    if len(args) != 1:
        raise SystemExit("usage: %s [--section NAME] <elf-object>" % sys.argv[0])
    blob = find_section(args[0], ".mcs251.xinit")
    pos = 0
    n = 0
    while pos < len(blob):
        if pos + 6 > len(blob):
            raise SystemExit("truncated XINIT record header at %u" % pos)
        addr, size, paylen = struct.unpack_from(">HHH", blob, pos)
        pos += 6
        if pos + paylen > len(blob):
            raise SystemExit("XINIT payload overruns section at %u" % pos)
        payload = blob[pos:pos + paylen]
        pos += paylen
        print("%04x %04x %04x %s" % (addr, size, paylen, payload.hex()))
        n += 1
    if n == 0:
        raise SystemExit("empty XINIT section: %s" % sys.argv[1])


if __name__ == "__main__":
    main()
