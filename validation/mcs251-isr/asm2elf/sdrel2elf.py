#!/usr/bin/env python3
#===- sdrel2elf.py - sdas251 .rel -> MCS251 ELF32 ET_REL converter --------===//
#
# Part of the LLVM Project under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===//
#
# Converts an sdas251 (ASxxxx version 3, sdld) .rel object into an ELF32 MSB
# ET_REL object with e_machine EM_MCS251 (0x9999) that mcs251-lld links
# directly together with clang/llc-produced MCS251 objects.  The caller must
# declare the assembler encoding mode with --source-mode source (see
# README.md: the .rel header does not record it, so the converter cannot
# detect it).
#
# The .rel grammar and relocation semantics implemented here follow the sdcc
# aslink sources in this repository (authoritative for this format):
#   sdcc-upstream/sdas/linksrc/lkrel.c    file recognition/loading
#   sdcc-upstream/sdas/linksrc/lkmain.c   line directives X/D/Q, H, M, A, S, T, R, P
#   sdcc-upstream/sdas/linksrc/lkarea.c   A line: name, size, flags, addr
#   sdcc-upstream/sdas/linksrc/lksym.c    S line: Def/Ref, area-relative value
#   sdcc-upstream/sdas/linksrc/lkrloc3.c  T/R line relocation processing
#   sdcc-upstream/sdas/linksrc/aslink.h   R3_* mode bits and escape coding
#   sdcc-upstream/sdas/asxxsrc/asout.c    assembler-side .rel emission
#   sdcc-upstream/sdas/as251/mcs251mch.c  MCS251 instruction encodings
#
# One module per file: an .rel carries exactly one "H" header.  sdas251
# writes a 3-address-byte, high-low header (XH3 by default; -d/-q listings
# select DH3/QH3); other header forms are rejected with a clear message.
#
# See README.md in this directory for the relocation mapping table, the
# supported subset, and the list of unsupported forms.

import argparse
import re
import struct
import sys

# --- ELF/MCS251 constants ---------------------------------------------------
# (llvm/include/llvm/BinaryFormat/ELFRelocs/MCS251.def,
#  lld/MCS251/Driver.cpp, lld/MCS251/LinkerCore.cpp)

EM_MCS251 = 0x9999
EF_MCS251_ABI_V1 = 1

ET_REL = 1
EV_CURRENT = 1
ELFCLASS32 = 1
ELFDATA2MSB = 2

SHT_NULL = 0
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOTE = 7
SHT_NOBITS = 8

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHF_INFO_LINK = 0x40

SHN_UNDEF = 0
SHN_ABS = 0xFFF1

STB_LOCAL = 0
STB_GLOBAL = 1

STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC = 2
STT_SECTION = 3

R_MCS251_16 = 1
R_MCS251_24 = 2
R_MCS251_LO8 = 3
R_MCS251_MID8 = 4
R_MCS251_HI8 = 5
R_MCS251_PC8 = 6
R_MCS251_J16 = 7
R_MCS251_J11 = 8

RELOC_NAME = {
    R_MCS251_16: "R_MCS251_16",
    R_MCS251_24: "R_MCS251_24",
    R_MCS251_LO8: "R_MCS251_LO8",
    R_MCS251_MID8: "R_MCS251_MID8",
    R_MCS251_HI8: "R_MCS251_HI8",
    R_MCS251_PC8: "R_MCS251_PC8",
    R_MCS251_J16: "R_MCS251_J16",
    R_MCS251_J11: "R_MCS251_J11",
}

# .note.mcs251.abi: namesz=7 "MCS251\0", descsz=32, type=NT_MCS251_ABI=1,
# then the 8 descriptor words validated by lld/MCS251/LinkerCore.cpp
# validateNote().  The layout matches llvm/lib/Target/MCS251/MCTargetDesc/
# MCS251ELFStreamer.cpp initSections().  Total size is exactly 52 bytes.
ABI_NOTE = (
    struct.pack(">III", 7, 32, 1)
    + b"MCS251\0\0"
    + struct.pack(">8I", 1, 1, 0, 2, 0x0000F3FF, 7, 0, 0)
)
assert len(ABI_NOTE) == 52

# --- sdld .rel mode bits (sdcc-upstream/sdas/linksrc/aslink.h) --------------

R3_BYTE = 0x001
R3_SYM = 0x002
R3_PCR = 0x004
R3_BYTX = 0x008
R3_USGN = 0x010
R3_PAG0 = 0x020
R3_PAG = 0x040
R3_MSB = 0x080
R_BYT3 = 0x100
R_HIB = 0x200
R_BIT = 0x400
R_J16 = 0x800

KNOWN_MODE_BITS = (R3_BYTE | R3_SYM | R3_PCR | R3_BYTX | R3_USGN | R3_PAG0 |
                   R3_PAG | R3_MSB | R_BYT3 | R_HIB | R_BIT | R_J16)

# aslink.h: IS_R_J11/IS_R_J19 mask the mode with (R3_BYTE|R3_BYTX|R3_MSB).
R_J19_MASK = R3_BYTE | R3_BYTX | R3_MSB
R3_J11 = R3_BYTX           # 11-bit ACALL/AJMP (opcode arrives as 3rd T byte)
R3_J19 = R3_BYTX | R3_MSB  # DS80C390 only
R_C24 = R3_MSB             # 24-bit absolute word

# Area flag bits (sdcc-upstream/sdas/asxxsrc/asxxxx.h).
A_OVR = 0x04
A_ABS = 0x08
A_CODE = 0x20
A_XDATA = 0x40
A_BIT = 0x80

# --- area name -> ELF section mapping ---------------------------------------
#
# The linker whitelist lives in lld/MCS251/LinkerCore.cpp classifySection():
#   .text / .text.*               -> CSEG region (PROGBITS ALLOC|EXECINSTR)
#   .mcs251.dseg / .mcs251.DSEG.* -> DSEG (NOBITS ALLOC|WRITE)
#   .mcs251.ISEG*                 -> ISEG (NOBITS ALLOC|WRITE)
#   .mcs251.BSEG_BYTES*           -> BSEG_BYTES (NOBITS ALLOC|WRITE)
#   .mcs251.XSEG*                 -> XSEG (NOBITS ALLOC|WRITE)
# ".mcs251.CSEG" is not a whitelisted name (the E4 assessment records that
# exact rejection); code areas must map into the .text family.

NOBITS_SECTION = {
    "DSEG": ".mcs251.dseg",
    "XSEG": ".mcs251.XSEG",
    "ISEG": ".mcs251.ISEG",
    "BSEG": ".mcs251.BSEG_BYTES",
}


class ConvertError(Exception):
    pass


class Chunk(object):
    """One T line plus the R groups that follow it."""

    def __init__(self, area, addr, data, lineno):
        self.area = area
        self.addr = addr
        self.data = data          # list of byte values from the T line
        self.lineno = lineno
        self.groups = []          # Group records in file order


