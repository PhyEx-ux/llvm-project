#!/usr/bin/env python3
# check.py - static acceptance assertions for the asm2elf E4 demo.
#
# Parses the linked ET_EXEC image (produced with --keep-symbols) -- and,
# for assertion 8, the compiler-produced C caller object cabi.o -- directly
# and asserts, without any emulator:
#   1. the final symbol table carries every asm and C entry with the
#      expected types/sizes (E5 traceability through --keep-symbols);
#   2. cross-object call sites carry the callee's final address
#      (J16 lcall asm->asm, 24-bit ecall asm->C, 24-bit ecall asm->asm);
#   3. relative and paged jumps resolve to their targets (PC8 sjmp
#      cross-object, J11 acall/ajmp intra- and cross-object);
#   4. call/return frame pairing: every ecall (extended frame) targets a
#      function whose body ends in ERET (0xAA) and every lcall/acall (near
#      frame) targets a function whose body ends in RET (0x22) -- no
#      function is called through both frames;
#   5. the asm -> C call prepares the int c_add(int,int) ABI arguments:
#      i32 arg0 in DPL:DPH:B:A (LSB first, direct SFR stores) and i32 arg1
#      in the static slot _c_add_PARM_2 (4 direct stores, big-endian in
#      memory: 33 44 55 66 at ascending addresses for 0x33445566 --
#      MCS251ISelLowering.cpp:2529-2537, measured on hardware);
#   6. data references land on the right addresses (CSEG const table,
#      C global in DSEG via 16-bit and direct 8-bit references, XSEG
#      reservation);
#   7. map FUNC rows agree with the ELF symbol table (function size
#      propagation from .rel gaps through sdrel2elf to the map);
#   8. independent ABI oracle: the compiler-produced C caller cabi.o
#      (demo/cabi.c, built by run.sh with the current clang/llc) performs
#      the same c_add(0x44332211, 0x33445566) call; its slot stores are
#      decoded from its own relocations and must equal both the documented
#      big-endian value and liba.asm's hand-written stores byte for byte,
#      so the hand-written object and the static expectation cannot agree
#      on a wrong byte order together.
#
# Vector consistency does not apply here: this link is non-IRQ (no
# .mcs251.isr metadata can be produced by sdas251, and R3 forbids
# registering a foreign object's function), so the linker synthesizes no
# vector table.  See README.md.

import struct
import subprocess
import sys

PHDR_SIZE = 32
SHDR_SIZE = 40

RET_OPCODE = 0x22   # near-frame return (source-mode encoding)
ERET_OPCODE = 0xAA  # extended-frame return


def die(msg):
    sys.stderr.write("check: FAIL: %s\n" % msg)
    sys.exit(1)


def check(cond, msg):
    if not cond:
        die(msg)
    print("check: ok: %s" % msg)


