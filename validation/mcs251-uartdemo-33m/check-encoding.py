#!/usr/bin/env python3
"""Encoding-mode hard gate for mcs251-uartdemo-33m (v3).

The gate asserts, with byte-level evidence:
  1. Intel HEX integrity: every record's count field, checksum, record type
     and EOF record are verified (v2 silently accepted corrupt records).
  2. ROM window: every byte in the image and every CODE area start lies in
     the flash window [0xff0000, 0x1000000) -- the same window build.sh
     passes to mcs251-lld via --flash-base/--flash-size.  (The linker gate
     is opt-in; this re-verifies it from the artifact.)
  3. BOOT + every FUNC symbol decodes EXACTLY, instruction-boundary by
     instruction-boundary, as legal MCS-251 SOURCE-mode encodings.  This is
     the primary check (v2's byte-frequency scan is demoted to purely
     informational evidence that can never fail the gate; see "Limitations"
     below).

Opcode/length model provenance:
  * In this toolchain's source mode (sdas251 mcs251mch.c needs_prefix,
    mirrored by MCS251MCCodeEmitter.cpp putOpcode), a CLASSIC 8051 opcode
    with low nibble >= 6 is A5-escaped; classic opcodes with low nibble < 6
    execute bare.  Classic instruction lengths are the public 8051 map --
    the table is now COMPLETE (all 255 classic opcodes; self-test group L7
    reconciles it against the public map).  The round-3 gap closers
    B3/B4/B5/E4/E5/F4/F5 were each verified bare with sdas251 source mode
    before being added (cpl c = B3; cjne a,#0x12 = B4 12 09; cjne a,0x30 =
    B5 30 06; clr a = E4; mov a,0x30 = E5 30; cpl a = F4; mov 0x30,a =
    F5 30).  B4/B5 are legal classic CJNE forms: bare in source mode, not
    A5-escaped (low nibble < 6), and not in the native 251 opcode set.
  * Native 251 opcodes (bare) and their lengths come from the closed set in
    llvm/lib/Target/MCS251/MCTargetDesc/MCS251MCCodeEmitter.cpp, plus two
    general forms verified against sdas251 itself:
      - 0x0B/0x1B word inc/dec: 2 bytes when the specifier low nibble is
        0/4/C/D/E/F (e.g. "inc wr4" = 0B 24), 3 bytes for the low nibble
        8/A word-move forms ("mov wr4,@wr2" = 0B 18 20 / "@dr60" zero-disp
        cf. MOV16rmS zero-disp emission, "mov wr4,@dr60" = 0B FA 20).
      - 0xAD word multiply: 2 bytes with any specifier ("mul wr2,wr6" =
        AD 13; the emitter's "AD 64" is one instance).
  * Some native opcodes derive length from the following specifier byte
    (low nibble 4 selects the 16-bit immediate form): 0x7E, 0x7A, 0x0B,
    0x1B, 0x2E, 0x4E, 0x5E, 0x6E, 0x9E, 0xBE.
  * ANY byte not covered by the tables is a hard FAIL (fail-closed).  The
    tables must only be extended with sdas251-verified encodings.

Limitations (stated honestly):
  * A5-prefixed instructions are only legal when the escaped opcode is a
    classic opcode with low nibble >= 6 -- but "A5 7E" (escaped
    "mov r6,#imm") is such a legal encoding even though emitting it for a
    251 core op indicates a BINARY-mode producer regression.  The
    zero-tolerance signature rule is therefore evaluated ONLY on decoded
    instruction opcode/prefix bytes (the 'A5+xx' histogram decode_region
    already produces), never on a raw byte scan of the code stream: an
    "A5 7E" pair inside an instruction's immediate field (e.g. the legal
    native 4-byte form "7E 44 A5 7E" = mov wr,#0x7EA5) is NOT a signature
    hit.  Raw-byte A5-signature frequencies are printed as informational
    evidence only and can never fail the gate.
  * String literals live in CSEG after _main; the linker map's FUNC lines
    (build.sh links with --keep-symbols) give exact function boundaries.
    Bytes inside mapped .text sections but outside any FUNC are validated
    as a pure string pool (printable ASCII / NUL / CR / LF / TAB only),
    not decoded as instructions.

Self-test:  python3 check-encoding.py --self-test FIRMWARE.hex FIRMWARE.map
corrupts copies of the inputs (bad checksum, bad count, missing EOF,
garbage line, unknown record type, illegal opcode, injected binary-mode
signature, pool corruption) and verifies every corruption is caught.  A
positive "length" group then asserts instr_len() against sdas251-assembled
reference bytes (classic escaped 0x76..0x7F MOV-immediate forms, other
A5 escapes, bare classics, native 251 forms, boundary negatives), the
round-3 bare-classic gap closers B3/B4/B5/E4/E5/F4/F5 (L6), and a full
reconciliation of CLASSIC_LEN against the public 8051 length map on all
255 classic opcodes (L7).
"""
import re
import sys
import tempfile
import os

# The flash window build.sh passes to mcs251-lld (ISR linker-test values:
# validation/mcs251-isr compile-matrix.py / ISR-TASK-BREAKDOWN.md).
FLASH_BASE = 0xff0000
FLASH_SIZE = 0x10000

# A5-escape regression signatures: escaped 251 core ops must never appear
# (a bare 7E/9A/8A/AA/78/7A is native; A5-escaping it means a binary-mode
# producer).  Evaluated ONLY on decoded instruction opcode/prefix bytes
# (the 'A5+xx' histogram decode_region already produces) -- NOT on a raw
# byte scan of the code stream, where an "A5 7E" pair inside a legal
# immediate (native "7E 44 A5 7E" = mov wr,#0x7EA5) would false-positive.
# Raw-byte counts are printed as informational evidence only.
BAD_A5_SIGNATURES = ('a57e', 'a59a', 'a58a', 'a5aa', 'a578', 'a57a')


class IhexError(Exception):
    """Corrupt Intel HEX input."""


class DecodeError(Exception):
    """Byte stream does not decode as legal source-mode instructions."""


# --------------------------------------------------------------------------
# Strict Intel HEX reader
# --------------------------------------------------------------------------