class Group(object):
    """One decoded relocation group of an R line."""

    def __init__(self, mode, rtp, rindex, lineno):
        self.mode = mode
        self.rtp = rtp            # token index into Chunk.data
        self.rindex = rindex      # symbol or area index
        self.lineno = lineno
        # Filled by finalize:
        self.elf_type = None
        self.width = 0            # width of the value field inside data[]
        self.dtp = 0              # data index of the field (rtp - 3)
        self.offset = 0           # byte offset in the output section
        self.field = 0            # raw big-endian T-line field value
        self.addend = 0           # representative at the adb_* domain
        #                          width: non-positive for the pure slices
        #                          (_16/MID8/LO8, both reference kinds,
        #                          round 5); two's-complement for
        #                          _24/HI8 (any kind) and for J16/J11/PC8
        #                          symbol refs; unsigned only for J16/J11/
        #                          PC8 area refs (real non-negative
        #                          offsets)
        self.hidden = []
        self.select = False       # byte-selection (R_BYT3/R3_BYTX) group
        self.sym_kind = None      # 'section' | 'global' | 'abs'
        self.sym_ref = None       # Area | Symbol | None


class Area(object):
    def __init__(self, name, size, flags, addr, lineno):
        self.name = name
        self.size = size
        self.flags = flags
        self.addr = addr
        self.lineno = lineno
        self.chunks = []
        # Filled during conversion:
        self.section_name = None
        self.is_code = False
        self.skipped = False
        self.elf_section_index = None
        self.section_sym_index = None
        self.content = b""


class Symbol(object):
    def __init__(self, name, is_def, value, area, lineno):
        self.name = name
        self.is_def = is_def
        self.value = value
        self.area = area          # Area, or None for absolute definitions
        self.lineno = lineno
        # Filled during conversion:
        self.elf_sym_index = None
        self.absolute = area is None
        self.stt = STT_NOTYPE
        self.size = 0


class RelObject(object):
    def __init__(self):
        self.module = None
        self.areas = []
        self.symbols = []         # S-line order; index == .rel symbol index


# --- .rel parsing -----------------------------------------------------------

def parse_rel(path, text):
    obj = RelObject()
    current_area = None
    pending = []              # T lines since the last R line (T lines carry
    header_seen = False       # no area of their own; the R line binds them)
    symbol_index = {}
    radix = 16

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip("\r\n")
        if not line.strip():
            continue

        if not header_seen:
            header = line.strip()
            # lkmain.c: the header is [XDQ][HL][234] -- radix, byte order,
            # address bytes.  sdas251 emits 3 address bytes, high-low; the
            # radix depends on the -x/-d/-q flag (default X, -d selects
            # decimal, which is how DH3 files happen).
            m = re.match(r"^([XDQ])(H)(3)$", header)
            if not m:
                raise ConvertError(
                    "%s:%d: unsupported .rel header %r; this converter "
                    "supports sdas251 output only (header XH3, DH3 or QH3: "
                    "3 address bytes, high-low byte order)"
                    % (path, lineno, header))
            radix = {"X": 16, "D": 10, "Q": 8}[m.group(1)]
            header_seen = True
            continue

        directive, rest = line[0], line[1:]

        if directive == "H":
            if obj.module is not None or obj.areas or obj.symbols:
                raise ConvertError(
                    "%s:%d: multiple 'H' module headers in one .rel file; "
                    "convert one module per file" % (path, lineno))
            obj.module = "module"
        elif directive == "M":
            name = rest.strip()
            if not name:
                raise ConvertError("%s:%d: empty 'M' module name"
                                   % (path, lineno))
            obj.module = name
        elif directive == "A":
            # lkarea.c newarea(): "A <name> size <n> flags <n> addr <n>";
            # the linker's skip() consumes each keyword, eval() the number.
            m = re.match(r"\s*(\S+)\s*", rest)
            if not m:
                raise ConvertError("%s:%d: malformed 'A' area line"
                                   % (path, lineno))
            name = m.group(1)
            fields = {"size": 0, "flags": 0, "addr": 0}
            for kw in re.finditer(r"([A-Za-z]+)\s+([0-9A-Fa-f]+)",
                                  rest[m.end():]):
                if kw.group(1) not in fields:
                    raise ConvertError(
                        "%s:%d: unknown keyword %r in 'A' area line"
                        % (path, lineno, kw.group(1)))
                fields[kw.group(1)] = int(kw.group(2), radix)
            area = Area(name, fields["size"], fields["flags"],
                        fields["addr"], lineno)
            obj.areas.append(area)
            current_area = area
            if pending:
                raise ConvertError(
                    "%s:%d: 'T' line(s) without a following 'R' line before "
                    "the next area definition" % (path, lineno))
        elif directive == "S":
            # lksym.c newsym(): "S <name> (Def|Ref)<value>"; the value is an
            # offset inside the most recently defined area (absolute when no
            # area has been seen yet, e.g. the built-in SFR definitions).
            m = re.match(r"\s*(\S+)\s+(Def|Ref)([0-9A-Fa-f]+)\s*$", rest)
            if not m:
                raise ConvertError("%s:%d: malformed 'S' symbol line %r"
                                   % (path, lineno, line))
            name, kind, value = m.group(1), m.group(2), int(m.group(3), radix)
            is_def = kind == "Def"
            if name in symbol_index:
                prev = obj.symbols[symbol_index[name]]
                if is_def and not prev.is_def:
                    # Forward reference resolved later in the same module.
                    prev.is_def = True
                    prev.value = value
                    prev.area = current_area
                    prev.lineno = lineno
                elif is_def and prev.is_def:
                    raise ConvertError("%s:%d: duplicate definition of %r"
                                       % (path, lineno, name))
                # A Ref after a Def keeps the definition.
                continue
            sym = Symbol(name, is_def, value,
                         current_area if is_def else None, lineno)
            symbol_index[name] = len(obj.symbols)
            obj.symbols.append(sym)
        elif directive == "T":
            if current_area is None:
                raise ConvertError("%s:%d: 'T' line before any 'A' area line"
                                   % (path, lineno))
            toks = _num_tokens(rest, radix, path, lineno)
            if len(toks) < 3:
                raise ConvertError("%s:%d: truncated 'T' line (need the "
                                   "3-byte area offset)" % (path, lineno))
            addr = (toks[0] << 16) | (toks[1] << 8) | toks[2]
            data = toks[3:]
            for b in data:
                if b > 0xFF:
                    raise ConvertError("%s:%d: 'T' data byte 0x%X out of "
                                       "range" % (path, lineno, b))
            pending.append(Chunk(None, addr, data, lineno))
        elif directive == "R":
            # lkrloc3.c relr3(): "R 00 00 <area:2> <groups...>"; the first
            # two values must be 0 (R3_WORD|R3_AREA and its zero filler).
            # The R line declares the area of the T line(s) before it, which
            # is how sdld reads them (link_main processes line by line and
            # relr3() looks the area up by index).
            toks = _num_tokens(rest, radix, path, lineno)
            if len(toks) < 4:
                raise ConvertError("%s:%d: truncated 'R' line" % (path, lineno))
            if toks[0] != 0 or toks[1] != 0:
                raise ConvertError("%s:%d: 'R' line header must be '00 00' "
                                   "(R3_WORD|R3_AREA)" % (path, lineno))
            aindex = (toks[2] << 8) | toks[3]
            if aindex >= len(obj.areas):
                raise ConvertError("%s:%d: 'R' area index %d out of range "
                                   "(%d areas)" % (path, lineno, aindex,
                                                   len(obj.areas)))
            area = obj.areas[aindex]
            if not pending:
                # An R line without T bytes contributes nothing (sdas251
                # always flushes an empty T line first).
                continue
            chunk = pending[-1]
            chunk.area = area
            for c in pending:
                c.area = area
                area.chunks.append(c)
            pending = []
            idx = 4
            while idx < len(toks):
                raw = toks[idx]
                if (raw & 0xF0) == 0xF0:
                    # asout.c write_rmode(): modes > 0xFF are emitted as
                    # 0xF0|(mode>>8), mode&0xFF (aslink.h R_ESCAPE_MASK).
                    if idx + 1 >= len(toks):
                        raise ConvertError("%s:%d: truncated escaped "
                                           "relocation mode" % (path, lineno))
                    mode = ((raw & ~0xF0) << 8) | toks[idx + 1]
                    idx += 2
                else:
                    mode = raw
                    idx += 1
                if idx + 3 > len(toks):
                    raise ConvertError("%s:%d: truncated relocation group"
                                       % (path, lineno))
                rtp = toks[idx]
                rindex = (toks[idx + 1] << 8) | toks[idx + 2]
                idx += 3
                chunk.groups.append(Group(mode, rtp, rindex, lineno))
        elif directive == "P":
            raise ConvertError(
                "%s:%d: 'P' (setdp) lines are not supported: mcs251-lld has "
                "no setdp concept and this converter only supports the "
                "default direct page 0" % (path, lineno))
        else:
            raise ConvertError(
                "%s:%d: unknown .rel directive %r (supported: XH3 header, "
                "H, M, A, S, T, R)" % (path, lineno, directive))

    if not header_seen:
        raise ConvertError("%s: not an ASxxxx .rel object (missing XH3 header)"
                           % path)
    if pending:
        raise ConvertError("%s: 'T' line(s) at end of file without a "
                           "following 'R' line" % path)
    return obj


