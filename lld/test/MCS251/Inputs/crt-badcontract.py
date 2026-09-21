#!/usr/bin/env python3
"""Move the frozen CRT's BOOT chain `ecall _main` site out of position.

Round-3 review finding 5: the platform contract must be a closed, enumerable
set of sites.  This fixture rewrites the CRT yaml so the `_main` R_MCS251_24
relocation - which must sit at BOOT+0x45 behind the documented 0x9A opcode -
is moved to offset 2, where the preceding byte is an operand byte of the
leading classic instruction rather than a real ecall opcode.  A conforming
linker must NOT accept this variant as platform-contract evidence.

Usage: crt-badcontract.py <in.yaml> <out.yaml>
"""
import pathlib
import re
import sys

src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = src.read_text()
# Restrict the rewrite to the BOOT relocation block.
i = text.index(".rela.mcs251.BOOT")
head, tail = text[:i], text[i:]
moved = re.search(
    r"(      - Offset: )0x45(\n        Type: R_MCS251_24\n        Symbol: _main\n)", tail)
assert moved, "the frozen CRT's BOOT _main ecall site was not found"
tail = tail[:moved.start()] + "      - Offset: 0x2" + moved.group(2) + tail[moved.end():]
dst.write_text(head + tail)