def parse_ihex(path):
    """Parse Intel HEX with full record integrity checks.

    Returns (mem, n_records, n_data_records); mem maps address -> byte.
    Raises IhexError on any malformed record.
    """
    mem = {}
    ext = 0
    n_records = 0
    n_data_records = 0
    saw_eof = False
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line:
                continue
            n_records += 1
            if saw_eof:
                raise IhexError("record after EOF at line %d" % lineno)
            if not line.startswith(':'):
                raise IhexError("line %d: not an Intel HEX record "
                                "(missing ':')" % lineno)
            body = line[1:]
            try:
                b = bytes.fromhex(body)
            except ValueError:
                raise IhexError("line %d: invalid hex digits" % lineno)
            if len(b) < 5:
                raise IhexError("line %d: record shorter than count+addr+"
                                "type+checksum" % lineno)
            n = b[0]
            if len(b) != n + 5:
                raise IhexError("line %d: count field %d != %d payload "
                                "bytes" % (lineno, n, len(b) - 5))
            if (sum(b) & 0xff) != 0:
                raise IhexError("line %d: checksum mismatch (sum 0x%02x)"
                                % (lineno, sum(b) & 0xff))
            addr = (b[1] << 8) | b[2]
            rt = b[3]
            data = b[4:4 + n]
            if rt == 0x00:
                base = (ext << 16) | addr
                for i, v in enumerate(data):
                    a = base + i
                    if a > 0xffffff:
                        raise IhexError("line %d: address 0x%x outside "
                                        "24-bit space" % (lineno, a))
                    if a in mem:
                        raise IhexError("line %d: duplicate write to "
                                        "0x%06x" % (lineno, a))
                    mem[a] = v
                n_data_records += 1
            elif rt == 0x01:
                if n != 0 or addr != 0:
                    raise IhexError("line %d: malformed EOF record" % lineno)
                saw_eof = True
            elif rt == 0x04:
                if n != 2:
                    raise IhexError("line %d: malformed extended linear "
                                    "address record" % lineno)
                ext = int.from_bytes(data, 'big')
            elif rt == 0x05:
                # Start linear address (llvm-objcopy emits the 24-bit
                # entry point).  Must carry exactly 4 bytes and appear
                # before EOF; the entry value itself is not asserted here.
                if n != 4:
                    raise IhexError("line %d: malformed start linear "
                                    "address record" % lineno)
            else:
                raise IhexError("line %d: unsupported record type 0x%02x"
                                % (lineno, rt))
    if n_records == 0:
        raise IhexError("no Intel HEX records found")
    if not saw_eof:
        raise IhexError("missing EOF record")
    if not mem:
        raise IhexError("no data records")
    return mem, n_records, n_data_records


def patch_ihex_bytes(hex_path, patches):
    """Return hex text with {addr: byte} patched, checksums recomputed."""
    out = []
    ext = 0
    remaining = dict(patches)
    with open(hex_path) as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith(':'):
                out.append(raw)
                continue
            b = bytearray(bytes.fromhex(line[1:]))
            n = b[0]
            rt = b[3]
            if rt == 0x04:
                ext = int.from_bytes(b[4:4 + n], 'big')
            elif rt == 0x00:
                base = (ext << 16) | (b[1] << 8) | b[2]
                for i in range(n):
                    a = base + i
                    if a in remaining:
                        b[4 + i] = remaining.pop(a) & 0xff
                        b[-1] = (-(sum(b[:-1]))) & 0xff
                        line = ':' + bytes(b).hex().upper()
            out.append(line + '\n')
    if remaining:
        raise IhexError("patch addresses not in image: %s"
                        % [hex(a) for a in sorted(remaining)])
    return ''.join(out)


# --------------------------------------------------------------------------
# Linker map
# --------------------------------------------------------------------------

def parse_map(path):
    """Parse the mcs251-lld map: s_/l_ symbols, object .text sections and
    (with --keep-symbols) FUNC lines."""
    areas = {}
    sections = []   # (path, section, start, length)
    funcs = []      # (name, start, length)
    sec_re = re.compile(r'^(\S+):(\S+)\s+0x([0-9a-fA-F]+)\s+\+0x([0-9a-fA-F]+)$')
    func_re = re.compile(r'^FUNC\s+0x([0-9a-fA-F]+)\s+\+0x([0-9a-fA-F]+)\s+(\S+)$')
    for line in open(path):
        line = line.strip()
        if line.startswith(('s_', 'l_')) and '=' in line:
            k, v = line.split('=', 1)
            areas[k.strip()] = int(v.strip(), 16)
            continue
        m = sec_re.match(line)
        if m:
            sections.append((m.group(1), m.group(2),
                             int(m.group(3), 16), int(m.group(4), 16)))
            continue
        m = func_re.match(line)
        if m:
            funcs.append((m.group(3), int(m.group(1), 16),
                          int(m.group(2), 16)))
    return areas, sections, funcs


# --------------------------------------------------------------------------
# MCS-251 source-mode instruction length model
# --------------------------------------------------------------------------

# Classic 8051 opcode -> total instruction length.  In source mode the
# low-nibble<6 classic opcodes run bare; all others must be A5-escaped.
CLASSIC_LEN = {}


def _cl(ops, ln):
    for o in ops:
        CLASSIC_LEN[o] = ln


_cl([0x00, 0x03, 0x13, 0x23, 0x33, 0x04, 0x14, 0x22, 0x32, 0x73, 0x83,
     0x93, 0x84, 0xA3, 0xA4, 0xC3, 0xC4, 0xD3, 0xD4,
     0xE0, 0xE1, 0xE2, 0xE3, 0xF0, 0xF1, 0xF2, 0xF3], 1)
_cl([0x05, 0x15, 0x24, 0x25, 0x34, 0x35, 0x44, 0x45, 0x54, 0x55, 0x64,
     0x65, 0x74, 0x94, 0x95, 0xC5,
     0x42, 0x52, 0x62, 0x72, 0x82, 0xA0, 0xA2, 0x92, 0xB0, 0xB2, 0xC0,
     0xD0, 0xC2, 0xD2, 0x40, 0x50, 0x60, 0x70, 0x80], 2)
for _hi in range(16):
    CLASSIC_LEN[_hi << 4 | 0x01] = 2      # ajmp/acall a11
CLASSIC_LEN[0x02] = 3                     # ljmp
CLASSIC_LEN[0x12] = 3                     # lcall
_cl([0x10, 0x20, 0x30], 3)                # jbc/jb/jnb bit,rel
_cl([0x43, 0x53, 0x63, 0x75, 0x85, 0x90, 0xD5], 3)
for _o in (0x06, 0x07, 0x16, 0x17, 0x26, 0x27, 0x36, 0x37, 0x46, 0x47,
           0x56, 0x57, 0x66, 0x67):
    CLASSIC_LEN[_o] = 1
# 0x76..0x7F: ALL TEN are classic MOV-immediate forms and ALL are 2 bytes
# -- 76/77 = mov @ri,#imm, 78..7F = mov rn,#imm.  (sdas251 source mode
# never emits the escaped rn form -- "mov r6,#0x12" goes to the native
# 7E 60 12 -- but the decoder must still size the escaped classic form:
# "A5 7E 12" = escaped mov r6,#0x12 is 3 bytes, not 2.)  Bare 0x78..0x7F
# never reaches this entry (NATIVE_LEN owns those bytes in source mode);
# nothing below may override these back to 1 byte.
for _o in range(0x76, 0x80):
    CLASSIC_LEN[_o] = 2                   # mov @ri/rn,#imm -- all 2 bytes
