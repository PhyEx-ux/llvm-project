#!/usr/bin/env python3
"""Structural regression checks for the .symtab this linker's output writer
emits, on a 32-bit big-endian ELF.

Usage: symtab-structure.py [--against <readobj-dump>] [--entry0 <hex16>]
                           <file.elf>

Default mode asserts the symbol table invariants that llvm-readobj happily
prints across even when violated:
  - entry 0 is the all-zero null symbol (STN_UNDEF): all 16 bytes zero,
    st_shndx included;
  - sh_entsize is exactly 16 (sizeof Elf32_Sym);
  - sh_info equals the index of the first non-STB_LOCAL entry (one greater
    than the index of the last local symbol; entry count when all-local);
  - no STB_LOCAL entry appears after that first global;
  - sh_size is a whole number of 16-byte Elf32_Sym entries;
  - sh_link names a SHT_STRTAB section;
  - the string table starts and ends with NUL and every st_name offset
    resolves to a NUL-terminated string inside it;
  - every non-reserved st_shndx is a valid section index.

With --against <file>, <file> must be an llvm-readobj --syms dump of <path>;
the number of symbol rows it prints must equal the .symtab entry count.

With --entry0 <hex16>, the raw 16 bytes of entry 0 must equal <hex16> (this
subsumes the all-zero check when <hex16> is 32 zeros).

Exit status is nonzero with a "FAIL: ..." message on stderr for any violation.
"""
import re
import struct
import sys

SHT_STRTAB = 3
SHN_LORESERVE = 0xFF00


def fail(msg):
    print("symtab-structure: FAIL: " + msg, file=sys.stderr)
    sys.exit(1)


def main():
    argv = sys.argv[1:]
    against = None
    entry0_hex = None
    while argv and argv[0].startswith("--"):
        opt = argv.pop(0)
        if opt == "--against":
            if not argv:
                print("usage error: --against needs a file", file=sys.stderr)
                return 2
            against = argv.pop(0)
        elif opt == "--entry0":
            if not argv:
                print("usage error: --entry0 needs 32 hex digits",
                      file=sys.stderr)
                return 2
            entry0_hex = argv.pop(0)
        else:
            print("usage error: unknown option " + opt, file=sys.stderr)
            return 2
    if len(argv) != 1:
        print("usage: symtab-structure.py [--against <readobj-dump>] "
              "[--entry0 <hex16>] <file.elf>", file=sys.stderr)
        return 2
    with open(argv[0], "rb") as f:
        data = f.read()
    if data[:6] != b"\x7fELF\x01\x02":
        fail("not a 32-bit big-endian ELF: " + argv[0])
    shoff, = struct.unpack_from(">I", data, 32)
    shentsize, shnum = struct.unpack_from(">HH", data, 46)
    sections = []
    for i in range(shnum):
        off = shoff + i * shentsize
        (_name, typ, _flags, _addr, offset, size, link, info,
         _align, ent) = struct.unpack_from(">10I", data, off)
        sections.append({"type": typ, "offset": offset, "size": size,
                         "link": link, "info": info, "ent": ent})

    symtabs = [s for s in sections if s["type"] == 2]  # SHT_SYMTAB
    if not symtabs:
        fail("no SHT_SYMTAB section")
    for symtab in symtabs:
        if symtab["ent"] != 16:
            fail(".symtab sh_entsize %d is not 16 (sizeof Elf32_Sym)"
                 % symtab["ent"])
        if symtab["size"] % 16 != 0:
            fail(".symtab sh_size %d is not a multiple of 16" % symtab["size"])
        count = symtab["size"] // 16
        if count == 0:
            fail(".symtab has no entries")
        link = symtab["link"]
        if link >= shnum or sections[link]["type"] != SHT_STRTAB:
            fail(".symtab sh_link %d does not name a SHT_STRTAB" % link)
        strtab = sections[link]
        if strtab["size"] < 1:
            fail(".strtab is empty; it must at least hold the mandatory NUL")
        if data[strtab["offset"]] != 0:
            fail(".strtab does not start with NUL (st_name 0 must be empty)")
        if data[strtab["offset"] + strtab["size"] - 1] != 0:
            fail(".strtab does not end with NUL (a string is unterminated)")
        # Entry 0 must be the all-zero null symbol, st_shndx included.
        entry0 = data[symtab["offset"]:symtab["offset"] + 16]
        if entry0_hex is not None and entry0.hex() != entry0_hex.lower():
            fail("entry 0 is " + entry0.hex() + ", expected " + entry0_hex)
        elif entry0 != b"\x00" * 16:
            fail("entry 0 is not the all-zero null symbol: " + entry0.hex())
        # sh_info must equal the index of the first non-local entry (one past
        # the last local); an all-local table uses the entry count.
        first_global = count
        for i in range(count):
            st_info = data[symtab["offset"] + 16 * i + 12]
            if st_info >> 4 != 0:  # not STB_LOCAL
                first_global = i
                break
        if symtab["info"] != first_global:
            fail("sh_info %d != first global index %d"
                 % (symtab["info"], first_global))
        # Locals must not reappear after the first global: the table is
        # laid out as all locals first, then all globals.
        for i in range(first_global + 1, count):
            st_info = data[symtab["offset"] + 16 * i + 12]
            if st_info >> 4 == 0:
                fail("entry %d is STB_LOCAL but comes after the first "
                     "global at index %d" % (i, first_global))
        # Name offsets and section indices must be in range for every entry,
        # and every name must be a NUL-terminated string inside .strtab.
        for i in range(count):
            (st_name, _st_value, _st_size, _st_info, _st_other,
             st_shndx) = struct.unpack_from(">IIIBBH",
                                            data, symtab["offset"] + 16 * i)
            if st_name >= strtab["size"]:
                fail("entry %d st_name %d is outside .strtab (size %d)"
                     % (i, st_name, strtab["size"]))
            end = data.find(b"\x00", strtab["offset"] + st_name,
                            strtab["offset"] + strtab["size"])
            if end < 0:
                fail("entry %d st_name %d starts an unterminated string in "
                     ".strtab" % (i, st_name))
            if st_shndx < SHN_LORESERVE and st_shndx >= shnum:
                fail("entry %d st_shndx %d is not a valid section index"
                     % (i, st_shndx))
    if against is not None:
        with open(against) as f:
            printed = len(re.findall(r"^    Name: ", f.read(), re.M))
        if printed != count:
            fail("llvm-readobj prints %d symbol rows but .symtab holds %d "
                 "entries" % (printed, count))
    print("symtab-structure: OK (%d entries, sh_info=%d)"
          % (count, symtabs[0]["info"]))
    return 0


sys.exit(main())
