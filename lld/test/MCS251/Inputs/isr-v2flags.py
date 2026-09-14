#!/usr/bin/env python3
"""Rewrite the ELF32 e_flags word of each named object to the v2 identity.

yaml2obj registers only the EF_MCS251_ABI_V1 enum for EM_MCS251, so every
generated ISR fixture is emitted with the v1 word and rewritten here -- the
same technique the other MCS251 v2 tests use.  G1 x P-4: the only approved IRQ
CRT is a v2 identity object, so the ISR test objects must be v2 as well.
"""
import pathlib
import sys

OFF = 36  # ELF32 big-endian e_flags offset
EF_V2 = bytes.fromhex("00000102")

for name in sys.argv[1:]:
    p = pathlib.Path(name)
    b = bytearray(p.read_bytes())
    assert len(b) >= OFF + 4, name
    b[OFF:OFF + 4] = EF_V2
    p.write_bytes(bytes(b))