for _o in range(0x86, 0x90):
    CLASSIC_LEN[_o] = 2                   # mov dir,@r/rn
for _o in range(0x96, 0xA0):
    CLASSIC_LEN[_o] = 1                   # subb a,rn/@r
for _o in range(0xA6, 0xB0):
    CLASSIC_LEN[_o] = 2                   # mov rn/@r,dir
for _o in range(0xB6, 0xC0):
    CLASSIC_LEN[_o] = 3                   # cjne ...,#imm,rel
# Round-3 gap closers (Alice review): B3/B4/B5 were missing, so a bare
# classic `cjne a,#0x12,rel` -- legal source mode, low nibble < 6, no A5
# escape, and not a native 251 opcode -- hit "undecodable bare opcode".
# sdas251 source mode, read back from the .lst before freezing:
#   cpl c             -> B3        (1 byte)
#   cjne a,#0x12,+9   -> B4 12 09  (3 bytes)
#   cjne a,0x30,+6    -> B5 30 06  (3 bytes)
# Matches the public 8051 map; nothing below overrides these (the cjne
# register-range walk starts at 0xB6).
CLASSIC_LEN[0xB3] = 1                     # cpl c
CLASSIC_LEN[0xB4] = 3                     # cjne a,#imm,rel (bare classic)
CLASSIC_LEN[0xB5] = 3                     # cjne a,direct,rel (bare classic)
for _o in range(0xC6, 0xD0):
    CLASSIC_LEN[_o] = 1                   # xch a,rn/@r
CLASSIC_LEN[0xD6] = 1                     # xchd a,@r0 (sdas251: A5 D6 = 2 B)
CLASSIC_LEN[0xD7] = 1                     # xchd a,@r1 (sdas251: A5 D7 = 2 B)
CLASSIC_LEN[0xE6] = 1                     # mov a,@r0 (sdas251: A5 E6 = 2 B)
CLASSIC_LEN[0xE7] = 1                     # mov a,@r1 (sdas251: A5 E7 = 2 B)
CLASSIC_LEN[0xF6] = 1                     # mov @r0,a (sdas251: A5 F6 = 2 B)
CLASSIC_LEN[0xF7] = 1                     # mov @r1,a (sdas251: A5 F7 = 2 B)
# Round-3 gap closers (Alice review), same sdas251 source-mode run:
#   clr a             -> E4        (1 byte)
#   mov a,0x30        -> E5 30     (2 bytes)
#   cpl a             -> F4        (1 byte)
#   mov 0x30,a        -> F5 30     (2 bytes)
# Matches the public 8051 map; the register-range walks below start at
# 0xD8/0xE8/0xF8 and never touch these.
CLASSIC_LEN[0xE4] = 1                     # clr a
CLASSIC_LEN[0xE5] = 2                     # mov a,direct
CLASSIC_LEN[0xF4] = 1                     # cpl a
CLASSIC_LEN[0xF5] = 2                     # mov direct,a
for _o in range(0xD8, 0xE0):
    CLASSIC_LEN[_o] = 2                   # djnz rn,rel
for _o in range(0xE8, 0xF0):
    CLASSIC_LEN[_o] = 1                   # mov a,rn
for _o in range(0xF8, 0x100):
    CLASSIC_LEN[_o] = 1                   # mov rn,a
# 1-byte register forms.  NOTE the bases stop at 0x68 on purpose: the
# classic 0x76..0x7F range is all 2-byte MOV immediate (above).  Stamping
# 0x78 here used to override it and made escaped "A5 7E 12" (mov r6,#0x12)
# decode as 2 instead of 3 bytes.  0x08..0x0F inc, 0x18..0x1F dec,
# 0x28 add, 0x38 addc, 0x48 orl, 0x58 anl, 0x68 xrl -- all op A,rn = 1 B.
for _base in (0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68):
    for _i in range(8):
        CLASSIC_LEN[_base + _i] = 1
CLASSIC_LEN[0xA5] = None                  # not a classic opcode


def _imm_len(spec):
    """Native imm8/imm16 opcode: specifier low nibble 4 -> 16-bit imm."""
    n = spec & 0x0F
    if n == 4:
        return 4
    if n == 0:
        return 3
    return None


def _spx_len(spec):
    """Native 0x0B/0x1B (word inc/dec and @wr/@dr word move forms)."""
    n = spec & 0x0F
    if n in (0xA, 0x8):
        # 3-byte word move: spec xx?A = @dr60 zero-disp form (emitter
        # MOV16rmS/MOV16mrS putOpcode 0x10B/0x11B), spec xx?8 = plain
        # @wr source/store form (sdas251: "mov wr4,@wr2" = 0B 18 20,
        # "mov @wr2,wr4" = 1B 18 20).
        return 3
    if n in (0x0, 0x4, 0xC, 0xD, 0xE, 0xF):
        return 2
    return None


# Native (bare) 251 opcode -> length (int, or callable(specifier)->length).
NATIVE_LEN = {}
for _o in (0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x78):
    NATIVE_LEN[_o] = 2                    # rel8 branch family (emitter)
for _o in (0x13, 0x33, 0xC3, 0xA4, 0x32, 0xAA):
    NATIVE_LEN[_o] = 1                    # rrc a / rlc a / clr c / mul ab /
                                          # reti / eret
for _o in (0x2C, 0x2D, 0x2F, 0x4C, 0x4D, 0x5C, 0x5D, 0x6C, 0x6D, 0x9C,
           0x9D, 0x9F, 0x7C, 0x7D, 0x7F, 0xBC, 0xBD, 0xBF, 0xCA, 0xDA,
           0x99):
    NATIVE_LEN[_o] = 2                    # opcode + register specifier byte
for _o in (0x0E, 0x1E, 0x3E):
    NATIVE_LEN[_o] = 2                    # sra/srl/sll + specifier
for _o in (0x2E, 0x4E, 0x5E, 0x6E, 0x9E, 0xBE):
    NATIVE_LEN[_o] = _imm_len             # imm8 vs imm16 by specifier
for _o in (0x09, 0x19, 0x29, 0x39, 0x69, 0x79):
    NATIVE_LEN[_o] = 4                    # @wr+dis16 move forms
NATIVE_LEN[0x8A] = 4                      # ejmp 24-bit
NATIVE_LEN[0x9A] = 4                      # ecall 24-bit
NATIVE_LEN[0xAD] = 2                      # mul wr,wr (sdas251: AD 13)
NATIVE_LEN[0x7E] = lambda s: 4 if (s & 0x0F) in (4, 8) else 3
NATIVE_LEN[0x7A] = lambda s: 4 if (s & 0x0F) == 0xC else 3
NATIVE_LEN[0x0B] = _spx_len
NATIVE_LEN[0x1B] = _spx_len