def _num_tokens(rest, radix, path, lineno):
    out = []
    for tok in rest.split():
        try:
            out.append(int(tok, radix))
        except ValueError:
            raise ConvertError(
                "%s:%d: value %r is not a digit of the radix declared by "
                "the .rel header" % (path, lineno, tok))
    return out


# --- relocation group decoding (lkrloc3.c relr3 semantics) ------------------

def _sext(value, nbytes):
    """Two's-complement interpretation of the low nbytes of `value`."""
    bits = nbytes * 8
    value &= (1 << bits) - 1
    if value & (1 << (bits - 1)):
        return value - (1 << bits)
    return value


# Addend interpretation width, in bytes, per ELF relocation type, plus the
# per-reference-kind interpretation of the field value.
#
# Two separate properties must hold, and it is worth keeping them apart
# (Alice review round 3):
#
# (1) BYTE CONGRUENCE.  sdld never stores an "addend" next to the reference:
#     the T-line field holds the raw partial value (asout.c writes
#     `esp->e_addr`, masked to the field width), and relr3() adds the
#     resolved base into that field and stores the sum back truncated to the
#     field's own width:
#       adb_1b (lkrloc.c:157)  j = v + rtval[i];        store j & 0xFF
#       adb_2b (lkrloc.c:191)  j = v + big-endian word; store j & 0xFFFF
#       adb_3b (lkrloc.c:233)  j = v + big-endian 24b;  store j & 0xFFFFFF
#     The ELF RELA equivalent is "write S + A", so whenever lld accepts the
#     link, the written bytes match sdld's iff A == T field (mod 2**W) at
#     the width W that adb_* adds at AND lld writes (any representative of
#     that residue class does; `field` below is one such representative).
#
# (2) CHECK SEMANTICS.  WHICH representative to pick decides whether lld's
#     range checks (LinkerCore.cpp applyRelocations()) accept the links
#     sdld accepts.  The R line distinguishes the reference base by R3_SYM:
#
#     - Symbol reference (R3_SYM set): relr3() uses reli = symval(s[rindex])
#       (lkrloc3.c:363-369); these are external/global symbols per
#       asexpr.c:576-585, and the T field holds a *partial expression*
#       that may borrow, e.g. `ecall #(_target-1)` emits T=FF FF FF.
#       The two's-complement representative recovers the intended value.
#
#     - Area reference (R3_SYM clear; R3_AREA base type 0): relr3() uses
#       reli = a[rindex]->a_addr (lkrloc3.c:377-384), the *base address*
#       of the referenced area; the T field holds the folded
#       offset-in-area plus expression constants (asout.c
#       out_txb(a_bytes, esp->e_addr), mod 2**24).  R3_SYM only tells us
#       where the base comes from -- it does NOT make area fields
#       non-negative by itself: same-area labels participate in negative
#       expressions (`ecall #(_entry-1)` emits T=FF FF FF, round 3).
#       The recoverable structure is the address space: MCS251 code areas
#       live in the 64K CSEG region, so a *legal non-negative* area offset
#       is < 0x10000 < 2**23, while any field >= 0x800000 can only be a
#       negative borrow wrapped mod 2**24 (or an out-of-space constant,
#       which sdld merely truncates).  Hence the full-24-bit-field types
#       (_24/HI8) are read as two's complement REGARDLESS of the reference
#       kind (round 3), and the pure slice types (_16/MID8/LO8) take the
#       unique NON-POSITIVE domain representative for BOTH reference kinds
#       (round 5 -- round 4's sext at domain width still misread borrows
#       deeper than half a domain as positive offsets, which overflow the
#       slice window at high bases; see the matrix).  The CONTROL types
#       (J16/J11) and PC8 keep the reference-kind split: sdas rejects
#       negative same-area control targets at assembly time (verified,
#       exit 2) and cross-area PC8 area refs carry non-negative offsets,
#       so an area field there is a real unsigned offset.
#
# The final matrix (domain = the width the addend must stay congruent to the
# T field at for byte equality: the adb_* addition width for full-field
# types, the slice modulus 2**(8*(k+1)) for byte selections.  lld checks
# quoted from applyRelocations(), LinkerCore.cpp:1749-1790):
#
#   type   adb (domain)              area ref      symbol ref
#   _24    adb_3b (24b, lkrloc3.c:633)  sext24        sext24
#          lld writes 24 bits, checks [0, 2**24).
#   HI8    adb_24_hi == adb_3b (24b)    sext24        sext24
#          The >>16 shift is carried by the R_HIB mode BIT, not folded into
#          T (verified: `mov a,#((_entry-1)>>16)` emits T=FF FF FF), so the
#          borrow form is reachable and the domain is the full field.
#   MID8   adb_24_mid (slice 16b)       np16          np16
#   LO8    adb_24_lo (slice 8b)         np8           np8
#   _16    adb_2b (16b, lkrloc.c:191)   np16          np16
#          np = the unique NON-POSITIVE representative of the field's
#          residue class mod M = 2**domain: u = field mod M; A = 0 when
#          u == 0, else A = u - M (round 5).  Round 4 quoted the lld
#          check correctly -- _16/LO8/MID8/HI8 are its "Is24Slice" family,
#          bounded by -0x800000 <= S+A <= 0xFFFFFF (LinkerCore.cpp:
#          1764-1773), while sdld's adb_2b/adb_24_* paths have NO check --
#          but the sext-at-domain-width representative does NOT make
#          every in-space link pass: sext goes negative only for u >= M/2,
#          so a borrow deeper than half a domain (a -b borrow wraps to
#          u = M - b < M/2) is read as the positive offset u, and S + u
#          leaves the window at high bases.  Round-5 repros (all true
#          targets in space, sdld exit 0, round-4 lld rejected all):
#            LO8  mov a,#(_entry-129)          base 0xFFFFF0 -> 0xFFFF6F
#            _16  mov dptr,#(_entry-0x8001)    base 0xFF9000 -> 0xFF0FFF
#            MID8 mov a,#((_entry-0x8001)>>8)  base 0xFF9000 -> 0xFF0FFF
#          The non-positive representative fixes the acceptance problem
#          completely: A <= 0 and S <= 0xFFFFFF give S+A <= 0xFFFFFF;
#          A >= -(M-1) = -65535 and S >= 0 give S+A >= -65535 >
#          -0x800000.  Both bounds hold for EVERY field and EVERY base,
#          so the slice check can never fire: every link sdld accepts (or
#          silently truncates) is accepted, byte-identical (the written
#          bytes depend only on (S + field) mod M, and A is congruent).
#          No positive/negative intent disambiguation is needed -- the
#          one thing sext could not provide.  This does NOT transfer to
#          the other families: _24/HI8 write/compare the full 24-bit
#          value (their landing semantics are the round-3 story), and
#          J16/J11/PC8 compare the FULL target (bank/page/PC window).
#   J16    adb_2b (16b, lkrloc3.c:642)  u16           sext16 (see caveat)
#   J11    adb_2b (16b, lkrloc3.c:569)  u16           sext16 (see caveat)
#          Area refs: mcs251mch.c out_control16/out_control11 reject
#          negative same-area targets at ASSEMBLY time (verified, exit 2),
#          so the field is always a real offset < 2**16; sdld's untruncated
#          bank/page check then equals lld's.  Symbol refs: the field is a
#          partial expression and T >= 0x8000 is GENUINELY AMBIGUOUS (a
#          -N borrow or a +0x8000-class constant; same bytes).  sext16
#          recovers the borrow class (and the byte-compare case
#          ljmp16_borrow) but lld's bank check then rejects the
#          sdld-silently-accepted `ljmp #(_sym+0x8000)` class ("J16 bank
#          overflow", pinned by ljmp16_sym_const_high); the unsigned
#          representative would invert which class diverges.  Neither
#          covers both; declared as a divergence in README, not papered
#          over.
#   PC8    adb_1b / adb_24_lo (== field width, lkrloc3.c:553)
#                                       unsigned      sext at field width
#          Same-area byte branches are resolved at assembly time
#          (mcs251mch.c out_relative); cross-area area refs carry the
#          target's non-negative offset-in-area, so no borrow form exists.
#
# Out-of-space forms (true value leaves the 24-bit space, e.g.
# `ecall #(_entry+0x123456)`): sdld silently truncates to a wrong landing,
# lld rejects with "relocation overflow" -- the intentional lld-stricter
# divergence (README "addend semantics").  Round 5 scopes it precisely:
# for the pure slices (_16/MID8/LO8) the non-positive representative
# makes the slice check unreachable (see the matrix), so those types match
# sdld's modular behaviour unconditionally and never draw this divergence;
# it lives on for _24/HI8, whose full 24-bit write materializes the wrong
# landing and whose sext24 landing semantics are the round-3 story.  For
# _24/HI8 fields >= 0x800000 sdas ALSO accepts positive constants of that
# magnitude (`ecall #(_entry+0x800000)`, verified): intent is
# unrecoverable from the .rel, and the sext24 representative reproduces
# sdld's modular landing byte-for-byte (pinned by ecall24_area_hugeconst).
ADDEND_DOMAIN_BYTES = {
    R_MCS251_16: 2,
    R_MCS251_24: 3,
    R_MCS251_J16: 2,
    R_MCS251_J11: 2,
    R_MCS251_PC8: None,   # == the field width (1-byte or 3-byte locus)
    R_MCS251_LO8: 1,      # selected byte is bits [7:0]
    R_MCS251_MID8: 2,     # selected byte is bits [15:8]
    R_MCS251_HI8: 3,      # selected byte is bits [23:16]
}


