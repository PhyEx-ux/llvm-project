#!/usr/bin/env python3
"""check-bytes.py - X4 e2e byte-level assertions over the linked images.

Standalone (stdlib only, no LLVM product code imported; the same
independence discipline as check-crt-irq.py).  Inputs are the build
directory produced by build.sh:

  keil.elf/keil.map    Keil-dialect image (BYTE xdata / char code, cross-TU)
  modern.elf/modern.map  modern-qualifier image (__xdata/__code, cross-TU)
  fw.elf/fw.map        the QEMU firmware image
  *.o                  the llc ELF objects before linking

Checks (per XDATA-CODE-SLICE-TASK.md X4 acceptance):
  1. object placement: per-object `.mcs251.XSEG.<sym>` NOBITS sections with
     the exact sizes; extern-only TUs emit no placement; every object
     carries `.mcs251.xdata_init` records whose relocation triples
     (HI8/MID8/LO8) target the object symbols;
  2. link map: every XSEG slice lies in [0x010000, 0x020000) inside ONE
     64K window; the boundary symbols s_XSEG/l_XSEG/s_XDATA_INIT/
     l_XDATA_INIT are present and consistent;
  3. xdata_init record contents (linked image): each record parses as v1
     (u8 bank + u16 window BE + u16 object_size + u16 payload_size +
     payload), bank == 0x01, window == the low 16 bits of the object's
     allocated XSEG address, payload_size is 0 or the object size, and the
     payload bytes equal the source initializer;
  4. CODE image: DEVICEDESC/MODTAB/FWDESC byte images appear verbatim in
     the linked CODE window;
  5. DPXL discipline: decoding every mapped function at instruction
     boundaries (a source-mode length decoder ported from the QEMU
     target/mcs51/decode.c tables), every MOVX (E0/F0) has a DPXL region
     write (mov 0x84,rN = 7A <reg> 84) nearer than the previous MOVX -
     the X2 self-healing invariant.

Exit 0 with PASS lines; exit 1 with FAIL:<detail>.
"""

