#!/usr/bin/env python3
"""Generate .mcs251.bit object fixtures for the lld bit-allocator tests.

Usage:
  bit-fixture.py <out.yaml> records <count> [--sections EXTRA]
                  [--fixed ADDR] [--init1]
  bit-fixture.py <out.yaml> cross <name> <tu-tag>

`records N` writes a single-object YAML with N automatic bit definitions,
each named b0..b(N-1), plus (when --fixed ADDR is given) one fixed SHN_ABS
reference record first.  `cross` writes a cross-TU object: a definition of
`name` (GLOBAL) when the tag is `def`, or an undefined reference plus a
BITADDR8 use when the tag is `use`.
"""
import sys

HEADER = """--- !ELF
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
    Content: '{text}'
"""

NOTE = """  - Name: .note.mcs251.abi
    Type: SHT_NOTE
    AddressAlign: 4
    Notes:
      - Name: MCS251
        Type: 1
        Desc: '000000010000000100000000000000020000F3FF000000070000000000000000'
"""


def rec(kind, init=0):
    return "01%02x%02x0100000000" % (kind, init)


def records_yaml(n, fixed=None, init1=False):
    recs = []
    rels = []
    syms = []
    off = 0
    if fixed is not None:
        recs.append(rec(2))
        rels.append((off + 4, "f0"))
        syms.append(
            "  - Name: f0\n    Type: STT_OBJECT\n    Binding: STB_GLOBAL\n"
            "    Value: %d\n    Index: SHN_ABS\n" % fixed)
        off += 8
    for i in range(n):
        recs.append(rec(1, 1 if init1 else 0))
        rels.append((off + 4, "b%d" % i))
        syms.append(
            "  - Name: b%d\n    Type: STT_OBJECT\n    Binding: STB_GLOBAL\n"
            "    Section: .mcs251.bit\n    Value: 0x%x\n    Size: 1\n"
            % (i, off))
        off += 8
    rel = "".join(
        "      - Offset: 0x%x\n        Type: 10\n        Symbol: %s\n"
        "        Addend: 0\n" % (o, s) for o, s in rels)
    text = "00"
    y = HEADER.format(text=text)
    y += "  - Name: .mcs251.bit\n    Type: SHT_PROGBITS\n    Flags: [ ]\n"
    y += "    AddressAlign: 4\n    Content: '%s'\n" % "".join(recs)
    y += ("  - Name: .rela.mcs251.bit\n    Type: SHT_RELA\n    Link: .symtab\n"
          "    Info: .mcs251.bit\n    Relocations:\n" + rel)
    y += NOTE
    y += "Symbols:\n" + "".join(syms)
    return y


def cross_yaml(name, tag):
    y = HEADER.format(text="D200" if tag == "use" else "00")
    if tag == "def":
        y += "  - Name: .mcs251.bit\n    Type: SHT_PROGBITS\n    Flags: [ ]\n"
        y += "    AddressAlign: 4\n    Content: '%s'\n" % rec(1)
        y += ("  - Name: .rela.mcs251.bit\n    Type: SHT_RELA\n"
              "    Link: .symtab\n    Info: .mcs251.bit\n    Relocations:\n"
              "      - Offset: 0x4\n        Type: 10\n        Symbol: %s\n"
              "        Addend: 0\n" % name)
        y += NOTE
        y += ("Symbols:\n  - Name: %s\n    Type: STT_OBJECT\n"
              "    Binding: STB_GLOBAL\n    Section: .mcs251.bit\n"
              "    Value: 0x0\n    Size: 1\n" % name)
    else:
        y += ("  - Name: .rela.text\n    Type: SHT_RELA\n    Link: .symtab\n"
              "    Info: .text\n    Relocations:\n"
              "      - Offset: 0x1\n        Type: 11\n        Symbol: %s\n"
              "        Addend: 0\n" % name)
        y += NOTE
        y += ("Symbols:\n  - Name: %s\n    Type: STT_OBJECT\n"
              "    Binding: STB_GLOBAL\n" % name)
    return y


def main():
    out = sys.argv[1]
    mode = sys.argv[2]
    if mode == "records":
        n = int(sys.argv[3])
        fixed = None
        init1 = False
        if "--fixed" in sys.argv:
            fixed = int(sys.argv[sys.argv.index("--fixed") + 1], 0)
        if "--init1" in sys.argv:
            init1 = True
        open(out, "w").write(records_yaml(n, fixed, init1))
    elif mode == "cross":
        name = sys.argv[3]
        tag = sys.argv[4]
        open(out, "w").write(cross_yaml(name, tag))
    else:
        sys.exit("unknown mode " + mode)


if __name__ == "__main__":
    main()