def _classify_group(obj, chunk, g, path):
    mode = g.mode
    unknown = mode & ~KNOWN_MODE_BITS & 0xFFFF
    if unknown:
        raise ConvertError(
            "%s:%d: relocation mode 0x%X uses unsupported bits 0x%X (not "
            "produced by sdas251 for MCS251)" % (path, g.lineno, mode, unknown))
    if mode & R_BIT:
        raise ConvertError(
            "%s:%d: bit-addressable relocations (R_BIT, mode 0x%X) are not "
            "supported: mcs251-lld has no bit-space relocation; express the "
            "bit access through explicit byte addresses instead"
            % (path, g.lineno, mode))
    if mode & R3_PAG:
        raise ConvertError("%s:%d: paged relocations (R3_PAG, mode 0x%X) are "
                           "not supported" % (path, g.lineno, mode))

    is_sym = bool(mode & R3_SYM)
    is_byte = bool(mode & R3_BYTE)
    is_pcr = bool(mode & R3_PCR)
    is_bytx = bool(mode & R3_BYTX)
    is_byt3 = bool(mode & R_BYT3)
    is_msb = bool(mode & R3_MSB)
    is_j16 = bool(mode & R_J16)
    is_hib = bool(mode & R_HIB)

    # Resolve the reference first so diagnostics can name it.
    sym = None
    target_area = None
    abs_const = False
    if is_sym:
        if g.rindex == 0xFFFF:
            abs_const = True  # asout.c: 8051-like absolute control marker
        elif g.rindex < len(obj.symbols):
            sym = obj.symbols[g.rindex]
        else:
            raise ConvertError(
                "%s:%d: relocation symbol index %d out of range (%d symbols)"
                % (path, g.lineno, g.rindex, len(obj.symbols)))
    else:
        if g.rindex >= len(obj.areas):
            raise ConvertError("%s:%d: relocation area index %d out of range "
                               "(%d areas)" % (path, g.lineno, g.rindex,
                                               len(obj.areas)))
        target_area = obj.areas[g.rindex]

    # Determine the ELF relocation type and field width (lkrloc3.c order).
    if not is_byte and is_bytx and (mode & R_J19_MASK) == R3_J19:
        raise ConvertError("%s:%d: 19-bit DS80C390 jumps (R3_J19) are not "
                           "meaningful on MCS251" % (path, g.lineno))
    if not is_byte and is_bytx and (mode & R_J19_MASK) == R3_J11:
        elf_type, width = R_MCS251_J11, 2
    elif not is_byte and is_j16:
        elf_type, width = R_MCS251_J16, 2
    elif not is_byte and (mode & R_J19_MASK) == R_C24:
        # R_C24: 24-bit absolute (asout.c outr3b emits this for every
        # relocatable 3-byte value, e.g. ecall/ejmp targets).
        elf_type, width = R_MCS251_24, 3
    elif not is_byte:
        if is_pcr:
            raise ConvertError(
                "%s:%d: 16-bit PC-relative relocations are not supported "
                "(mode 0x%X); MCS251 relative branches are 8-bit"
                % (path, g.lineno, mode))
        if is_bytx or is_msb:
            raise ConvertError("%s:%d: unsupported relocation mode 0x%X"
                               % (path, g.lineno, mode))
        elf_type, width = R_MCS251_16, 2
    else:
        # BYTE modes: a byte selection out of a 24-bit (R_BYT3, the form
        # sdas251 emits) or 16-bit (R3_BYTX) value field, or a plain byte.
        if is_byt3 or is_bytx:
            if is_pcr:
                elf_type = R_MCS251_PC8
            elif is_hib:
                elf_type = R_MCS251_HI8
            elif is_msb:
                elf_type = R_MCS251_MID8
            else:
                elf_type = R_MCS251_LO8
            width = 3 if is_byt3 else 2
        else:
            elf_type = R_MCS251_PC8 if is_pcr else R_MCS251_LO8
            width = 1

    # lld refuses control relocations whose target is not a CODE symbol
    # (LinkerCore.cpp applyRelocations).  Absolute (constant) ajmp/acall
    # targets reach us as references to .__.ABS. (or index 0xFFFF from the
    # generic 8051 emission path).
    if elf_type in (R_MCS251_J11, R_MCS251_J16):
        if abs_const or (sym is not None and sym.name == ".__.ABS."):
            raise ConvertError(
                "%s:%d: absolute (constant) ajmp/acall target is not "
                "supported: mcs251-lld requires 11/16-bit control transfers "
                "to target a CODE symbol; replace the numeric target with a "
                "label (ljmp/lcall to constants produce final bytes and are "
                "fine)" % (path, g.lineno))

    if sym is not None:
        if sym.name == ".__.ABS.":
            g.sym_kind, g.sym_ref = "abs", sym
        else:
            g.sym_kind, g.sym_ref = "global", sym
    elif target_area is not None:
        g.sym_kind, g.sym_ref = "section", target_area
    else:
        g.sym_kind, g.sym_ref = "abs", None
    g.elf_type = elf_type
    g.width = width
    # Byte selections: the T line carries a 2/3-byte value field for a
    # 1-byte instruction operand (outrxb R_BYT3/R3_BYTX); everything except
    # the selected byte is hidden from the output.  Full-field types
    # (R_MCS251_16/_24/J16, plain bytes) keep every T byte.
    g.select = is_byte and (is_byt3 or is_bytx)