def instr_len(buf, off):
    """Length of the source-mode instruction at buf[off]; DecodeError if
    the byte does not start a legal source-mode instruction."""
    b0 = buf[off]
    if b0 == 0xA5:
        if off + 1 >= len(buf):
            raise DecodeError("A5 prefix at end of region (offset +0x%x)"
                              % off)
        b1 = buf[off + 1]
        if b1 == 0xA5 or (b1 & 0x0F) < 6 or CLASSIC_LEN.get(b1) is None:
            raise DecodeError("A5 escape on illegal opcode 0x%02x "
                              "(offset +0x%x)" % (b1, off))
        return 1 + CLASSIC_LEN[b1]
    if b0 in NATIVE_LEN:
        ln = NATIVE_LEN[b0]
        if callable(ln):
            if off + 1 >= len(buf):
                raise DecodeError("truncated specifier at end of region "
                                  "(offset +0x%x)" % off)
            ln = ln(buf[off + 1])
            if ln is None:
                raise DecodeError("native opcode 0x%02x with illegal "
                                  "specifier 0x%02x (offset +0x%x)"
                                  % (b0, buf[off + 1], off))
        return ln
    if (b0 & 0x0F) < 6:
        if b0 not in CLASSIC_LEN:
            raise DecodeError("undecodable bare opcode 0x%02x (offset "
                              "+0x%x)" % (b0, off))
        return CLASSIC_LEN[b0]
    raise DecodeError("bare opcode 0x%02x has low nibble >= 6 and is not "
                      "a known native 251 opcode -- not legal source mode "
                      "(offset +0x%x)" % (b0, off))


def decode_region(buf, name):
    """Decode buf fully at instruction boundaries.

    Returns (n_instructions, a5_prefixed, a5_opcodes histogram dict).
    Raises DecodeError with the failing offset on any illegal/truncated
    byte or on an instruction overrunning the region end.
    """
    off = 0
    n = 0
    a5 = 0
    hist = {}
    while off < len(buf):
        ln = instr_len(buf, off)
        if off + ln > len(buf):
            raise DecodeError("instruction at +0x%x overruns region end "
                              "(%d bytes)" % (off, len(buf)))
        if buf[off] == 0xA5:
            a5 += 1
            key = 'A5+%02X' % buf[off + 1]
        else:
            key = '%02X' % buf[off]
        hist[key] = hist.get(key, 0) + 1
        off += ln
        n += 1
    return n, a5, hist


# --------------------------------------------------------------------------
# The gate
# --------------------------------------------------------------------------

POOL_BYTES = set(range(0x20, 0x7f)) | {0x00, 0x0d, 0x0a, 0x09}