import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Frozen expectations (source of truth: src/*.c + src/initbytes.h).
# ---------------------------------------------------------------------------

BUF256 = [(i * 5 + 1) & 0xFF for i in range(256)]
EXT16 = [(0xA0 + i * 3) & 0xFF for i in range(16)]
MOD32 = [(i * 9 + 7) & 0xFF for i in range(32)]
FWEXT16 = [(0x5C + i * 7) & 0xFF for i in range(16)]
MODEXT16 = [(0xC3 - i) & 0xFF for i in range(16)]
DEVICEDESC = [0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
              0x34, 0x12, 0xEF, 0xCD, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00]
MODTAB = [0xC9, 0xFE, 0x4C, 0x6F, 0x77, 0x2D, 0x66, 0x72,
          0x6F, 0x6D, 0x2D, 0x49, 0x52, 0x2D, 0x74, 0x6F, 0x2D, 0x52]

XSEG_BASE = 0x010000
XSEG_LIMIT = 0x020000
XDATA_INIT_BASE = 0xFF9000

# image name -> (XSEG object name -> (size, payload or None))
EXPECT = {
    "keil": {"buf": (256, BUF256), "zbuf": (64, None),
             "ExtXbuf": (16, EXT16)},
    "modern": {"mbuf": (32, MOD32), "mzbuf": (8, None),
               "ModExt": (16, MODEXT16)},
    "fw": {"fwbuf": (64, BUF256[:64]), "fwzero": (32, None),
           "ExtXbuf": (16, FWEXT16)},
}
# image name -> CODE images that must appear verbatim somewhere in the
# linked CODE window
EXPECT_CODE = {"keil": {"DEVICEDESC": DEVICEDESC},
               "modern": {"MODTAB": MODTAB},
               "fw": {"FWDESC": DEVICEDESC}}

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

failures = 0
checks = 0


def ok(msg):
    global checks
    checks += 1
    print("PASS[%02d] %s" % (checks, msg))


def require(cond, msg):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print("FAIL: %s" % msg)


# ---------------------------------------------------------------------------
# Minimal ELF32 big-endian parser (ET_REL or ET_EXEC).
# ---------------------------------------------------------------------------

def parse_elf(path):
    data = Path(path).read_bytes()
    assert data[:4] == b"\x7fELF", path
    assert data[4] == 1 and data[5] == 2, "not ELF32/MSB: %s" % path
    (e_type, e_machine, _, _, e_phoff, e_shoff, _, _, _, _, e_shentsize,
     e_shnum, e_shstrndx) = struct.unpack(">HHIIIIIHHHHHH", data[16:52])
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link,
         sh_info, _, _) = struct.unpack(">10I", data[off:off + 40])
        sections.append({"name_off": sh_name, "type": sh_type,
                         "flags": sh_flags, "addr": sh_addr,
                         "offset": sh_offset, "size": sh_size,
                         "link": sh_link, "info": sh_info, "index": i})
    shstr = sections[e_shstrndx]
    strtab = data[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for s in sections:
        end = strtab.find(b"\x00", s["name_off"])
        s["name"] = strtab[s["name_off"]:end].decode("ascii")
        if s["type"] != SHT_NOBITS:
            s["data"] = data[s["offset"]:s["offset"] + s["size"]]
        else:
            s["data"] = b""
    # symbols
    symbols = []
    for s in sections:
        if s["type"] != SHT_SYMTAB:
            continue
        names = sections[s["link"]]["data"]
        for i in range(s["size"] // 16):
            (st_name, st_value, _, st_info, _, st_shndx) = struct.unpack(
                ">IIIBBH", s["data"][i * 16:(i + 1) * 16])
            end = names.find(b"\x00", st_name)
            nm = names[st_name:end].decode("ascii") if st_name else ""
            symbols.append({"name": nm, "value": st_value,
                            "bind": st_info >> 4, "type": st_info & 0xF,
                            "shndx": st_shndx})
    return {"type": e_type, "machine": e_machine, "sections": sections,
            "symbols": symbols}


# ---------------------------------------------------------------------------
# MCS-251 source-mode instruction length decoder (ported from the QEMU
# target/mcs51/decode.c tables; evidence: qemu-system-mcs251 runs the same
# decoder on the linked images).
# ---------------------------------------------------------------------------

def _classic_length(op):
    if (op & 0x1F) in (0x01, 0x11):
        return 2
    group = op >> 3
    if group in (0x0F, 0x11, 0x15, 0x1B):
        return 2
    if group == 0x17:
        return 3
    if op in (0x02, 0x12, 0x10, 0x20, 0x30, 0x43, 0x53, 0x63, 0x75, 0x85,
              0x90, 0xB4, 0xB5, 0xB6, 0xB7, 0xD5):
        return 3
    if op in (0x05, 0x15, 0x24, 0x25, 0x34, 0x35, 0x40, 0x42, 0x44, 0x45,
              0x50, 0x52, 0x54, 0x55, 0x60, 0x62, 0x64, 0x65, 0x70, 0x72,
              0x74, 0x76, 0x77, 0x80, 0x82, 0x86, 0x87, 0x92, 0x94, 0x95,
              0xA0, 0xA2, 0xA6, 0xA7, 0xB0, 0xB2, 0xC0, 0xC2, 0xC5, 0xD0,
              0xD2, 0xE5, 0xF5):
        return 2
    return 1


def _source_extra(code, pos):
    """Return the operand length after the opcode (source mode, low nibble
    > 5).  code/pos index the full byte stream; mirrors decode.c, including
    the one specifier byte the read-specifier cases always consume."""
    op = code[pos]
    if (op & 0x0F) <= 5:
        return _classic_length(op) - 1
    if op in (0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x78):
        return 1
    if op in (0x09, 0x19, 0x29, 0x39, 0x49, 0x59, 0x69, 0x79):
        return 3
    if op in (0x0A, 0x1A, 0x0E, 0x1E, 0x2C, 0x2D, 0x2F, 0x3E, 0x4C, 0x4D,
              0x5C, 0x5D, 0x6C, 0x6D, 0x7C, 0x7D, 0x7F, 0x89, 0x8C, 0x8D,
              0x99, 0x9C, 0x9D, 0x9F, 0xAC, 0xAD, 0xBC, 0xBD, 0xBF):
        return 1
    if op in (0x8A, 0x9A):
        return 3
    if pos + 1 >= len(code):
        return 0
    spec = code[pos + 1]
    if op == 0xA9:
        mode = (spec >> 3) & 0x1F
        return 1 + (2 if mode in (M_BIT_JBC, M_BIT_JB, M_BIT_JNB) else 1)
    if op in (0x0B, 0x1B):
        return 1 + (1 if spec & 0x0F in (0x8, 0xA) else 0)
    if op in (0x2E, 0x4E, 0x5E, 0x6E, 0x7E, 0x9E, 0xBE):
        mode = spec & 0x0F
        if mode in (0x0, 0x1, 0x5, 0x9, 0xB, 0xD):
            return 1 + 1
        if mode in (0x3, 0x4, 0x7, 0x8, 0xC, 0xF):
            return 1 + 2
        return 1
    if op == 0x7A:
        mode = spec & 0x0F
        if mode in (0x1, 0x5, 0x9, 0xB, 0xD):
            return 1 + 1
        if mode in (0x3, 0x7, 0xC, 0xF):
            return 1 + 2
        return 1
    if op in (0xCA, 0xDA):
        if op == 0xCA and spec == 0x02:
            return 1 + 1
        if op == 0xCA and spec == 0x06:
            return 1 + 2
        return 1
    return 0


M_BIT_JBC, M_BIT_JB, M_BIT_JNB = 0x02, 0x04, 0x06  # cpu.h BIT_SPECIFIER
                                                                 # OPERATION field


def decode_insns(code, base=0):
    """Yield (offset, length, opcode, operands) at instruction boundaries.

    The 0xA5 escape prefixes one instruction and flips its mode for THAT
    instruction only (decode.c re-derives the mode per instruction); the
    yielded offset/length always cover the prefix byte too.
    """
    pos = 0
    while pos < len(code):
        start = pos
        mode = True
        op = code[pos]
        while op == 0xA5:
            mode = not mode
            pos += 1
            if pos >= len(code):
                raise ValueError("truncated escape at 0x%x" % (base + start))
            op = code[pos]
        if mode and op in (0xA9, 0x0B, 0x1B, 0x2E, 0x4E, 0x5E,
                           0x6E, 0x7E, 0x9E, 0xBE, 0x7A, 0xCA, 0xDA) \
                and pos + 1 >= len(code):
            raise ValueError("missing specifier at 0x%x" % (base + start))
        extra = _source_extra(code, pos) if mode else _classic_length(op) - 1
        length = (pos - start) + 1 + extra
        if start + length > len(code):
            raise ValueError("truncated operands at 0x%x" % (base + start))
        # Operands exclude both prefixes and opcode. A5 7A 84 is classic
        # mov R2,#84, NOT source-mode mov direct,rN (7A N1 direct).
        yield start, length, op, code[pos + 1:pos + 1 + extra]
        pos = start + length


def is_lane_write(op, operands, dest):
    return (op == 0x7A and len(operands) == 2
            and operands[0] & 0x0F == 1 and operands[1] == dest)


def check_decoder():
    # Escapes affect one instruction only; repeated escapes toggle twice.
    require(list(decode_insns(bytes.fromhex("A5 7A 84 F0"))) ==
            [(0, 3, 0x7A, b"\x84"), (3, 1, 0xF0, b"")],
            "decoder: escaped classic immediate must not fake a DPXL write")
    require(list(decode_insns(bytes.fromhex("A5 A5 7A E1 84 F0"))) ==
            [(0, 5, 0x7A, bytes.fromhex("E1 84")), (5, 1, 0xF0, b"")],
            "decoder: even escapes restore source mode")
    require(is_lane_write(0x7A, bytes.fromhex("E1 84"), 0x84)
            and not is_lane_write(0x7A, bytes.fromhex("E9 84"), 0x84),
            "decoder: only byte-direct stores count as lane writes")
    for raw in ("A5", "7A", "7A E1", "A5 7A", "9A FF"):
        try:
            list(decode_insns(bytes.fromhex(raw)))
        except ValueError:
            require(True, "decoder: rejects truncated " + raw)
        else:
            require(False, "decoder: accepted truncated " + raw)


# ---------------------------------------------------------------------------
# Map parsing.
# ---------------------------------------------------------------------------

def parse_map(path):
    syms = {}
    funcs = []      # (addr, size, name)
    secs = []       # (file, section, addr, size)
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if "=" in line and " +0x" not in line and line.count("=") == 1:
            name, _, val = line.partition("=")
            try:
                syms[name.strip()] = int(val.strip(), 16)
            except ValueError:
                pass
        elif line.startswith("FUNC "):
            _, a, s, n = line.split()
            funcs.append((int(a, 16), int(s[1:], 16), n))
        elif " +0x" in line and ":" in line:
            where, a, s = line.split()
            f, _, secname = where.rpartition(":")
            secs.append((f, secname, int(a, 16), int(s[1:], 16)))
    return syms, funcs, secs


# ---------------------------------------------------------------------------
# xdata_init record parsing (v1, frozen DESIGN-SUPPLEMENT section 7).
# ---------------------------------------------------------------------------

def parse_records(blob):
    out = []
    off = 0
    while off < len(blob):
        assert len(blob) - off >= 7, "truncated record"
        bank = blob[off]
        window = (blob[off + 1] << 8) | blob[off + 2]
        obj = (blob[off + 3] << 8) | blob[off + 4]
        pay = (blob[off + 5] << 8) | blob[off + 6]
        payload = blob[off + 7:off + 7 + pay]
        assert len(payload) == pay
        out.append({"bank": bank, "window": window, "object_size": obj,
                    "payload_size": pay, "payload": payload})
        off += 7 + pay
    assert off == len(blob), "trailing garbage"
    return out


# ---------------------------------------------------------------------------
# Checks.
# ---------------------------------------------------------------------------

def check_object(out, name, expects):
    elf = parse_elf(out / (name + ".o"))
    secs = {s["name"]: s for s in elf["sections"]}
    for sym, (size, payload) in expects.items():
        sec = ".mcs251.XSEG._" + sym
        require(sec in secs and secs[sec]["type"] == SHT_NOBITS
                and secs[sec]["size"] == size,
                "%s.o: %s must be a %d-byte NOBITS XSEG section" %
                (name, sec, size))
    require(".mcs251.xdata_init" in secs
            and secs[".mcs251.xdata_init"]["type"] == SHT_PROGBITS,
            "%s.o: .mcs251.xdata_init must be PROGBITS" % name)
    # every XSEG record header carries the HI8/MID8/LO8 triple of its symbol
    relsecs = [s for s in elf["sections"] if s["type"] == SHT_RELA
               and s["info"] == secs[".mcs251.xdata_init"]["index"]]
    require(len(relsecs) == 1, "%s.o: expected one .rela.mcs251.xdata_init"
            % name)
    triples = {}
    for i in range(relsecs[0]["size"] // 12):
        _, r_info, _ = struct.unpack(
            ">IIi", relsecs[0]["data"][i * 12:(i + 1) * 12])
        symidx, rtype = r_info >> 8, r_info & 0xFF
        nm = elf["symbols"][symidx]["name"]
        triples.setdefault(nm, set()).add(rtype)
    for sym in expects:
        require(triples.get("_" + sym) == {3, 4, 5},
                "%s.o: record for _%s must carry HI8/MID8/LO8 (got %s)"
                % (name, sym, sorted(triples.get("_" + sym, ()))))
    ok("%s.o: %d per-object XSEG sections + records with 24-bit "
       "relocation triples" % (name, len(expects)))


def check_image(out, name, expects, code_images):
    elf = parse_elf(out / (name + ".elf"))
    syms, funcs, secs = parse_map(out / (name + ".map"))

    # The linker emits flat load images (.mcs251.load.N with sh_addr);
    # build an address-indexed view of the linked bytes.
    loads = [s for s in elf["sections"]
             if s["type"] == SHT_PROGBITS and s["flags"] & SHF_ALLOC
             and s["size"]]

    def slice_at(addr, size):
        for s in loads:
            if s["addr"] <= addr and addr + size <= s["addr"] + s["size"]:
                return s["data"][addr - s["addr"]:addr + size - s["addr"]]
        return None

    # --- XSEG allocation (map) --------------------------------------------
    xseg = [(secname, addr, size) for f, secname, addr, size in secs
            if secname.startswith(".mcs251.XSEG")]
    require(len(xseg) == len(expects),
            "%s: %d XSEG sections in map, expected %d"
            % (name, len(xseg), len(expects)))
    placed = {}
    for secname, addr, size in xseg:
        require(XSEG_BASE <= addr and addr + size <= XSEG_LIMIT,
                "%s: XSEG slice %s outside [0x%x,0x%x)"
                % (name, secname, XSEG_BASE, XSEG_LIMIT))
        require((addr ^ (addr + size - 1)) & 0xFF0000 == 0,
                "%s: XSEG slice %s straddles a 64K window" % (name, secname))
        placed[secname[len(".mcs251.XSEG._"):]] = (addr, size)
    require(syms.get("s_XSEG") == XSEG_BASE and "l_XSEG" in syms,
            "%s: map boundary symbols s_XSEG/l_XSEG missing" % name)
    require(syms.get("s_XDATA_INIT") == XDATA_INIT_BASE,
            "%s: s_XDATA_INIT must be 0x%x (got 0x%s)"
            % (name, XDATA_INIT_BASE, syms.get("s_XDATA_INIT")))
    # --- records (linked image, sliced at [s_XDATA_INIT, +l_XDATA_INIT)) ---
    span = syms.get("l_XDATA_INIT", 0)
    blob = slice_at(XDATA_INIT_BASE, span)
    require(blob is not None,
            "%s: no linked load image covers the XDATA_INIT area" % name)
    records = parse_records(blob or b"")
    by_window = {r["window"]: r for r in records}
    for sym, (size, payload) in expects.items():
        require(sym in placed and placed[sym][1] == size,
                "%s: %s not placed with size %d" % (name, sym, size))
        addr = placed[sym][0]
        r = by_window.get(addr & 0xFFFF)
        require(r is not None, "%s: no record targeting %s's window 0x%04x"
                % (name, sym, addr & 0xFFFF))
        if r:
            require(r["bank"] == (addr >> 16) & 0xFF,
                    "%s: record for %s has bank 0x%02x, object at 0x%06x"
                    % (name, sym, r["bank"], addr))
            require(r["object_size"] == size,
                    "%s: record for %s has object_size %d, expected %d"
                    % (name, sym, r["object_size"], size))
            if payload is None:
                require(r["payload_size"] == 0,
                        "%s: clear-only record for %s has payload_size %d"
                        % (name, sym, r["payload_size"]))
            else:
                require(r["payload_size"] == size
                        and list(r["payload"]) == payload,
                        "%s: payload record for %s content mismatch" %
                        (name, sym))
    total = sum(7 + r["payload_size"] for r in records)
    require(syms.get("l_XDATA_INIT") == total,
            "%s: l_XDATA_INIT 0x%s != computed record span 0x%x"
            % (name, syms.get("l_XDATA_INIT"), total))
    ok("%s: %d XSEG slices in [0x%x,0x%x), %d records (bank/window/"
       "sizes/payloads verified)" % (name, len(xseg), XSEG_BASE, XSEG_LIMIT,
                                     len(records)))

    # --- CODE images --------------------------------------------------------
    code_blob = b"".join(s["data"] for s in loads)
    for tbl, bytes_ in code_images.items():
        require(bytes(bytes_) in code_blob,
                "%s: CODE image %s not found verbatim in the CODE window"
                % (name, tbl))
    ok("%s: %d CODE table images verified verbatim in the CODE window"
       % (name, len(code_images)))

    # --- DPXL discipline -----------------------------------------------------
    # Every mapped function is decoded at instruction boundaries; every
    # MOVX (E0/F0) must have a nearer DPXL region write (mov 0x84,rN =
    # 7A <reg> 84) than the previous MOVX.
    movx_total = 0
    bad = 0
    for faddr, fsize, fname in funcs:
        blob = slice_at(faddr, fsize)
        if blob is None:
            continue
        last_dpxl = -1  # index of the last DPXL-writing instruction
        last_movx = -1
        insns = list(decode_insns(blob))
        for idx, (off, length, op, operands) in enumerate(insns):
            if is_lane_write(op, operands, 0x84):
                last_dpxl = idx
            elif op in (0xE0, 0xF0):
                movx_total += 1
                if not (last_movx < last_dpxl <= idx - 1):
                    bad += 1
                    if bad <= 3:
                        print("  %s: MOVX at %s+0x%x has no DPXL write "
                              "since the previous MOVX"
                              % (name, fname, off))
                last_movx = idx
    require(bad == 0,
            "%s: %d MOVX instructions violate the DPXL self-healing "
            "invariant" % (name, bad))
    ok("%s: %d MOVX instructions across %d mapped functions, each with a "
       "nearer DPXL region write (X2 invariant)"
       % (name, movx_total, len(funcs)))




def check_defect(out):
    """Green -O2 pin over the defect-o2 image (X2 fix landed).

    store3() (src/defect-repro.c) at -O2 used to emit its three XDATA
    stores as per-register streams (DPXL x3, DPL x3, DPH x3, A x3) followed
    by three bare MOVX - all stores then hit the last-written pointer
    (QEMU-confirmed: only i=0 survived).  The X2 fix (Glue-welded MOVX byte
    sequences in buildMOVXByteLoad/Store, pinned by
    llvm/test/CodeGen/MCS251/xdata-o2-order.ll) makes every MOVX follow its
    own lane writes, so the pin now asserts the GREEN shape: exactly three
    MOVX in _store3, each preceded - since the previous MOVX - by its own
    DPXL (mov 0x84,rN), DPL (mov 0x82,rN) and DPH (mov 0x83,rN) writes,
    and no bare-MOVX run.  QEMU read-back of the same -O2 shape was
    verified out-of-band (R=5AC381 O2-STORE3-PASS).
    """
    elf = parse_elf(out / "defect-o2.elf")
    syms, funcs, secs = parse_map(out / "defect-o2.map")
    loads = [s for s in elf["sections"]
             if s["type"] == SHT_PROGBITS and s["flags"] & SHF_ALLOC
             and s["size"]]

    def slice_at(addr, size):
        for s in loads:
            if s["addr"] <= addr and addr + size <= s["addr"] + s["size"]:
                return s["data"][addr - s["addr"]:addr + size - s["addr"]]
        return None

    f = [(a, sz) for a, sz, n in funcs if n == "_store3"]
    require(len(f) == 1, "defect-o2: _store3 missing from the map")
    blob = slice_at(f[0][0], f[0][1])
    insns = list(decode_insns(blob))
    movx_total = 0
    bad = 0
    barest = 0
    run = 0
    have = {"0x84": False, "0x82": False, "0x83": False}
    for off, length, op, ops in insns:
        if any(is_lane_write(op, ops, dest) for dest in (0x82, 0x83, 0x84)):
            have["0x%02x" % ops[1]] = True
            run = 0
        elif op in (0xE0, 0xF0):
            movx_total += 1
            if op != 0xF0 or not all(have.values()):
                bad += 1
                if bad <= 3:
                    print("  defect-o2/_store3: MOVX at +0x%x lacks its own "
                          "DPXL/DPL/DPH lane writes since the previous MOVX"
                          % off)
            have = {"0x84": False, "0x82": False, "0x83": False}
            run += 1
            barest = max(barest, run)
        elif op in (0x80, 0x68, 0x69, 0x8A, 0x9A, 0xAA):
            run = 0  # control flow ends a bare run
    require(movx_total == 3,
            "defect-o2/_store3: expected 3 MOVX stores, found %d"
            % movx_total)
    require(bad == 0,
            "defect-o2/_store3: %d MOVX without complete preceding lane "
            "writes (X2 -O2 defect shape)" % bad)
    require(barest <= 1,
            "defect-o2/_store3: bare MOVX run of %d (defect signature)"
            % barest)
    ok("defect-o2/_store3 (X2 fix): %d MOVX, each preceded by its own "
       "DPXL/DPL/DPH lane writes since the previous MOVX; longest bare-MOVX "
       "run %d" % (movx_total, barest))

def main():
    if len(sys.argv) != 2:
        print("usage: check-bytes.py BUILD_DIR", file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    check_decoder()

    # object-level placement (definitions before linking)
    check_object(out, "keil-form", {"buf": (256, BUF256), "zbuf": (64, None)})
    check_object(out, "keil-extern", {"ExtXbuf": (16, EXT16)})
    check_object(out, "modern-form", {"mbuf": (32, MOD32), "mzbuf": (8, None)})
    check_object(out, "modern-extern", {"ModExt": (16, MODEXT16)})
    check_object(out, "qemu-fw", {"fwbuf": (64, BUF256[:64]),
                                  "fwzero": (32, None)})
    check_object(out, "qemu-ext", {"ExtXbuf": (16, FWEXT16)})
    # extern-only usage: the using TU must not define storage for the extern
    # object it declares (the specific object's section is absent).
    for obj, extern in (("keil-form", "ExtXbuf"), ("modern-form", "ModExt"),
                        ("qemu-fw", "ExtXbuf")):
        oelf = parse_elf(out / (obj + ".o"))
        require((".mcs251.XSEG._" + extern) not in
                [s["name"] for s in oelf["sections"]],
                "%s.o unexpectedly defines an XSEG object for its extern "
                "declaration of %s" % (obj, extern))
    ok("extern declarations emit no storage in the using TU")

    # linked images
    check_image(out, "keil", EXPECT["keil"], EXPECT_CODE["keil"])
    check_image(out, "modern", EXPECT["modern"], EXPECT_CODE["modern"])
    check_image(out, "fw", EXPECT["fw"], EXPECT_CODE["fw"])
    check_defect(out)

    print()
    if failures:
        print("check-bytes: FAIL (%d failed / %d checks)" % (failures, checks))
        return 1
    print("check-bytes: PASS (%d checks)" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