def _finalize_groups(obj, path):
    """Decode every group and compute section-relative offsets/addends."""
    a_bytes = 3  # XH3: the T line starts with a 3-byte area-offset prefix;
    #              rtp indexes rtval[] *including* that prefix (relt3 stores
    #              the address at rtval[0..2], so rtp-3 is the data index).
    for area in obj.areas:
        for chunk in area.chunks:
            for g in chunk.groups:
                _classify_group(obj, chunk, g, path)
                data = chunk.data
                dtp = g.rtp - a_bytes
                if dtp < 0:
                    raise ConvertError(
                        "%s:%d: relocation field index %d points into the "
                        "'T' line address prefix" % (path, g.lineno, g.rtp))
                g.dtp = dtp
                # J11 fields occupy three T bytes: addr hi, addr lo, opcode.
                need = dtp + (3 if g.elf_type == R_MCS251_J11 else g.width)
                if need > len(data):
                    raise ConvertError(
                        "%s:%d: relocation group reaches past the end of its "
                        "'T' line (%d bytes)" % (path, g.lineno, len(data)))

                if g.elf_type == R_MCS251_J11:
                    # lkrloc3.c: rtval[rtp] = ((rtval[rtp]&7)<<5)|opcode;
                    # the raw opcode byte is hidden from the output.
                    g.hidden = [dtp + 2]
                    g.field = (data[dtp] << 8) | data[dtp + 1]
                elif g.select:
                    # Byte selections (adb_24_lo/mid/hi, adb_lo/hi): one
                    # byte of the big-endian value field reaches the image,
                    # the rest is hidden; the linker rewrites that byte.
                    sel = g.width - 1
                    if g.elf_type == R_MCS251_MID8:
                        sel = g.width - 2
                    elif g.elf_type == R_MCS251_HI8:
                        sel = g.width - 3
                    if sel < 0:
                        raise ConvertError(
                            "%s:%d: %s byte selection needs a 3-byte value "
                            "field (mode 0x%X)"
                            % (path, g.lineno, RELOC_NAME[g.elf_type],
                               g.mode))
                    g.hidden = [dtp + i for i in range(g.width)
                                if i != sel]
                    val = 0
                    for i in range(g.width):
                        val = (val << 8) | data[dtp + i]
                    g.field = val
                else:
                    # Full-field relocations (R_MCS251_16/_24/J16, 1-byte
                    # forms): adb_2b/adb_3b/adb_1b keep every T byte.
                    g.hidden = []
                    val = 0
                    for i in range(g.width):
                        val = (val << 8) | data[dtp + i]
                    g.field = val
                # The RELA addend interpretation follows the matrix above
                # (see ADDEND_DOMAIN_BYTES).  Three regimes:
                #   _24/HI8: two's complement over the full 24-bit field,
                #   for BOTH reference kinds (round 3).
                #   _16/MID8/LO8 (pure slices): the unique NON-POSITIVE
                #   domain representative, for BOTH reference kinds
                #   (round 5): with M = 2**(8d) and u = field mod M,
                #   A = 0 when u == 0, else A = u - M.  lld bounds these
                #   types by -0x800000 <= S+A <= 0xFFFFFF while sdld
                #   applies no check at all; A <= 0 with S <= 0xFFFFFF
                #   keeps S+A <= 0xFFFFFF, and A >= -(M-1) = -65535 with
                #   S >= 0 keeps S+A >= -65535 > -0x800000, so the check
                #   can never fire and every sdld-accepted (or silently
                #   truncated) link is accepted with byte-identical
                #   output -- no positive/negative intent disambiguation
                #   needed.  Round 4's sext choice could not do this: sext
                #   yields a negative addend only for u >= M/2, so a
                #   borrow deeper than half a domain (-129 wraps to
                #   u=0x7F, -0x8001 to u=0x7FFF) is read as a POSITIVE
                #   offset, and S+u leaves the window at high bases
                #   (round-5 repros: -129 at 0xFFFFF0, -0x8001 at
                #   0xFF9000, true targets 0xFFFF6F / 0xFF0FFF in space).
                #   J16/J11/PC8 keep the reference-kind split: an AREA
                #   reference there is a real unsigned offset (sdas
                #   rejects negative same-area control targets at assembly
                #   time; cross-area PC8 area refs carry non-negative
                #   offsets), a symbol reference still carries a
                #   borrowable partial expression.  These types compare
                #   the FULL target (bank/page/PC window), so a residue
                #   representative would be wrong there.  `field` keeps
                #   the raw big-endian value for dumps.
                domain = ADDEND_DOMAIN_BYTES[g.elf_type]
                if domain is None:
                    domain = g.width
                modulus = 1 << (domain * 8)
                if g.elf_type in (R_MCS251_16, R_MCS251_MID8,
                                  R_MCS251_LO8):
                    residue = g.field % modulus
                    g.addend = 0 if residue == 0 else residue - modulus
                elif g.sym_kind == "section" and g.elf_type in (
                        R_MCS251_J16, R_MCS251_J11, R_MCS251_PC8):
                    g.addend = g.field & (modulus - 1)
                else:
                    g.addend = _sext(g.field, domain)

    # Output offsets: the linker emits the kept (rtflg-set) T bytes in token
    # order, so a field's code offset is its data index minus the hidden
    # bytes before it (relr3 rtofst accounting).
    for area in obj.areas:
        for chunk in area.chunks:
            hidden = set()
            for g in chunk.groups:
                hidden.update(g.hidden)
            if chunk.groups and not chunk.data:
                raise ConvertError("%s:%d: relocation group on an empty 'T' "
                                   "line" % (path, chunk.lineno))
            outpos = 0
            for i in range(len(chunk.data)):
                for g in chunk.groups:
                    if g.dtp == i:
                        g.offset = chunk.addr + outpos
                if i not in hidden:
                    outpos += 1