def run_checks(hex_path, map_path):
    """Run the full gate.  Returns (failures, evidence, stats)."""
    mem, n_records, n_data = parse_ihex(hex_path)
    areas, sections, funcs = parse_map(map_path)

    failures = []
    evidence = []
    stats = {'instrs': 0, 'a5': 0, 'code_bytes': 0, 'pool_bytes': 0,
             'funcs': 0, 'a5_hist': {}}

    def check(name, ok, detail):
        evidence.append("%-58s %s" % (name, detail))
        if not ok:
            failures.append(name)

    def seg_bytes(start, length, what):
        missing = [start + i for i in range(length) if start + i not in mem]
        if missing:
            check("%s bytes present/contiguous in HEX" % what, False,
                  "%d missing at 0x%06x.." % (len(missing), missing[0]))
            return None
        return bytes(mem[start + i] for i in range(length))

    # --- map layout (proven area starts) ---
    for key, want in (('s_HOME', 0xff0000), ('s_VECS', 0xff0003),
                      ('s_BOOT', 0xff0100), ('s_CSEG', 0xff0200),
                      ('s_XINIT', 0xff8000), ('l_HOME', 0x0003),
                      ('l_BOOT', 0x006f)):
        ok = areas.get(key) == want
        check("map %s == 0x%06x" % (key, want), ok,
              "0x%06x" % areas[key] if key in areas else "MISSING")

    # --- flash window (mirrors build.sh --flash-base/--flash-size) ---
    # Like lld/MCS251 checkFlashGate(), only CODE-class areas are gated;
    # DSEG/ISEG/OSEG/REG_BANK are RAM and exempt.
    CODE_AREAS = ('s_HOME', 's_VECS', 's_BOOT', 's_CSEG', 's_XINIT')
    for key in CODE_AREAS:
        if key not in areas:
            continue
        v = areas[key]
        check("area %s start 0x%06x inside flash window 0x%06x..0x%06x"
              % (key, v, FLASH_BASE, FLASH_BASE + FLASH_SIZE),
              FLASH_BASE <= v < FLASH_BASE + FLASH_SIZE, "")
    lo_b, hi_b = min(mem), max(mem)
    check("all %d HEX bytes inside flash window 0x%06x..0x%06x"
          % (len(mem), FLASH_BASE, FLASH_BASE + FLASH_SIZE),
          lo_b >= FLASH_BASE and hi_b < FLASH_BASE + FLASH_SIZE,
          "range 0x%06x..0x%06x" % (lo_b, hi_b))

    # --- BOOT crt prologue (byte-level anchors) ---
    boot = seg_bytes(areas['s_BOOT'], areas.get('l_BOOT', 0), "BOOT")
    if boot is not None:
        check("BOOT prologue 75 E9 00 7E F8 (mov WTST,#0; mov spx,#)",
              boot[:5] == bytes.fromhex('75e9007ef8'), boot[:5].hex())
        ecall_globals = (boot[7] == 0x9A and
                         int.from_bytes(boot[8:11], 'big') == 0xff0114)
        check("BOOT[7] ecall __mcs251_globals_init -> 0xFF0114",
              ecall_globals, boot[7:11].hex())
        cseg_len = areas.get('l_CSEG', 0)
        main_addr = int.from_bytes(boot[12:15], 'big')
        in_cseg = (areas['s_CSEG'] <= main_addr <
                   areas['s_CSEG'] + max(cseg_len, 1))
        check("BOOT[11] ecall _main -> 0x%06x inside CSEG" % main_addr,
              boot[11] == 0x9A and in_cseg, boot[11:15].hex())

        # --- BOOT region: full instruction-boundary decode ---
        try:
            n, a5, hist = decode_region(boot, "BOOT")
            stats['instrs'] += n
            stats['a5'] += a5
            stats['code_bytes'] += len(boot)
            for k, v in hist.items():
                stats['a5_hist'][k] = stats['a5_hist'].get(k, 0) + v
            check("BOOT full decode: %d instrs cover all %d bytes at "
                  "instruction boundaries" % (n, len(boot)), True,
                  "A5-prefixed %d" % a5)
        except DecodeError as e:
            check("BOOT full instruction-boundary decode", False, str(e))

    # --- FUNC symbols: exact-boundary decode of every function ---
    check("map carries FUNC lines (link with --keep-symbols)",
          len(funcs) > 0, "%d FUNC symbols" % len(funcs))
    text_sections = [(p, s, a, l) for (p, s, a, l) in sections
                     if s == '.text']
    func_intervals = []
    for name, start, length in sorted(funcs, key=lambda f: f[1]):
        stats['funcs'] += 1
        if length == 0:
            continue
        buf = seg_bytes(start, length, "FUNC %s" % name)
        if buf is None:
            continue
        try:
            n, a5, hist = decode_region(buf, name)
            stats['instrs'] += n
            stats['a5'] += a5
            stats['code_bytes'] += len(buf)
            for k, v in hist.items():
                stats['a5_hist'][k] = stats['a5_hist'].get(k, 0) + v

            def _family_ok(key):
                if not key.startswith('A5+'):
                    return True
                op = int(key[3:], 16)
                return 0xE8 <= op <= 0xEF or 0xF8 <= op <= 0xFF

            bad_fam = [k for k in hist if not _family_ok(k)]
            check("FUNC %s 0x%06x+%#x full decode: %d instrs, A5 %d, "
                  "families legal" % (name, start, length, n, a5),
                  not bad_fam,
                  "illegal A5 family %s" % bad_fam if bad_fam else "ok")
            func_intervals.append((start, start + length, name))
        except DecodeError as e:
            check("FUNC %s 0x%06x+%#x full instruction-boundary decode"
                  % (name, start, length), False, str(e))

    # FUNC regions must not overlap each other.
    ovl = None
    for (s1, e1, n1), (s2, e2, n2) in zip(sorted(func_intervals),
                                          sorted(func_intervals)[1:]):
        if s2 < e1:
            ovl = (n1, n2)
            break
    check("FUNC regions do not overlap", ovl is None,
          "" if ovl is None else "%s vs %s" % ovl)

    # --- .text section gaps = string pool (pure data, not decoded) ---
    for path, sec, start, length in text_sections:
        inside = sorted((s, e, n) for (s, e, n) in func_intervals
                        if start <= s and e <= start + length)
        gaps = []
        cur = start
        for s, e, n in inside:
            if s > cur:
                gaps.append((cur, s))
            cur = max(cur, e)
        if cur < start + length:
            gaps.append((cur, start + length))
        gap_bytes = sum(e - s for s, e in gaps)
        stats['pool_bytes'] += gap_bytes
        bad = []
        for s, e in gaps:
            for i in range(s, e):
                if mem[i] not in POOL_BYTES:
                    bad.append((i, mem[i]))
        covered = sum(e - s for s, e, _ in inside)
        check("text %s..+%#x: FUNCs cover %d B, %d B data pool all "
              "printable/NUL" % (hex(start), length, covered, gap_bytes),
              not bad,
              "non-pool bytes %s" % ["0x%06x=0x%02x" % b for b in bad[:4]]
              if bad else "pool ok")

    # --- code byte ranges for byte-evidence checks below ---
    code = bytearray()
    for s, e, _ in sorted(func_intervals):
        for a in range(s, e):
            code.append(mem[a])
    if boot is not None:
        code[:0] = boot
    code = bytes(code)
    cseg = seg_bytes(areas['s_CSEG'], areas.get('l_CSEG', 0), "CSEG")

    if cseg is not None and code:
        # --- binary-mode producer regression signatures ---
        # Judged ONLY on decoded instruction opcode/prefix bytes: the
        # 'A5+xx' histogram decode_region accumulated over BOOT + every
        # FUNC.  A raw "A5 7E" pair inside a legal immediate (native
        # "7E 44 A5 7E" = mov wr,#0x7EA5) is legal source-mode code and
        # must NOT fail the gate; the raw-byte counts below are
        # informational evidence only.
        for sig in BAD_A5_SIGNATURES:
            op = int(sig[2:], 16)
            c = stats['a5_hist'].get('A5+%02X' % op, 0)
            check("decoded binary-mode signature A5 %02X == 0" % op, c == 0,
                  "%d escaped instrs" % c)
            evidence.append("%-58s %s" % ("  [info] raw-byte %s hits in "
                                          "code (not a failure condition)"
                                          % sig, code.count(bytes.fromhex(sig))))
        # --- secondary frequency evidence (v2 heuristics) ---
        # PURE INFO: these are statistical byte counts, never a failure
        # condition (README: "仅作参考不作为主判据").  The hard gate is the
        # instruction-boundary decode + decoded-signature rules above, so
        # these must not go through check()'s FAIL path.
        n7e = code.count(0x7E)
        evidence.append("%-58s %s" % ("  [info] bare 0x7E (source-mode mov "
                                      "wr,#imm16) byte count", n7e))
        legacy = code.count(0x74) + code.count(0x75) + code.count(0xF5)
        ratio = 100.0 * legacy / max(len(code), 1)
        evidence.append("%-58s %s" % ("  [info] bare 8051 74/75/F5 bytes "
                                      "(hwframe incident signature)",
                                      "%d (%.2f%% of %d)"
                                      % (legacy, ratio, len(code))))

        # --- UART1/Timer2 SFR store windows (STC32G layout) ---
        for name, hexpat in (
            ("SCON(0x98) = 0x50        ", '7e00507a0198'),
            ("T2L(0xD7) = 0xB8 [33MHz] ", '7e00b87a01d7'),
            ("T2H(0xD6) = 0xFF         ", '7e00ff7a01d6'),
            ("AUXR(0x8E) |= 0x01 S1BRT ", '7e018e4e00017a018e'),
            ("AUXR(0x8E) |= 0x04 T2x12 ", '7e018e4e00047a018e'),
            ("AUXR(0x8E) |= 0x10 T2R   ", '7e018e4e00107a018e'),
        ):
            pat = bytes.fromhex(hexpat)
            off = cseg.find(pat)
            check("CSEG window %s" % name, off >= 0,
                  "CSEG+0x%03x" % off if off >= 0 else "NOT FOUND")

        # --- negative: stores to 8052 T2 addresses must not exist ---
        for bad in ('7a01cc', '7a01cd'):
            c = cseg.count(bytes.fromhex(bad))
            check("CSEG store-to-0x%s (8052 T2 layout) == 0" % bad[2:],
                  c == 0, str(c))

        # --- placement sanity ---
        cseg_end = areas['s_CSEG'] + len(cseg)
        check("CSEG end 0x%06x < XINIT 0xFF8000" % cseg_end,
              cseg_end <= 0xff8000, "len=%d" % len(cseg))

    stats['records'] = n_records
    stats['data_records'] = n_data
    stats['image_bytes'] = len(mem)
    return failures, evidence, stats