class Image(object):
    def __init__(self, path):
        with open(path, "rb") as f:
            data = f.read()
        check(data[:4] == b"\x7fELF", "linked file is ELF")
        self.data = data
        (e_type, e_machine) = struct.unpack_from(">HH", data, 16)
        check(e_type == 2, "linked file is ET_EXEC")
        check(e_machine == 0x9999, "linked file is EM_MCS251")
        phoff = struct.unpack_from(">I", data, 28)[0]
        phnum = struct.unpack_from(">H", data, 44)[0]
        self.mem = {}
        for i in range(phnum):
            (_t, off, _va, pa, filesz, _memsz, _fl, _al) = \
                struct.unpack_from(">8I", data, phoff + i * PHDR_SIZE)
            for j in range(filesz):
                self.mem[pa + j] = data[off + j]
        self.symbols = self._symbols()

    def _symbols(self):
        data = self.data
        shoff = struct.unpack_from(">I", data, 32)[0]
        shnum = struct.unpack_from(">H", data, 48)[0]
        syms = {}
        for i in range(shnum):
            (name, typ, _fl, _addr, off, size, link, _info, _align,
             entsize) = struct.unpack_from(">10I", data, shoff + i * SHDR_SIZE)
            if typ != 2:  # SHT_SYMTAB
                continue
            strtab_off = struct.unpack_from(
                ">I", data, shoff + link * SHDR_SIZE + 16)[0]
            for j in range(size // 16):
                (nameoff, value, sz, info, _other, shndx) = \
                    struct.unpack_from(">IIIBBH", data, off + j * 16)
                if nameoff == 0:
                    continue
                end = data.index(b"\0", strtab_off + nameoff)
                nm = data[strtab_off + nameoff:end].decode()
                syms[nm] = {
                    "value": value,
                    "size": sz,
                    "type": info & 0xF,
                    "bind": info >> 4,
                    "shndx": shndx,
                }
        return syms

    def byte(self, addr):
        return self.mem[addr]

    def u16(self, addr):
        return (self.mem[addr] << 8) | self.mem[addr + 1]

    def u24(self, addr):
        return (self.mem[addr] << 16) | (self.mem[addr + 1] << 8) | \
            self.mem[addr + 2]


def find_code(image, lo, hi, pred):
    """Offsets in [lo, hi) whose instruction bytes satisfy pred."""
    return [a for a in range(lo, hi) if pred(a)]


R_MCS251_LO8 = 3  # .rela.text relocation type of the slot-store anchor


def c_caller_slot_stores(path):
    """Decode the compiler-produced _c_add_PARM_2 slot stores in the C
    caller object (demo/cabi.c) as {slot byte offset: immediate}.

    The decode is anchored on the object's own relocations, not on any
    hand-written byte expectation: llc materializes the slot address once
    through R_MCS251_MID8/LO8/HI8 relocations against _c_add_PARM_2
    (addend 0) for the slot+0 store, and the stored immediate sits 4 bytes
    before the LO8 relocation offset, behind llc's `7E 40 imm8` value-load
    prefix.  The remaining three stores are llc's register-relative form
    `7E 40 imm8` followed by `39 40 00 N` with a literal slot offset N.
    If llc changes this store form, the checks below fail loudly and this
    decoder must be updated alongside the compiler.
    """
    with open(path, "rb") as f:
        data = f.read()
    check(data[:4] == b"\x7fELF", "%s is ELF" % path)
    e_type, e_machine = struct.unpack_from(">HH", data, 16)
    check(e_type == 1 and e_machine == 0x9999,
          "%s is an EM_MCS251 ET_REL object" % path)
    shoff = struct.unpack_from(">I", data, 32)[0]
    shnum = struct.unpack_from(">H", data, 48)[0]
    secs = [struct.unpack_from(">10I", data, shoff + i * SHDR_SIZE)
            for i in range(shnum)]
    rela = [s for s in secs if s[1] == 4]  # SHT_RELA (.rela.text)
    check(len(rela) == 1, "%s carries exactly one RELA section" % path)
    rela = rela[0]
    text, symtab = secs[rela[7]], secs[rela[6]]
    strtab = secs[symtab[6]]

    def sym_name(idx):
        nameoff, = struct.unpack_from(">I", data, symtab[4] + idx * 16)
        end = data.index(b"\0", strtab[4] + nameoff)
        return data[strtab[4] + nameoff:end].decode()

    body = data[text[4]:text[4] + text[5]]
    lo8 = []
    for j in range(rela[5] // 12):
        off, info, addend = struct.unpack_from(">IIi", data, rela[4] + j * 12)
        if sym_name(info >> 8) == "_c_add_PARM_2":
            if info & 0xFF == R_MCS251_LO8:
                lo8.append((off, addend))
    check(len(lo8) == 1 and lo8[0][1] == 0,
          "%s materializes the slot address exactly once via "
          "R_MCS251_LO8(_c_add_PARM_2) with addend 0" % path)
    anchor = lo8[0][0]
    check(anchor >= 6 and body[anchor - 6:anchor - 4] == b"\x7e\x40",
          "%s slot+0 store follows the llc `7E 40 imm8` value-load form"
          % path)
    stores = {0: body[anchor - 4]}
    for i in range(anchor, len(body) - 3):
        if body[i:i + 3] == b"\x39\x40\x00" and body[i + 3] in (1, 2, 3):
            n = body[i + 3]
            check(body[i - 3:i - 1] == b"\x7e\x40" and n not in stores,
                  "%s slot+%d store follows the llc `7E 40 imm8` + "
                  "`39 40 00 N` form" % (path, n))
            stores[n] = body[i - 1]
    return stores


def main(outdir):
    image = Image("%s/demo.elf" % outdir)
    syms = image.symbols
    B = 0xFF0000  # CSEG base used by run.sh

    # --- 1. symbol table --------------------------------------------------
    # Sizes follow the documented .rel gap rule: distance to the next
    # defined global of the same section (section end for the last one).
    # _c_add's size is the real C .size emitted by llc.
    expect_funcs = {
        "_asm_entry": (B, 0x43),
        "_asm_local": (B + 0x43, 2),
        "_asm_helper_b": (B + 0x45, 7),
        "_asm_helper_x": (B + 0x4C, 3),
        "_asm_resume": (B + 0x4F, 3),
        "_asm_table": (B + 0x52, 4),
        "_c_add": (B + 0x56, 0x30),
    }
    for name, (addr, size) in expect_funcs.items():
        s = syms.get(name)
        check(s is not None, "final ELF defines %s" % name)
        check(s["type"] == 2, "%s has STT_FUNC in the final ELF" % name)
        check(s["value"] == addr,
              "%s value is 0x%X" % (name, addr))
        if size is not None:
            check(s["size"] == size, "%s size is 0x%X" % (name, size))
    for name in ("_bvar", "_c_flag", "_asm_xbuf", "_c_add_PARM_2"):
        check(name in syms, "final ELF defines %s" % name)
    check(syms["_bvar"]["value"] == 0x30, "_bvar allocated at 0x30 (DSEG)")
    check(syms["_c_flag"]["value"] == 0x31, "_c_flag allocated at 0x31")
    parm2 = syms["_c_add_PARM_2"]["value"]
    check(0x20 <= parm2 <= 0x7F,
          "_c_add_PARM_2 at 0x%X (direct-addressable overlay slot)" % parm2)
    check(syms["_asm_table"]["value"] == B + 0x52,
          "_asm_table lands at 0x%X (cross-object CSEG concat)" % (B + 0x52))
    check(syms["_asm_xbuf"]["value"] == 0x12000,
          "_asm_xbuf lands on the XSEG area start")

    # --- 2. cross-object calls -------------------------------------------
    entry = syms["_asm_entry"]["value"]
    span = syms["_asm_entry"]["size"]
    check(image.byte(entry) == 0x12, "_asm_entry opens with lcall")
    # J16 fields carry the low 16 bits of the target (the linker proves the
    # 64K region is shared), so compare against addr & 0xFFFF.
    check(image.u16(entry + 1) == (syms["_asm_helper_b"]["value"] & 0xFFFF),
          "lcall at _asm_entry targets _asm_helper_b (cross-object)")
    # asm -> C: the ecall at entry+0x21 must target _c_add.
    check(image.byte(entry + 0x21) == 0x9A,
          "ecall opcode at entry+0x21 (asm -> C call)")
    check(image.u24(entry + 0x22) == syms["_c_add"]["value"],
          "ecall at entry+0x21 targets _c_add (R_MCS251_24 asm -> C call)")
    # 24-bit ecall to the extended-frame asm helper at entry+0x3A.
    check(image.byte(entry + 0x3A) == 0x9A,
          "ecall opcode at entry+0x3A")
    check(image.u24(entry + 0x3B) == syms["_asm_helper_x"]["value"],
          "ecall at entry+0x3A targets _asm_helper_x (cross-object)")

    # --- 3. absolute / relative / paged jumps -----------------------------
    # PC8: sjmp opcode 0x80 at entry+3, displacement byte at entry+4.
    check(image.byte(entry + 3) == 0x80, "sjmp opcode at entry+3")
    disp = (image.byte(entry + 4) ^ 0x80) - 0x80  # sign-extend 8 bits
    check(entry + 5 + disp == syms["_asm_resume"]["value"],
          "sjmp displacement reaches _asm_resume (cross-object PC8)")
    # J16 absolute intra-object ljmp at the end of liba: opcode 0x02.
    # The J16 field carries the low 16 bits of the target (the linker
    # proves the 64K region is shared), so OR the region back for compare.
    ljmps = find_code(image, entry, entry + span,
                      lambda a: image.byte(a) == 0x02)
    check(len(ljmps) == 1, "exactly one ljmp in liba")
    target = 0xFF0000 | image.u16(ljmps[0] + 1)
    # _asm_back sits at entry+5 but is not .globl, so it is not part of the
    # .rel symbol table (a documented limitation); assert the numeric target.
    check(target == entry + 5,
          "ljmp field targets _asm_back at entry+5 (J16 region-relative)")
    # J11 intra acall (acall _asm_local): fixed layout offset entry+5
    # (lcall 3 + sjmp 2).  Opcode low 5 bits must be 0x11.
    a = entry + 5
    check(image.byte(a) & 0x1F == 0x11, "acall opcode at entry+5")
    addr11 = ((image.byte(a) >> 5) << 8) | image.byte(a + 1)
    landing = ((a + 2) & ~0x7FF) | addr11
    check(landing == syms["_asm_local"]["value"],
          "acall lands on _asm_local (J11, area reference + addend)")
    # J11 cross-object ajmp to _asm_resume at entry+0x3E (after the 4-byte
    # ecall to _asm_helper_x at entry+0x3A).
    a = entry + 0x3E
    check(image.byte(a) & 0x1F == 0x01, "ajmp opcode at entry+0x3E")
    addr11 = ((image.byte(a) >> 5) << 8) | image.byte(a + 1)
    landing = ((a + 2) & ~0x7FF) | addr11
    check(landing == syms["_asm_resume"]["value"],
          "ajmp lands on _asm_resume (J11 cross-object)")
    check(((a + 2) & ~0x7FF) == (syms["_asm_resume"]["value"] & ~0x7FF),
          "ajmp page check holds")

    # --- 4. call/return frame pairing (RET vs ERET) -----------------------
    # The last opcode byte of each function body must match the frame of
    # every caller: near lcall/acall -> RET, extended ecall -> ERET.
    near_funcs = {"_asm_local", "_asm_helper_b", "_asm_resume"}
    ext_funcs = {"_asm_helper_x", "_c_add"}
    for name in near_funcs:
        last = image.byte(syms[name]["value"] + syms[name]["size"] - 1)
        check(last == RET_OPCODE,
              "%s returns with RET (0x%02X, near frame)" % (name, last))
    for name in ext_funcs:
        last = image.byte(syms[name]["value"] + syms[name]["size"] - 1)
        check(last == ERET_OPCODE,
              "%s returns with ERET (0x%02X, extended frame)" % (name, last))
    # Every 24-bit ecall site in liba must target an ERET function...
    ecalls = find_code(image, entry, entry + span,
                       lambda a: image.byte(a) == 0x9A)
    check(len(ecalls) == 2, "exactly two ecall sites in _asm_entry")
    for a in ecalls:
        val = image.u24(a + 1)
        hit = [n for n in ext_funcs if syms[n]["value"] == val]
        check(len(hit) == 1,
              "ecall at entry+0x%X targets extended-frame %s"
              % (a - entry, hit[0] if hit else "0x%X" % val))
    # ...and every near call site must target a RET function.
    lcalls = find_code(image, entry, entry + span,
                       lambda a: image.byte(a) == 0x12)
    check(len(lcalls) == 1, "exactly one lcall site in _asm_entry")
    for a in lcalls:
        val = 0xFF0000 | image.u16(a + 1)
        hit = [n for n in near_funcs if syms[n]["value"] == val]
        check(len(hit) == 1,
              "lcall at entry+0x%X targets near-frame %s"
              % (a - entry, hit[0] if hit else "0x%X" % val))
    # The J11 acall at entry+5 is near-frame too.
    a = entry + 5
    addr11 = ((image.byte(a) >> 5) << 8) | image.byte(a + 1)
    landing = ((a + 2) & ~0x7FF) | addr11
    near_vals = [syms[n]["value"] for n in near_funcs]
    check(landing in near_vals,
          "acall at entry+5 targets a near-frame function")
    # No frame mixing anywhere in the image: no lcall byte pattern may hit
    # an ERET function and no ecall may hit a RET function.
    ext_vals = [syms[n]["value"] for n in ext_funcs]
    for a in find_code(image, entry, entry + span,
                       lambda a: image.byte(a) == 0x12):
        check(not (0xFF0000 | image.u16(a + 1)) in ext_vals,
              "no lcall targets an extended-frame function")
    for a in ecalls:
        check(image.u24(a + 1) not in near_vals,
              "no ecall targets a near-frame function")

    # --- 5. asm -> C argument preparation (int c_add(int,int) ABI) --------
    # The two channels differ: arg0 = 0x44332211 goes through the register
    # channel DPL:DPH:B:A, least significant byte first (measured on
    # hardware, MCS251CallingConv.td); arg1 = 0x33445566 goes through the
    # 4-byte static slot _c_add_PARM_2, which is BIG-ENDIAN in memory
    # ("Memory objects use the same measured big-endian layout as SDCC",
    # MCS251ISelLowering.cpp:2529-2537): 33 44 55 66 at ascending slot
    # addresses.  Section 8 cross-checks this ordering against the
    # compiler-produced caller so both sides cannot be wrong together.
    arg_seq = [(0x75, 0x82, 0x11),  # mov dpl,#0x11
               (0x75, 0x83, 0x22),  # mov dph,#0x22
               (0x75, 0xF0, 0x33),  # mov b,#0x33
               (0x75, 0xE0, 0x44)]  # mov acc,#0x44
    for i, (op, diraddr, imm) in enumerate(arg_seq):
        a = entry + 0x09 + i * 3
        check(image.byte(a) == op and image.byte(a + 1) == diraddr and
              image.byte(a + 2) == imm,
              "arg0 byte %d stored via mov %s,#0x%02X (DPL:DPH:B:A, "
              "LSB first)" % (i, {0x82: "dpl", 0x83: "dph",
                                  0xF0: "b", 0xE0: "acc"}[diraddr], imm))
    parm_seq = [(0x33, "MSB"), (0x44, "byte 2"), (0x55, "byte 1"),
                (0x66, "LSB")]  # big-endian slot: +0 is the MSB
    for i, (imm, label) in enumerate(parm_seq):
        a = entry + 0x15 + i * 3
        check(image.byte(a) == 0x75 and
              image.byte(a + 1) == ((parm2 + i) & 0xFF) and
              image.byte(a + 2) == imm,
              "arg1 %s stored to _c_add_PARM_2+0x%X (imm 0x%02X, "
              "big-endian slot)" % (label, parm2 + i, imm))
    # The immediate set-up must sit between the movc prelude and the ecall
    # to _c_add, i.e. arguments are prepared immediately before the call.
    check(entry + 0x15 + 3 * 3 <= entry + 0x21 <= entry + 0x22,
          "argument stores precede the ecall to _c_add")

    # --- 6. data references ----------------------------------------------
    table = syms["_asm_table"]["value"]
    dptrs = find_code(image, entry, entry + span,
                      lambda a: image.byte(a) == 0x90)
    fields = [image.u16(a + 1) for a in dptrs]
    check((table & 0xFFFF) in fields,
          "a mov dptr,#imm16 loads _asm_table (R_MCS251_16)")
    check(syms["_c_flag"]["value"] & 0xFFFF in fields,
          "a mov dptr,#imm16 loads _c_flag (C global, 16-bit)")
    check(syms["_asm_xbuf"]["value"] & 0xFFFF in fields,
          "a mov dptr,#imm16 loads the XSEG address")
    check([image.byte(table + i) for i in range(4)] ==
          [0x10, 0x20, 0x30, 0x40],
          "_asm_table image bytes are the .db constants")
    # direct 8-bit references: mov a,dir (E5) and mov dir,#imm (75).
    dirs = find_code(image, entry, entry + span,
                     lambda a: image.byte(a) == 0xE5)
    check(any(image.byte(a + 1) == syms["_c_flag"]["value"] for a in dirs),
          "mov a,dir reads _c_flag directly (R_PAG0 -> LO8)")
    stores = find_code(image, entry, entry + span,
                       lambda a: image.byte(a) == 0x75)
    check(any(image.byte(a + 1) == syms["_bvar"]["value"] and
              image.byte(a + 2) == 0x55 for a in stores),
          "mov dir,#imm stores 0x55 to _bvar (cross-object DSEG)")
    # byte selections of the table address (LO8/MID8/HI8).
    imms = find_code(image, entry, entry + span,
                     lambda a: image.byte(a) == 0x74)
    vals = [image.byte(a + 1) for a in imms]
    for label, want in (("LO8", table & 0xFF),
                        ("MID8", (table >> 8) & 0xFF),
                        ("HI8", (table >> 16) & 0xFF)):
        check(want in vals,
              "an immediate byte equals 0x%02X (%s selection of _asm_table)"
              % (want, label))

    # --- 7. map FUNC rows agree with the ELF symbols ----------------------
    map_rows = {}
    with open("%s/demo.map" % outdir) as f:
        for line in f:
            if line.startswith("FUNC "):
                parts = line.split()
                map_rows[parts[3]] = (int(parts[1], 16), int(parts[2], 16))
    for name, (addr, size) in expect_funcs.items():
        check(name in map_rows, "map has a FUNC row for %s" % name)
        maddr, msize = map_rows[name]
        check(maddr == syms[name]["value"],
              "map FUNC address for %s matches the ELF symbol" % name)
        if size is not None:
            check(msize == syms[name]["size"] == size,
                  "map FUNC size for %s matches (size propagation)" % name)

    # --- 8. independent ABI oracle: compiler-produced C caller -----------
    # cabi.o (demo/cabi.c, compiled by run.sh with the current clang/llc)
    # performs exactly the call liba.asm performs.  Its PARM_2 slot stores
    # are decoded from the OBJECT's own relocations -- no hand-written
    # expectation involved -- and must equal both the documented big-endian
    # layout of 0x33445566 and liba.asm's stores decoded in section 5.
    c_stores = c_caller_slot_stores("%s/cabi.o" % outdir)
    check(c_stores == {0: 0x33, 1: 0x44, 2: 0x55, 3: 0x66},
          "compiler C caller stores 0x33445566 big-endian into "
          "_c_add_PARM_2 (%s)" % c_stores)
    asm_stores = {i: imm for i, (imm, _label) in enumerate(parm_seq)}
    check(c_stores == asm_stores,
          "liba.asm slot stores match the compiler ABI oracle byte for "
          "byte")

    print("check: all assertions passed")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