# --- area/symbol finalization ------------------------------------------------

def _map_area(area, path):
    """Choose the ELF section name / region for one .rel area."""
    if area.flags & A_BIT:
        raise ConvertError(
            "%s:%d: bit-addressable area %r is not supported (mcs251-lld has "
            "no bit-space allocation for hand-written objects)"
            % (path, area.lineno, area.name))
    if area.flags & A_ABS:
        raise ConvertError(
            "%s:%d: absolute area %r is not supported: ET_REL objects carry "
            "no fixed addresses; pass --area-start to mcs251-lld instead"
            % (path, area.lineno, area.name))
    if area.flags & A_OVR:
        raise ConvertError(
            "%s:%d: overlay area %r is not supported: mcs251-lld overlay "
            "groups (OSEG/REG_BANK) are established by the C toolchain, not "
            "by area names" % (path, area.lineno, area.name))
    if area.addr != 0:
        raise ConvertError(
            "%s:%d: area %r carries a non-zero base address 0x%X in the .rel "
            "file; place areas with --area-start on mcs251-lld instead"
            % (path, area.lineno, area.name, area.addr))

    name = area.name
    if name in NOBITS_SECTION:
        area.section_name = NOBITS_SECTION[name]
        area.is_code = False
    elif name == "CSEG" or (area.flags & A_CODE):
        area.section_name = ".text" if name == "CSEG" else ".text." + name
        area.is_code = True
    elif area.flags & A_XDATA:
        area.section_name = ".mcs251.XSEG." + name
        area.is_code = False
    else:
        raise ConvertError(
            "%s:%d: area %r is not supported (no CODE/XDATA flag); supported "
            "areas: CSEG or any (CODE) area, DSEG, XSEG, ISEG, BSEG"
            % (path, area.lineno, name))


def _build_area_content(area, path):
    """Assemble the section bytes from the area's T/R chunks."""
    for chunk in area.chunks:
        if not area.is_code and chunk.data:
            raise ConvertError(
                "%s:%d: initialized data in area %r is not supported: "
                "mcs251-lld maps %s to a NOBITS reservation (no ROM bytes); "
                "put constant tables in a CODE area (CSEG)"
                % (path, chunk.lineno, area.name, area.name))
    content = bytearray()
    for chunk in area.chunks:
        if chunk.addr < len(content):
            raise ConvertError(
                "%s:%d: 'T' line offset 0x%X overlaps earlier data in area %r"
                % (path, chunk.lineno, chunk.addr, area.name))
        content.extend(b"\0" * (chunk.addr - len(content)))
        # J11 fields arrive as three T bytes (addr hi, addr lo, opcode); the
        # linker merges the opcode into the first byte (lkrloc3.c:
        # rtval[rtp] = ((rtval[rtp]&7)<<5)|opcode).  The section content must
        # already show that merged form: applyRelocations() takes the opcode
        # from the content byte at r_offset and rewrites only the address
        # bits, so pre-apply the merge with the partial address.
        repl = {}
        for g in chunk.groups:
            if g.elf_type == R_MCS251_J11:
                repl[g.dtp] = (((chunk.data[g.dtp] & 0x07) << 5)
                               | chunk.data[g.dtp + 2])
        content.extend(repl.get(i, b) for i, b in enumerate(chunk.data)
                       if not any(i in g.hidden for g in chunk.groups))
    if len(content) > area.size:
        raise ConvertError(
            "%s:%d: area %r holds 0x%X bytes of content but declares size "
            "0x%X" % (path, area.lineno, area.name, len(content), area.size))
    content.extend(b"\0" * (area.size - len(content)))
    area.content = bytes(content)