# --------------------------------------------------------------------------
# Self-test: every corruption class must be caught
# --------------------------------------------------------------------------

def _write_tmp(text):
    fd, p = tempfile.mkstemp(suffix='.hex')
    with os.fdopen(fd, 'w') as f:
        f.write(text)
    return p


def _first_data_record_range(hex_path):
    with open(hex_path) as f:
        for raw in f:
            line = raw.strip()
            if line.startswith(':'):
                b = bytes.fromhex(line[1:])
                if b[3] == 0x00 and b[0] > 0:
                    base = (b[1] << 8) | b[2]
                    return base, b[0], line
    raise SystemExit("self-test: no data record found")


def self_test(hex_path, map_path):
    print("=== check-encoding self-test ===")
    ok = True

    def expect(name, cond, detail=""):
        nonlocal ok
        print("  %-52s %s %s" % (name, "caught" if cond else "MISSED",
                                 detail))
        ok = ok and cond

    # Baseline must pass.
    failures, _, _ = run_checks(hex_path, map_path)
    expect("baseline firmware passes", not failures, "")

    with open(hex_path) as f:
        lines = f.readlines()
    base, n, _ = _first_data_record_range(hex_path)

    # T1: corrupt a checksum.
    t1 = list(lines)
    b = bytearray(bytes.fromhex(t1[0].strip()[1:]))
    b[-1] ^= 0xff
    t1[0] = ':' + bytes(b).hex().upper() + '\n'
    p = _write_tmp(''.join(t1))
    try:
        parse_ihex(p)
        expect("T1 bad record checksum", False, "(parse succeeded)")
    except IhexError:
        expect("T1 bad record checksum", True)
    finally:
        os.unlink(p)

    # T2: count field mismatch.
    t2 = list(lines)
    b = bytearray(bytes.fromhex(t2[0].strip()[1:]))
    b[0] = (b[0] + 1) & 0xff
    b[-1] = (-(sum(b[:-1]))) & 0xff        # keep checksum consistent
    t2[0] = ':' + bytes(b).hex().upper() + '\n'
    p = _write_tmp(''.join(t2))
    try:
        parse_ihex(p)
        expect("T2 count field != payload bytes", False,
               "(parse succeeded)")
    except IhexError:
        expect("T2 count field != payload bytes", True)
    finally:
        os.unlink(p)

    # T3: missing EOF record.
    t3 = [l for l in lines if not
          (l.startswith(':') and
           bytes.fromhex(l.strip()[1:])[3] == 0x01)]
    p = _write_tmp(''.join(t3))
    try:
        parse_ihex(p)
        expect("T3 missing EOF record", False, "(parse succeeded)")
    except IhexError:
        expect("T3 missing EOF record", True)
    finally:
        os.unlink(p)

    # T4: garbage line.
    p = _write_tmp(''.join(lines) + 'GARBAGE\n')
    try:
        parse_ihex(p)
        expect("T4 garbage line", False, "(parse succeeded)")
    except IhexError:
        expect("T4 garbage line", True)
    finally:
        os.unlink(p)

    # T5: unknown record type.
    t5 = list(lines)
    b = bytearray(bytes.fromhex(t5[0].strip()[1:]))
    b[3] = 0x02
    b[-1] = (-(sum(b[:-1]))) & 0xff
    t5[0] = ':' + bytes(b).hex().upper() + '\n'
    p = _write_tmp(''.join(t5))
    try:
        parse_ihex(p)
        expect("T5 unknown record type 02", False, "(parse succeeded)")
    except IhexError:
        expect("T5 unknown record type 02", True)
    finally:
        os.unlink(p)

    # T6: illegal bare opcode injected into a FUNC body (checksum fixed so
    # only the decode gate can catch it).
    areas, _, funcs = parse_map(map_path)
    tgt = sorted(funcs, key=lambda f: -f[2])[0]     # largest function
    name, start, length = tgt
    p = _write_tmp(patch_ihex_bytes(hex_path, {start: 0xB7}))
    failures, ev, _ = run_checks(p, map_path)
    expect("T6 illegal opcode 0xB7 at %s+0" % name,
           any("FUNC %s" % name in f for f in failures),
           "(%d checks failed)" % len(failures))
    os.unlink(p)

    # T7: binary-mode signature A5 7E injected AT an instruction boundary:
    # replace the first 3 bytes of the SCON store window ("7E 00 50" = mov
    # wr0,#0x0050) inside _main with "A5 7E 50" (escaped classic mov
    # r6,#0x50).  Same 3-byte length, so the whole function still decodes
    # exactly -- only the decoded-signature rule can catch it.  (A raw-byte
    # A5 7E hit inside an immediate, e.g. native "7E 44 A5 7E", must NOT
    # fail -- that is asserted by the L4 positive length group below.)
    mem_t7, _, _ = parse_ihex(hex_path)
    pat = bytes.fromhex('7e00507a0198')     # SCON window (first instruction
    t7_addr = None                          # starts at its first byte)
    for a in range(start, start + length - len(pat)):
        if bytes(mem_t7[a + i] for i in range(len(pat))) == pat:
            t7_addr = a
            break
    if t7_addr is None:
        expect("T7 injected A5 7E binary-mode signature", False,
               "(SCON window pattern not found in %s)" % name)
    else:
        p = _write_tmp(patch_ihex_bytes(hex_path,
                                        {t7_addr: 0xA5,
                                         t7_addr + 1: 0x7E}))
        failures, ev, _ = run_checks(p, map_path)
        expect("T7 injected A5 7E binary-mode signature",
               any("signature A5 7E" in f for f in failures),
               "(%d checks failed)" % len(failures))
        os.unlink(p)

    # T8: non-printable byte planted in the string pool (data, not code).
    # Pool = end of _main .. start of the next .text contribution.
    main = [f for f in funcs if f[0] == '_main']
    if main:
        mstart, mlen = main[0][1], main[0][2]
        pool_addr = mstart + mlen
        p = _write_tmp(patch_ihex_bytes(hex_path, {pool_addr: 0xFF}))
        failures, ev, _ = run_checks(p, map_path)
        expect("T8 non-printable byte planted in string pool",
               any("data pool" in f for f in failures),
               "(%d checks failed)" % len(failures))
        os.unlink(p)
    else:
        expect("T8 non-printable byte planted in string pool", False,
               "(_main FUNC not found)")

    # --- Positive length group -------------------------------------------
    # instr_len() asserted against reference bytes that were assembled with
    # sdas251 V05.50.4 (source mode) and read back from the .lst before
    # being frozen here.  A mutation of the length tables (e.g. the old
    # 0x78..0x7F override that made "A5 7E 12" a 2-byte instruction) must
    # fail this group.
    length_groups = (
        ("L1 escaped classic mov-immediate 0x76..0x7F (all 3 bytes)",
         (('a57612', 3, 'mov @r0,#0x12'),
          ('a57734', 3, 'mov @r1,#0x34'),
          ('a57812', 3, 'mov r0,#0x12  (escaped classic form)'),
          ('a57e12', 3, 'mov r6,#0x12  (regression Alice found)'),
          ('a57f12', 3, 'mov r7,#0x12  (escaped classic form)'))),
        ("L2 other A5 escapes (escaped opcode low nibble >= 6 only)",
         (('a5e8', 2, 'mov a,r0'),
          ('a5fa', 2, 'mov r2,a'),
          ('a5e6', 2, 'mov a,@r0'),
          ('a5f6', 2, 'mov @r0,a'),
          ('a5c6', 2, 'xch a,@r0'),
          ('a5d6', 2, 'xchd a,@r0'),
          ('a5d7', 2, 'xchd a,@r1'),
          ('a598', 2, 'subb a,r0'),
          ('a5d8fd', 3, 'djnz r0,.'),
          ('a5b810fc', 4, 'cjne r0,#0x10,.+2'))),
        ("L3 bare classic (low nibble < 6, never escaped)",
         (('7455', 2, 'mov a,#0x55'),
          ('753055', 3, 'mov 0x30,#0x55'),
          ('854030', 3, 'mov 0x30,0x40'),
          ('901234', 3, 'mov dptr,#0x1234'),
          ('b020', 2, 'anl c,/0x20'),
          ('b220', 2, 'cpl 0x20 (cpl bit, sdas251)'),
          ('a020', 2, 'orl c,/0x20'),
          ('c0d0', 2, 'push psw'),
          ('80fe', 2, 'sjmp .'),
          ('40fe', 2, 'jc .'),
          ('13', 1, 'rrc a'),
          ('33', 1, 'rlc a'),
          ('c3', 1, 'clr c'),
          ('a4', 1, 'mul ab'),
          ('32', 1, 'reti'),
          ('22', 1, 'ret'),
          ('a3', 1, 'inc dptr'),
          ('e0', 1, 'movx a,@dptr'))),
        ("L4 native 251 forms (bare; emitter closed set + sdas251 gold)",
         (('7e0050', 3, 'mov wr0,#0x0050 (SCON window)'),
          ('7ef8010f', 4, 'mov spx,#0x010F (BOOT prologue)'),
          ('7e14a57e', 4, 'mov wr2,#0x7EA5 (A5 7E inside imm16: legal!)'),
          ('7a0198', 3, 'native 7A store form (SCON window bytes)'),
          ('7c3c', 2, 'mov r3,r12'),
          ('7a1930', 3, 'mov @wr2,r3'),
          ('7820', 2, 'jne .+2 (native rel8)'),
          ('0b24', 2, 'inc wr4'),
          ('1b24', 2, 'dec wr4'),
          ('0b1c', 2, 'inc dr4'),
          ('0b1820', 3, 'mov wr4,@wr2 (sdas251)'),
          ('1b1820', 3, 'mov @wr2,wr4 (sdas251)'),
          ('0bfa20', 3, 'mov wr4,@dr60 (sdas251)'),
          ('ad13', 2, 'mul wr2,wr6'),
          ('be2055', 3, 'cmp r2,#0x55'),
          ('be141234', 4, 'cmp wr2,#0x1234'),
          ('2e2055', 3, 'add r2,#0x55'),
          ('2e141234', 4, 'add wr2,#0x1234'),
          ('3e30', 2, 'sll r3'),
          ('1e70', 2, 'srl r7'),
          ('0e10', 2, 'sra r1'),
          ('3e14', 2, 'sll wr2'),
          ('09201234', 4, 'mov r2,@wr0+0x1234'),
          ('691f1234', 4, 'mov wr2,@dr60+0x1234'),
          ('8aff0000', 4, 'ejmp 24-bit'),
          ('9aff0200', 4, 'ecall 0xff0200'),
          ('aa', 1, 'eret'))),
    )
    for gname, entries in length_groups:
        bad = []
        for hexstr, want, note in entries:
            buf = bytes.fromhex(hexstr)
            try:
                got = instr_len(buf, 0)
            except DecodeError as e:
                bad.append("%s (%s): DecodeError %s" % (hexstr, note, e))
                continue
            if got != want:
                bad.append("%s (%s): len %d != %d" % (hexstr, note, got,
                                                      want))
        expect(gname, not bad, "; ".join(bad[:3]))

    # Boundary negatives in the length model itself: these byte patterns
    # are NOT legal source-mode instructions and must raise DecodeError.
    len_neg = (('76', 'bare classic low nibble >= 6 must be A5-escaped'),
               ('7b', 'bare 0x7B is in neither table (fail-closed)'),
               ('a575', 'A5 escape on classic low nibble < 6'),
               ('a590', 'A5 escape on classic low nibble < 6'),
               ('a5a5', 'A5 escape on A5'),
               ('a5b4', 'A5 escape on cjne a,#imm: B4 low nibble < 6 is '
                        'bare-only (never escaped)'))
    bad = []
    for hexstr, why in len_neg:
        try:
            instr_len(bytes.fromhex(hexstr), 0)
            bad.append("%s (%s) decoded without error" % (hexstr, why))
        except DecodeError:
            pass
    expect("L5 length-model boundary negatives raise DecodeError",
           not bad, "; ".join(bad))

    # --- Round-3 gap-closer positives (Alice review) ----------------------
    # The seven bare classic opcodes CLASSIC_LEN used to be missing; each
    # reference byte string below was read back from an sdas251 V05.50.4
    # source-mode .lst before being frozen here:
    #   cpl c            -> B3
    #   cjne a,#0x12,+9  -> B4 12 09   (legal bare classic CJNE, no A5,
    #   cjne a,0x30,+6  -> B5 30 06    not in the native 251 set)
    #   clr a            -> E4
    #   mov a,0x30       -> E5 30
    #   cpl a            -> F4
    #   mov 0x30,a       -> F5 30
    gap_groups = (
        ("L6 bare classic gap closers B3/B4/B5/E4/E5/F4/F5 (sdas251)",
         (('b3', 1, 'cpl c'),
          ('b41209', 3, 'cjne a,#0x12,+9'),
          ('b53006', 3, 'cjne a,0x30,+6'),
          ('e4', 1, 'clr a'),
          ('e530', 2, 'mov a,0x30'),
          ('f4', 1, 'cpl a'),
          ('f530', 2, 'mov 0x30,a'))),
    )
    for gname, entries in gap_groups:
        bad = []
        for hexstr, want, note in entries:
            buf = bytes.fromhex(hexstr)
            try:
                got = instr_len(buf, 0)
            except DecodeError as e:
                bad.append("%s (%s): DecodeError %s" % (hexstr, note, e))
                continue
            if got != want:
                bad.append("%s (%s): len %d != %d" % (hexstr, note, got,
                                                      want))
        expect(gname, not bad, "; ".join(bad[:3]))

    # The whole sdas251-assembled gap-closer stream must decode exactly at
    # instruction boundaries: the 7 instructions above + sjmp . = 8 instrs
    # / 15 bytes (bytes read back from the same sdas251 .lst).
    try:
        n, _, _ = decode_region(
            bytes.fromhex('b3b41209b53006e4e530f4f53080fe'),
            "gap-closer stream")
        expect("L6 sdas251 gap-closer stream decodes end-to-end "
               "(8 instrs / 15 bytes)", n == 8, "(%d instrs)" % n)
    except DecodeError as e:
        expect("L6 sdas251 gap-closer stream decodes end-to-end "
               "(8 instrs / 15 bytes)", False, str(e))

    # --- L7: CLASSIC_LEN reconciliation against the public 8051 map ------
    # The reference below is transcribed from the public 8051 instruction
    # map, family by family (A5 is not a classic opcode).  Any future edit
    # to CLASSIC_LEN that drops an entry, adds one outside the public map,
    # or changes a length fails here.  Divergences found in sdas251 source
    # mode would be frozen as measured with a README note; so far sdas251
    # and the public map agree on every opcode checked.
    pub = {}

    def _P(ops, ln):
        for o in ops:
            pub[o] = ln

    _P([0x00, 0x03, 0x04, 0x13, 0x14, 0x22, 0x23, 0x32, 0x33, 0x73, 0x83,
        0x84, 0x93, 0xA3, 0xA4, 0xB3, 0xC3, 0xC4, 0xD3, 0xD4, 0xE0, 0xE2,
        0xE3, 0xE4, 0xF0, 0xF2, 0xF3, 0xF4], 1)      # fixed 1-byte forms
    for _lo in (0x06, 0x16, 0x26, 0x36, 0x46, 0x56, 0x66, 0x96, 0xC6,
                0xE6, 0xF6):
        _P(range(_lo, _lo + 10), 1)   # @ri(2) + rn(8) 1-byte forms
    _P([0xD6, 0xD7], 1)               # xchd a,@ri
    _P(range(0xE8, 0xF0), 1)          # mov a,rn
    _P(range(0xF8, 0x100), 1)         # mov rn,a
    _P(range(0x01, 0x100, 0x10), 2)   # ajmp a11 (x1) / acall a11 (xF1)
    _P([0x05, 0x15, 0x24, 0x25, 0x34, 0x35, 0x40, 0x42, 0x44, 0x45, 0x50,
        0x52, 0x54, 0x55, 0x60, 0x62, 0x64, 0x65, 0x70, 0x72, 0x74, 0x80,
        0x82, 0x92, 0x94, 0x95, 0xA0, 0xA2, 0xB0, 0xB2, 0xC0, 0xC2, 0xC5,
        0xD0, 0xD2, 0xE5, 0xF5], 2)   # fixed 2-byte forms (40/50/60/70
                                      # rel8 branches, 42/52/62
                                      # orl/anl/xrl direct,A)
    for _lo in (0x76, 0x86, 0xA6):
        _P(range(_lo, _lo + 10), 2)   # mov #imm/@dir forms, 2 bytes
    _P(range(0xD8, 0xE0), 2)          # djnz rn,rel
    _P([0x02, 0x10, 0x12, 0x20, 0x30, 0x43, 0x53, 0x63, 0x75, 0x85, 0x90,
        0xB4, 0xB5, 0xD5], 3)         # fixed 3-byte forms
    _P(range(0xB6, 0xC0), 3)          # cjne rn/@ri,#imm,rel
    missing = sorted(o for o in range(256) if o != 0xA5 and o not in pub)
    if len(pub) != 255 or missing:
        expect("L7 public-map reference itself covers 255 classic opcodes",
               False, "reference incomplete: %s" %
               ["0x%02x" % o for o in missing[:6]])
    else:
        wrong = ["0x%02x: %s != %d" % (o, CLASSIC_LEN.get(o), pub[o])
                 for o in range(256) if o != 0xA5
                 and CLASSIC_LEN.get(o) != pub[o]]
        extra = ["0x%02x" % k for k in sorted(CLASSIC_LEN)
                 if k != 0xA5 and k not in pub]
        expect("L7 CLASSIC_LEN == public 8051 map (255 opcodes)",
               not wrong and not extra,
               "; ".join((wrong + extra)[:3]))

    print("=== self-test %s ===" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


# --------------------------------------------------------------------------

def main():
    argv = sys.argv[1:]
    selftest = False
    if argv and argv[0] == '--self-test':
        selftest = True
        argv = argv[1:]
    if len(argv) != 2:
        print("usage: check-encoding.py [--self-test] FIRMWARE.hex "
              "FIRMWARE.map", file=sys.stderr)
        return 2
    hex_path, map_path = argv
    if selftest:
        return self_test(hex_path, map_path)
    try:
        failures, evidence, stats = run_checks(hex_path, map_path)
    except IhexError as e:
        print("HEX CORRUPT: %s" % e, file=sys.stderr)
        return 2
    a5_e8ef = sum(v for k, v in stats['a5_hist'].items()
                  if k.startswith('A5+') and 0xE8 <= int(k[3:], 16) <= 0xEF)
    a5_f8ff = sum(v for k, v in stats['a5_hist'].items()
                  if k.startswith('A5+') and 0xF8 <= int(k[3:], 16) <= 0xFF)
    print("=== encoding-mode self-check: %s ===" % hex_path)
    print("image %d bytes, %d records (%d data); %d FUNC symbols; "
          "%d instructions decoded over %d code bytes; "
          "A5-prefixed %d (E8-EF: %d, F8-FF: %d); string pool %d bytes"
          % (stats['image_bytes'], stats['records'],
             stats['data_records'], stats['funcs'], stats['instrs'],
             stats['code_bytes'], stats['a5'], a5_e8ef, a5_f8ff,
             stats['pool_bytes']))
    for line in evidence:
        print("  " + line)
    if failures:
        print("FAIL (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("PASS: strict HEX; flash window 0x%06x..0x%06x; BOOT + all FUNC "
          "regions decode exactly as legal 251 SOURCE-mode instructions "
          "(A5 escapes all classic E8-FF families); .text gaps are pure "
          "string pools; no decoded-instruction binary-mode signatures; "
          "T2 @ D6/D7."
          % (FLASH_BASE, FLASH_BASE + FLASH_SIZE))
    return 0


if __name__ == '__main__':
    sys.exit(main())
