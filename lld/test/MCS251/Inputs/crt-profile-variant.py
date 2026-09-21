#!/usr/bin/env python3
"""Rewrite the frozen CRT yaml into a profile variant for the closed-set test.

Round-4 review finding 3: platform-contract evidence must come from a
CONSTRAINED profile (fixed layout, fixed relocation anchor set, template
byte equality), never from a relocation filter alone.  Two variants:

  operand - BOOT bytes 0x44..0x46 become `75 00 9A` and the `_main`
            R_MCS251_24 site moves 0x45 -> 0x47.  The `9A` at 0x46 is the
            IMMEDIATE of a classic `MOV direct,#imm`, not an ECALL opcode,
            yet the moved relocation still passes a naive
            "zero-addend R24 one byte behind a 9A" filter with the walker
            roles in order.
  fourth  - a fourth raw `9A` is written at BOOT 0x48 (the halt `sjmp self`
            opcode) with NO relocation.  A filter that only counts
            relocated sites still finds "exactly three".

Round-5 review finding 1 adds two variants that keep EVERY byte and EVERY
relocation of the frozen template and change only a chain target's SYMBOL
VALUE -- the name still resolves, the anchors still match, but the linked
ECALL no longer enters the documented walker:

  walker-g - `__mcs251_globals_init` keeps its name/section and moves its
             Value 0x4A -> 0x48 (the `80 FE` halt self-loop).
  walker-x - `__mcs251_xdata_init` keeps its name/section and moves its
             Value 0xA0 -> 0x48.

The landing check the round-5 fix added must be INDEPENDENT of the other
three profile conditions (BOOT size, the 12-anchor set, the out-of-relocation
template bytes).  Three more variants pin that independence, each changing
exactly ONE anchor's landing while leaving the other two conditions intact:

  walker-swap     - the two walkers TRADE entry points (0x4A<->0xA0).  Both
                    values are still legitimate BOOT offsets inside the
                    region the chain is documented to enter, so an
                    "offset is inside BOOT" test would accept it.
  walker-interior - `__mcs251_globals_init` moves to 0x56, an INTERIOR label
                    of its own walker body -- a defined symbol in the right
                    object and the right section, just not the entry.
  walker-section  - `__mcs251_globals_init` moves INTO ANOTHER BOOT-adjacent
                    section of the same object (HOME), i.e. a defined symbol
                    of the right object whose section is not the BOOT the
                    chain is checked in.

A conforming linker must reject all of them as platform-contract evidence and
keep them at the unknown grading.

Usage: crt-profile-variant.py <in.yaml> <out.yaml>
         <operand|fourth|walker-g|walker-x|walker-swap|walker-interior|walker-section>
"""
import pathlib
import re
import sys

src, dst, mode = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
text = src.read_text()

if mode in ("walker-g", "walker-x", "walker-swap", "walker-interior"):
    # Symbol-VALUE variants only: the BOOT content and the relocation set
    # stay byte-identical to the frozen template, so a filter that matches
    # the anchor NAMES alone still accepts them.

    def revalue_symbol(name, old, new):
        global text
        i = text.index("  - Name: %s\n" % name)
        j = text.index("    Value: %s\n" % old, i)
        # The Value line must belong to THIS symbol block (the next symbol
        # entry starts before any other "  - Name:" line).
        nxt = text.index("\n  - Name: ", i)
        assert j < nxt, "value not in %s's block" % name
        text = (
            text[:j] + "    Value: %s\n" % new + text[j + len("    Value: %s\n" % old) :]
        )

    if mode == "walker-g":
        revalue_symbol("__mcs251_globals_init", "0x4A", "0x48")
    elif mode == "walker-x":
        revalue_symbol("__mcs251_xdata_init", "0xA0", "0x48")
    elif mode == "walker-swap":
        # Both walkers keep their names, sections and every byte, but trade
        # entry points: the first chain ECALL enters the XDATA walker and the
        # second enters the XINIT walker.  Both values are still legitimate
        # BOOT offsets that a "resolves into this BOOT" test would accept.
        revalue_symbol("__mcs251_globals_init", "0x4A", "0xA0")
        revalue_symbol("__mcs251_xdata_init", "0xA0", "0x4A")
    else:
        # walker-interior: the walker name keeps its section and moves to the
        # next local label inside the same walker body (0x56), i.e. an
        # interior label of the region the chain is documented to enter.  It
        # is a valid BOOT offset and a valid symbol, but not the ENTRY.
        revalue_symbol("__mcs251_globals_init", "0x4A", "0x56")
    pathlib.Path(dst).write_text(text)
    raise SystemExit(0)

if mode == "walker-section":
    # Move the walker's DEFINITION into the object's OTHER code section
    # (HOME), keeping its name and a value that is a valid offset there.  The
    # symbol is still defined and still owned by the CRT object, so only the
    # "the landing must be the frozen BOOT entry" condition can reject it:
    # the anchor match, the 12-anchor set, the BOOT size and the template
    # bytes are all untouched.
    i = text.index("  - Name: __mcs251_globals_init\n")
    nxt = text.index("\n  - Name: ", i)
    block = text[i:nxt]
    assert "    Value: 0x4A\n" in block, "walker block shape changed"
    moved = (
        "  - Name: __mcs251_globals_init\n"
        "    Type: STT_NOTYPE\n"
        "    Binding: STB_GLOBAL\n"
        "    Section: .mcs251.HOME\n"
        "    Value: 0x4A\n"
    )
    text = text[:i] + moved + text[nxt:]
    pathlib.Path(dst).write_text(text)
    raise SystemExit(0)


# Restrict every rewrite to the .mcs251.BOOT SECTION block (the header
# comments also mention the name; the section header line is unique).
i = text.index("  - Name: .mcs251.BOOT\n")
j = text.index("Content: '", i) + len("Content: '")
k = text.index("'\n", j)
data = bytearray.fromhex(text[j:k])
assert len(data) == 0x106, "the frozen BOOT template is 0x106 bytes"
if mode == "operand":
    data[0x44] = 0x75
    data[0x46] = 0x9A
elif mode == "fourth":
    data[0x48] = 0x9A
else:
    raise SystemExit(
        "mode must be operand, fourth, walker-g, walker-x, walker-swap or "
        "walker-interior"
    )
text = text[:j] + data.hex().upper() + text[k:]
if mode == "operand":
    # Move the _main chain relocation to 0x47 (one byte behind the operand
    # 9A the mutation just planted at 0x46).
    moved = re.search(
        r"(      - Offset: )0x45(\n        Type: R_MCS251_24\n        Symbol: _main\n)",
        text[k:],
    )
    assert moved, "the frozen CRT's BOOT _main ecall site was not found"
    tail = text[k:]
    text = (
        text[:k]
        + tail[: moved.start()]
        + "      - Offset: 0x47"
        + moved.group(2)
        + tail[moved.end() :]
    )
pathlib.Path(dst).write_text(text)