def _assign_symbol_metadata(obj, path):
    # Symbol types and gap-based function sizes.  .rel files carry no size
    # information, so a code symbol's size is the distance to the next
    # defined symbol of the same section (section end for the last one) --
    # the same bounded-gap convention the linker's own diagnostics use.
    by_section = {}
    for sym in obj.symbols:
        if not sym.is_def or sym.name == ".__.ABS.":
            continue
        if sym.absolute:
            sym.stt = STT_NOTYPE
            continue
        if sym.area.skipped:
            raise ConvertError(
                "%s:%d: symbol %r is defined in skipped area %r"
                % (path, sym.lineno, sym.name, sym.area.name))
        if sym.value > len(sym.area.content):
            raise ConvertError(
                "%s:%d: symbol %r value 0x%X exceeds the size of area %r"
                % (path, sym.lineno, sym.name, sym.value, sym.area.name))
        sym.stt = STT_FUNC if sym.area.is_code else STT_OBJECT
        by_section.setdefault(id(sym.area), []).append(sym)
    for syms in by_section.values():
        syms.sort(key=lambda s: (s.value, s.lineno))
        content_len = len(syms[0].area.content)
        for i, sym in enumerate(syms):
            end = syms[i + 1].value if i + 1 < len(syms) else content_len
            sym.size = max(0, end - sym.value)


def _collect_relocs(area):
    return [g for chunk in area.chunks for g in chunk.groups]


# --- ELF serialization -------------------------------------------------------

class StrTab(object):
    def __init__(self):
        self.data = bytearray(b"\0")
        self.cache = {"": 0}

    def add(self, name):
        if name not in self.cache:
            self.cache[name] = len(self.data)
            self.data.extend(name.encode() + b"\0")
        return self.cache[name]


def prepare(obj, args):
    """Parse-level finalization shared by conversion and --dump."""
    path = args.input
    sections = []
    for area in obj.areas:
        if area.name == "_CODE":
            if area.size or area.chunks:
                raise ConvertError(
                    "%s:%d: non-empty default area '_CODE' is not supported; "
                    "declare '.area CSEG (CODE)' explicitly"
                    % (path, area.lineno))
            area.skipped = True
            continue
        _map_area(area, path)
        sections.append(area)
    _finalize_groups(obj, path)
    for area in sections:
        _build_area_content(area, path)
    _assign_symbol_metadata(obj, path)
    return sections


def build_elf(obj, sections, args):
    # Section rows (alloc sections first, in .rel area order).
    sec_rows = []
    for area in sections:
        nobits = not area.is_code
        sec_rows.append({
            "name": area.section_name,
            "type": SHT_NOBITS if nobits else SHT_PROGBITS,
            "flags": (SHF_ALLOC | SHF_WRITE) if nobits
                     else (SHF_ALLOC | SHF_EXECINSTR),
            "align": 1,
            "content": b"" if nobits else area.content,
            "size": len(area.content),
            "area": area,
        })
        area.elf_section_index = 1 + len(sec_rows) - 1

    if not args.no_abi_note:
        note = ABI_NOTE
        if args.corrupt_abi_note:
            # Flip one descriptor word (object version 1 -> 2): the linker
            # must reject the object with an ABI mismatch diagnostic.
            note = note[:20] + struct.pack(">I", 2) + note[24:]
        sec_rows.append({"name": ".note.mcs251.abi", "type": SHT_NOTE,
                         "flags": 0, "align": 4, "content": note,
                         "size": len(note), "area": None})
    comment = ("sdas251 module: %s (sdrel2elf)\0" % (obj.module or "?")).encode()
    sec_rows.append({"name": ".comment", "type": SHT_PROGBITS, "flags": 0,
                     "align": 1, "content": comment, "size": len(comment),
                     "area": None})

    # Symbol table: [0] null, section symbols (local), defined globals,
    # undefined globals.  sh_info is the index of the first global.
    strtab = StrTab()
    sym_rows = [(0, 0, 0, 0, 0)]
    for row in sec_rows:
        if row["area"] is not None:
            row["area"].section_sym_index = len(sym_rows)
            sym_rows.append((0, 0, 0, (STB_LOCAL << 4) | STT_SECTION,
                             row["area"].elf_section_index))
    first_global = len(sym_rows)

    for sym in obj.symbols:
        if not sym.is_def or sym.name == ".__.ABS.":
            continue
        if sym.absolute:
            shndx, value = SHN_ABS, sym.value
        else:
            shndx, value = sym.area.elf_section_index, sym.value
        sym.elf_sym_index = len(sym_rows)
        sym_rows.append((strtab.add(sym.name), value, sym.size,
                         (STB_GLOBAL << 4) | sym.stt, shndx))
    for sym in obj.symbols:
        if sym.is_def:
            continue
        sym.elf_sym_index = len(sym_rows)
        sym_rows.append((strtab.add(sym.name), 0, 0,
                         (STB_GLOBAL << 4) | STT_NOTYPE, SHN_UNDEF))

    # Non-control .__.ABS. references (never emitted by sdas251 today) fall
    # back to an absolute symbol; control references were already rejected.
    abs_sym_index = None
    if any(g.sym_kind == "abs" for a in sections for g in _collect_relocs(a)):
        abs_sym_index = len(sym_rows)
        sym_rows.append((strtab.add(".__.ABS."), 0, 0,
                         (STB_GLOBAL << 4) | STT_NOTYPE, SHN_ABS))

    # RELA sections.
    rela_rows = []
    for row in sec_rows:
        if row["area"] is None:
            continue
        relocs = _collect_relocs(row["area"])
        if not relocs:
            continue
        entries = bytearray()
        for g in relocs:
            if g.sym_kind == "section":
                symidx = g.sym_ref.section_sym_index
            elif g.sym_kind == "global":
                symidx = g.sym_ref.elf_sym_index
            elif g.sym_kind == "abs" and g.sym_ref is not None:
                symidx = g.sym_ref.elf_sym_index
                if symidx is None:
                    symidx = abs_sym_index
            else:
                symidx = abs_sym_index
            entries += struct.pack(">IIi", g.offset,
                                   (symidx << 8) | g.elf_type, g.addend)
        rela_rows.append((row, bytes(entries)))

    # Layout.
    shstr = StrTab()
    for row in sec_rows:
        row["sh_name"] = shstr.add(row["name"])
    for row, _ in rela_rows:
        row["rela_name"] = shstr.add(".rela" + row["name"])
    symtab_name = shstr.add(".symtab")
    strtab_name = shstr.add(".strtab")
    shstrtab_name = shstr.add(".shstrtab")

    num_sections = len(sec_rows) + len(rela_rows)
    symtab_index = 1 + num_sections
    strtab_index = symtab_index + 1
    shstr_index = symtab_index + 2
    shnum = symtab_index + 3

    ehdr_size, shdr_size = 52, 40
    offset = ehdr_size
    blobs = []
    for row in sec_rows:
        if row["type"] == SHT_NOBITS:
            row["offset"] = 0
            continue
        row["offset"] = offset
        blobs.append((offset, row["content"]))
        offset = (offset + len(row["content"]) + 3) & ~3
    for row, entries in rela_rows:
        row["rela_offset"] = offset
        row["rela_size"] = len(entries)
        blobs.append((offset, entries))
        offset = (offset + len(entries) + 3) & ~3
    symtab_offset = offset
    symtab_size = len(sym_rows) * 16
    offset = (offset + symtab_size + 3) & ~3
    strtab_offset = offset
    strtab_size = len(strtab.data)
    offset += strtab_size
    shstr_offset = offset
    offset += len(shstr.data)
    shoff = (offset + 3) & ~3

    symtab_blob = bytearray()
    for name_off, value, size, info, shndx in sym_rows:
        symtab_blob += struct.pack(">IIIBBH", name_off, value, size, info, 0,
                                   shndx)
    blobs.append((symtab_offset, bytes(symtab_blob)))
    blobs.append((strtab_offset, bytes(strtab.data)))
    blobs.append((shstr_offset, bytes(shstr.data)))

    def shdr(name, typ, flags, off, size, link, info, align, entsize):
        return struct.pack(">IIIIIIIIII", name, typ, flags, 0, off, size,
                           link, info, align, entsize)

    # Body: ELF header, section contents, section header table.
    body = bytearray()
    for off, blob in blobs:
        body.extend(b"\0" * (off - ehdr_size - len(body)))
        body.extend(blob)
    body.extend(b"\0" * (shoff - ehdr_size - len(body)))
    headers = bytearray(shdr(0, 0, 0, 0, 0, 0, 0, 0, 0))
    for row in sec_rows:
        headers += shdr(row["sh_name"], row["type"], row["flags"],
                        row["offset"], row["size"], 0, 0, row["align"], 0)
    for row, _ in rela_rows:
        headers += shdr(row["rela_name"], SHT_RELA, SHF_INFO_LINK,
                        row["rela_offset"], row["rela_size"], symtab_index,
                        row["area"].elf_section_index, 4, 12)
    headers += shdr(symtab_name, SHT_SYMTAB, 0, symtab_offset, symtab_size,
                    strtab_index, first_global, 4, 16)
    headers += shdr(strtab_name, SHT_STRTAB, 0, strtab_offset, strtab_size,
                    0, 0, 1, 0)
    headers += shdr(shstrtab_name, SHT_STRTAB, 0, shstr_offset,
                    len(shstr.data), 0, 0, 1, 0)
    assert len(headers) == shnum * shdr_size

    ehdr = struct.pack(">16sHHIIIIIHHHHHH",
                       b"\x7fELF\1\2\1\0\0" + b"\0" * 7,
                       ET_REL, EM_MCS251, EV_CURRENT,
                       0,          # e_entry
                       0,          # e_phoff
                       shoff,      # e_shoff
                       EF_MCS251_ABI_V1,
                       ehdr_size, 0, 0, shdr_size, shnum, shstr_index)
    return bytes(ehdr + body + headers)


def dump_object(obj, sections):
    print("module: %s" % obj.module)
    for area in sections:
        kind = "code" if area.is_code else "nobits"
        print("area %-12s -> %-24s %-6s size=0x%X"
              % (area.name, area.section_name, kind, len(area.content)))
        for g in _collect_relocs(area):
            target = ("<area %s>" % g.sym_ref.name if g.sym_kind == "section"
                      else g.sym_ref.name if g.sym_ref is not None
                      else "<absolute>")
            if g.elf_type in (R_MCS251_16, R_MCS251_MID8,
                              R_MCS251_LO8):
                interp = "non-positive domain representative (pure slice)"
            elif g.sym_kind == "section" and g.elf_type in (
                    R_MCS251_J16, R_MCS251_J11, R_MCS251_PC8):
                interp = "unsigned area offset"
            else:
                domain = ADDEND_DOMAIN_BYTES[g.elf_type]
                if domain is None:
                    domain = g.width
                interp = ("two's complement (%d-byte adb domain, any "
                          "reference)" % domain)
            print("  reloc offset=0x%X %s <- %s addend=%d (%s; T field "
                  "0x%X, mode 0x%X)"
                  % (g.offset, RELOC_NAME[g.elf_type], target, g.addend,
                     interp, g.field, g.mode))
    for sym in obj.symbols:
        if sym.is_def and sym.name != ".__.ABS.":
            where = ("ABS" if sym.absolute
                     else "%s+0x%X" % (sym.area.section_name, sym.value))
            print("def  %-20s %-32s size=0x%X" % (sym.name, where, sym.size))
        elif not sym.is_def:
            print("ref  %-20s UND" % sym.name)


def main(argv):
    ap = argparse.ArgumentParser(
        description="Convert an sdas251 .rel object to an MCS251 ELF32 ET_REL "
                    "object for mcs251-lld (E4 standard asm->ELF path).")
    ap.add_argument("input", help="sdas251 .rel file")
    ap.add_argument("-o", "--output", required=True, help="output ELF object")
    ap.add_argument("--source-mode", required=True, choices=("source",),
                    help="declare the sdas251 encoding mode of the .rel "
                         "input; only 'source' is accepted.  The XH3 header "
                         "records radix/byte order/address width but NOT the "
                         "source/binary opcode map (the .source/.binary "
                         "directives), and .rel relocations carry no "
                         "encoding identity, so this converter cannot detect "
                         "the mode itself: you must re-assemble with the "
                         "default source mode (or an explicit '.source' "
                         "directive) and declare it here.  A '.binary'-mode "
                         "module must not be linked into source-mode MCS251 "
                         "images and is outside the supported contract: the "
                         ".rel carries no encoding identity, so this "
                         "converter cannot detect a misdeclared module.")
    ap.add_argument("--dump", action="store_true",
                    help="print the parsed areas/symbols/relocations")
    ap.add_argument("--no-abi-note", action="store_true",
                    help="omit .note.mcs251.abi (negative-test hook only; "
                         "mcs251-lld rejects such objects)")
    ap.add_argument("--corrupt-abi-note", action="store_true",
                    help="corrupt one .note.mcs251.abi descriptor word "
                         "(negative-test hook only)")
    args = ap.parse_args(argv)

    try:
        try:
            with open(args.input, "r") as f:
                text = f.read()
        except OSError as e:
            raise ConvertError("cannot read %s: %s" % (args.input, e))
        obj = parse_rel(args.input, text)
        sections = prepare(obj, args)
        if args.dump:
            dump_object(obj, sections)
        elf = build_elf(obj, sections, args)
        try:
            with open(args.output, "wb") as f:
                f.write(elf)
        except OSError as e:
            raise ConvertError("cannot write %s: %s" % (args.output, e))
    except ConvertError as e:
        sys.stderr.write("sdrel2elf: error: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
