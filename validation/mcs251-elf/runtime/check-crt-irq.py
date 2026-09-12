#!/usr/bin/env python3
# check-crt-irq.py - standalone acceptance checker for the T08 IRQ CRT object
# (validation/mcs251-elf/runtime/crt-irq.yaml built by yaml2obj), extended for
# the X4 XDATA_INIT walker.
#
# Independence rules (T08 card steps 13/14, unchanged):
#   - stdlib only; imports no LLVM/lld/MCS251 product code;
#   - policy is decided ONLY at frozen-template instruction boundaries: BOOT is
#     decoded against the frozen startup template (prologue + BSEG_BYTES clear
#     + ECALLs + halt + the reused crt-selfstart XINIT walker + the X4
#     XDATA_INIT walker + default entry; XINIT boundaries taken from
#     selfstart-smoke/build/crt-selfstart.lst, XDATA walker boundaries from
#     crt-xdata-init-walker.asm assembled with the frozen sdas251), and the
#     no-IP-init / no-SETB-EA / default-body checks run over the decoded
#     instruction list. The checker never searches the whole file for raw byte
#     patterns such as "B8".
#   - the new CRT's 16-byte .mcs251.BSEG_BYTES reservation (T08 step 6) is
#     verified separately: writable NOBITS shape and consistency between the
#     [0x20,0x30) window, the 16 BOOT clears and the XINIT override order.
#   - X4: the XDATA walker's mov-direct SFR writes (7A <reg> <direct> forms)
#     must target exactly {DPL 0x82, DPH 0x83, DPXL 0x84}; DPXL is loaded per
#     record and deliberately not restored (DESIGN-SUPPLEMENT section 3:
#     CRT startup has no DPXL obligation).
#
# Usage: check-crt-irq.py FILE.o
# Exit 0 with one PASS line per check; exit 1 with FAIL:<detail> otherwise.

import struct
import sys

# ---------------------------------------------------------------------------
# Frozen interface constants (SPEC.md v1, ISR-TASK-BREAKDOWN A3/A5, T08 card).
# ---------------------------------------------------------------------------

ELFCLASS32 = 1
ELFDATA2MSB = 2
ET_REL = 1
EM_MCS251 = 0x9999
EF_MCS251_ABI_V1 = 0x00000001

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOTE = 7
SHT_NOBITS = 8

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

SHN_UNDEF = 0
STB_GLOBAL = 1
STT_FUNC = 2

R_MCS251_16 = 1
R_MCS251_24 = 2
R_MCS251_LO8 = 3
R_MCS251_MID8 = 4
R_MCS251_HI8 = 5
R_MCS251_J16 = 7
R_MCS251_ISR_REF = 9

RELOC_NAMES = {
    0: "R_MCS251_NONE",
    1: "R_MCS251_16",
    2: "R_MCS251_24",
    3: "R_MCS251_LO8",
    4: "R_MCS251_MID8",
    5: "R_MCS251_HI8",
    6: "R_MCS251_PC8",
    7: "R_MCS251_J16",
    8: "R_MCS251_J11",
    9: "R_MCS251_ISR_REF",
}

RELOC_WIDTH = {1: 2, 2: 3, 3: 1, 4: 1, 5: 1, 7: 2}  # type9 is zero-width

HOME_SIZE = 3
HOME_BYTES = bytes.fromhex("020000")
BOOT_SIZE = 0x106
DEFAULT_OFF = 0x102
DEFAULT_SIZE = 4
DEFAULT_BYTES = bytes.fromhex("C2AF80FE")

# The new CRT's own bit-byte reservation (T08 step 6, frozen): a 16-byte
# writable NOBITS .mcs251.BSEG_BYTES section. lld's BSEG_BYTES region rule
# (LinkerCore: exactly SHT_NOBITS with ALLOC|WRITE, allocated inside the fixed
# [0x20,0x2f] window; SPEC 4.1/5.2) places it at [0x20,0x30), which must match
# the 16 BOOT clear instructions below.
BSEG_SECTION = ".mcs251.BSEG_BYTES"
BSEG_BASE = 0x20
BSEG_SIZE = 16
BSEG_ALIGN = 1

WALKER_OFF = 0x4A
WALKER_SIZE = 86

# X4: the XDATA_INIT walker occupies [XWALKER_OFF, XWALKER_OFF+XWALKER_SIZE)
# = [0xA0, 0x102), between the XINIT walker's ERET and the default entry.
# Boundaries are the sdas251 gold listing of crt-xdata-init-walker.asm.
XWALKER_OFF = 0xA0
XWALKER_SIZE = 98

# The two 24-byte asset records (A3.2/A3.3): IRQ_DEFAULT then IRQ_RESET.
RECORD_SIZE = 24
RECORD_DEFAULT = {
    "version": 1, "record_size": 24,
    "record_kind": 3, "entry_kind": 2,          # IRQ_STOP
    "hardware_profile": 1, "save_profile": 0,
    "vector_slot": 0xFFFF, "required_caps": 0x0001,
    "asset_profile": 1,
}
RECORD_RESET = {
    "version": 1, "record_size": 24,
    "record_kind": 4, "entry_kind": 3,          # RESET
    "hardware_profile": 0, "save_profile": 0,
    "vector_slot": 0xFFFF, "required_caps": 0x0001,
    "asset_profile": 1,
}

NOTE_NAME = b"MCS251\x00\x00"
NOTE_DESC_WORDS = (1, 1, 0, 2, 0x0000F3FF, 7, 0, 0)

EXPECTED_SECTIONS = (
    ".mcs251.HOME",
    ".mcs251.BOOT",
    ".mcs251.BSEG_BYTES",
    ".mcs251.isr",
    ".rela.mcs251.HOME",
    ".rela.mcs251.BOOT",
    ".rela.mcs251.isr",
    ".note.mcs251.abi",
    ".symtab",
    ".strtab",
    ".shstrtab",
)

# Direct-memory destinations a mov direct,#imm may legally touch in this CRT:
# the BSEG_BYTES reservation 0x20-0x2F (T08 step 6) plus the two frozen
# control SFR writes PSW(0xD0)/DPS(0xE3) (T08 step 3). Anything else - in
# particular IP=0xB8 / IPH=0xB7 - fails.
ALLOWED_DIRECT_DESTS = set(range(0x20, 0x30)) | {0xD0, 0xE3}

# ---------------------------------------------------------------------------
# Frozen BOOT template. One entry per instruction: (offset, length, bytes,
# label). Boundaries come from the ground-truth crt-selfstart.lst (prologue)
# and from the reused walker block (lst lines 88-125). The template must cover
# [0, BOOT_SIZE) exactly, with no gaps and no overlaps; every byte is compared.
# ---------------------------------------------------------------------------

FROZEN_BOOT_TEMPLATE = [
    (0x00, 2, "C2AF", "clr EA"),
    (0x02, 3, "75D000", "mov PSW,#0"),
    (0x05, 3, "75E300", "mov DPS,#0"),
    (0x08, 4, "7EF80000", "mov spx,#__mcs251_stack_base"),
]
# 0x0C..0x3B: mov 0x20,#0 .. mov 0x2F,#0 (BSEG_BYTES clear).
for _i in range(16):
    FROZEN_BOOT_TEMPLATE.append(
        (0x0C + 3 * _i, 3, "75%02X00" % (0x20 + _i), "mov 0x%02X,#0" % (0x20 + _i)))
FROZEN_BOOT_TEMPLATE += [
    (0x3C, 4, "9A000000", "ecall __mcs251_globals_init"),
    # X4: the XDATA_INIT walker is ECALLed strictly between globals_init and
    # _main (record consumption order: internal RAM first, XDATA second).
    (0x40, 4, "9A000000", "ecall __mcs251_xdata_init"),
    (0x44, 4, "9A000000", "ecall _main"),
    (0x48, 2, "80FE", "sjmp __mcs251_halt"),
    # Reused XINIT walker (86 bytes at 0x4A), lst boundaries, walker-relative
    # offsets translated to BOOT offsets (walker_base + rel).
    (0x4A, 4, "7E080000", "mov wr8,#<s_XINIT mid/lo>"),
    (0x4E, 4, "7A0C0000", "mov r12,#<s_XINIT hi>"),
    (0x52, 4, "7E240000", "mov wr4,#l_XINIT"),
    (0x56, 4, "BE240000", "cmp wr4,#0x0000"),
    (0x5A, 2, "6843", "je __mcs251_xinit_done"),
    (0x5C, 3, "0B0A40", "mov wr8,@dr0"),
    (0x5F, 2, "0B0C", "inc dr0"),
    (0x61, 2, "0B0C", "inc dr0"),
    (0x63, 2, "7DA4", "mov wr20,wr8"),
    (0x65, 3, "0B0A60", "mov wr12,@dr0"),
    (0x68, 2, "0B0C", "inc dr0"),
    (0x6A, 2, "0B0C", "inc dr0"),
    (0x6C, 3, "0B0A80", "mov wr16,@dr0"),
    (0x6F, 2, "0B0C", "inc dr0"),
    (0x71, 2, "0B0C", "inc dr0"),
    (0x73, 4, "9E240006", "sub wr4,#0x0006"),
    (0x77, 3, "7EE000", "mov r14,#0x00"),
    (0x7A, 4, "BE640000", "cmp wr12,#0x0000"),
    (0x7E, 2, "6809", "je __mcs251_xinit_copy"),
    (0x80, 3, "7A49E0", "mov @wr8,r14"),
    (0x83, 2, "0B44", "inc wr8"),
    (0x85, 2, "1B64", "dec wr12"),
    (0x87, 2, "80F1", "sjmp __mcs251_xinit_clear"),
    (0x89, 4, "BE840000", "cmp wr16,#0x0000"),
    (0x8D, 2, "68C7", "je __mcs251_xinit_record"),
    (0x8F, 3, "7E0BE0", "mov r14,@dr0"),
    (0x92, 2, "0B0C", "inc dr0"),
    (0x94, 3, "7AA9E0", "mov @wr20,r14"),
    (0x97, 2, "0BA4", "inc wr20"),
    (0x99, 2, "1B84", "dec wr16"),
    (0x9B, 2, "1B24", "dec wr4"),
    (0x9D, 2, "80EA", "sjmp __mcs251_xinit_copy"),
    (0x9F, 1, "AA", "eret"),
    # X4 XDATA_INIT walker (98 bytes at 0xA0), boundaries from the sdas251
    # gold listing of crt-xdata-init-walker.asm (walker-relative offsets
    # translated to BOOT offsets, xwalker_base + rel).
    (0xA0, 4, "7E080000", "mov dr0_lo16,#<s_XDATA_INIT window mid/lo>"),
    (0xA4, 4, "7A0C0000", "mov dr0_hi16,#0x00<s_XDATA_INIT bank>"),
    (0xA8, 4, "7E240000", "mov wr4,#l_XDATA_INIT"),
    (0xAC, 4, "BE240000", "cmp wr4,#0x0000"),
    (0xB0, 2, "684F", "je __mcs251_xdata_done"),
    (0xB2, 3, "7E0BE0", "mov r14,@dr0 (bank)"),
    (0xB5, 2, "0B0C", "inc dr0"),
    (0xB7, 3, "0B0A40", "mov wr8,@dr0 (window)"),
    (0xBA, 2, "0B0C", "inc dr0"),
    (0xBC, 2, "0B0C", "inc dr0"),
    (0xBE, 3, "0B0A60", "mov wr12,@dr0 (object_size)"),
    (0xC1, 2, "0B0C", "inc dr0"),
    (0xC3, 2, "0B0C", "inc dr0"),
    (0xC5, 3, "0B0A80", "mov wr16,@dr0 (payload_size)"),
    (0xC8, 2, "0B0C", "inc dr0"),
    (0xCA, 2, "0B0C", "inc dr0"),
    (0xCC, 4, "9E240007", "sub wr4,#0x0007"),
    (0xD0, 3, "7AE184", "mov dpxl,r14 (DPXL <- bank)"),
    (0xD3, 3, "7A8183", "mov dph,r8 (DPH <- window hi)"),
    (0xD6, 3, "7A9182", "mov dpl,r9 (DPL <- window lo)"),
    (0xD9, 4, "BE840000", "cmp wr16,#0x0000"),
    (0xDD, 2, "6815", "je __mcs251_xdata_zero"),
    (0xDF, 3, "7E0BE0", "mov r14,@dr0 (payload byte)"),
    (0xE2, 2, "0B0C", "inc dr0"),
    (0xE4, 2, "7CBE", "mov a,r14"),
    (0xE6, 1, "F0", "movx @dptr,a"),
    (0xE7, 1, "A3", "inc dptr"),
    (0xE8, 2, "1B24", "dec wr4"),
    (0xEA, 2, "1B84", "dec wr16"),
    (0xEC, 4, "BE840000", "cmp wr16,#0x0000"),
    (0xF0, 2, "68BA", "je __mcs251_xdata_record"),
    (0xF2, 2, "80EB", "sjmp __mcs251_xdata_copy"),
    (0xF4, 4, "BE640000", "cmp wr12,#0x0000"),
    (0xF8, 2, "68B2", "je __mcs251_xdata_record"),
    (0xFA, 1, "E4", "clr a"),
    (0xFB, 1, "F0", "movx @dptr,a"),
    (0xFC, 1, "A3", "inc dptr"),
    (0xFD, 2, "1B64", "dec wr12"),
    (0xFF, 2, "80F3", "sjmp __mcs251_xdata_zero"),
    (0x101, 1, "AA", "eret"),
    # IRQ default fail-stop entry (A5): clr EA; sjmp self. No call/push/return.
    (0x102, 2, "C2AF", "clr EA"),
    (0x104, 2, "80FE", "sjmp self (default halt)"),
]

# Frozen BOOT relocations: offset -> (type, symbol). Field start = instruction
# start + opcode (7E forms: +2 after the subop byte).
FROZEN_BOOT_RELOCS = {
    0x0A: (R_MCS251_16, "__mcs251_stack_base"),
    0x3D: (R_MCS251_24, "__mcs251_globals_init"),
    0x41: (R_MCS251_24, "__mcs251_xdata_init"),
    0x45: (R_MCS251_24, "_main"),
    0x4C: (R_MCS251_MID8, "s_XINIT"),
    0x4D: (R_MCS251_LO8, "s_XINIT"),
    0x51: (R_MCS251_HI8, "s_XINIT"),
    0x54: (R_MCS251_16, "l_XINIT"),
    0xA2: (R_MCS251_MID8, "s_XDATA_INIT"),
    0xA3: (R_MCS251_LO8, "s_XDATA_INIT"),
    0xA7: (R_MCS251_HI8, "s_XDATA_INIT"),
    0xAA: (R_MCS251_16, "l_XDATA_INIT"),
}

FROZEN_HOME_RELOCS = {
    0x01: (R_MCS251_J16, "__mcs251_selfstart_boot"),
}


class Fail(Exception):
    pass


CHECKS = [0]


def ok(msg):
    CHECKS[0] += 1
    print("PASS[%02d] %s" % (CHECKS[0], msg))


def require(cond, msg):
    if not cond:
        raise Fail(msg)


# ---------------------------------------------------------------------------
# Minimal ELF32 big-endian parser.
# ---------------------------------------------------------------------------

def parse_elf(data):
    require(len(data) >= 52, "file shorter than ELF32 header (%d bytes)" % len(data))
    require(data[0:4] == b"\x7fELF", "missing ELF magic")
    ident = data[0:16]
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = \
        struct.unpack(">HHIIIIIHHHHHH", data[16:52])
    return {
        "ident": ident, "type": e_type, "machine": e_machine, "version": e_version,
        "entry": e_entry, "phoff": e_phoff, "shoff": e_shoff, "flags": e_flags,
        "ehsize": e_ehsize, "phentsize": e_phentsize, "phnum": e_phnum,
        "shentsize": e_shentsize, "shnum": e_shnum, "shstrndx": e_shstrndx,
    }


def parse_sections(data, eh):
    require(eh["shentsize"] == 40, "unexpected shentsize %d" % eh["shentsize"])
    require(eh["shoff"] + eh["shentsize"] * eh["shnum"] <= len(data),
            "section header table out of bounds")
    raw = []
    for i in range(eh["shnum"]):
        off = eh["shoff"] + i * 40
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link,
         sh_info, sh_addralign, sh_entsize) = struct.unpack(">10I", data[off:off + 40])
        raw.append({
            "name_off": sh_name, "type": sh_type, "flags": sh_flags,
            "addr": sh_addr, "offset": sh_offset, "size": sh_size,
            "link": sh_link, "info": sh_info, "align": sh_addralign,
            "entsize": sh_entsize, "index": i,
        })
    require(eh["shstrndx"] < len(raw), "shstrndx out of range")
    shstr = raw[eh["shstrndx"]]
    strtab = data[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for s in raw:
        end = strtab.find(b"\x00", s["name_off"])
        require(end != -1, "unterminated section name")
        s["name"] = strtab[s["name_off"]:end].decode("ascii")
    return raw


def parse_symbols(data, eh, sections):
    symtabs = [s for s in sections if s["type"] == SHT_SYMTAB]
    require(len(symtabs) == 1, "expected exactly one SHT_SYMTAB")
    sym = symtabs[0]
    strtab = sections[sym["link"]]
    sdata = data[sym["offset"]:sym["offset"] + sym["size"]]
    names = data[strtab["offset"]:strtab["offset"] + strtab["size"]]
    require(sym["entsize"] == 16, "unexpected symtab entsize %d" % sym["entsize"])
    out = []
    for i in range(sym["size"] // 16):
        st_name, st_value, st_size, st_info, st_other, st_shndx = \
            struct.unpack(">IIIBBH", sdata[i * 16:(i + 1) * 16])
        if i == 0:
            require((st_name, st_value, st_size, st_info, st_other, st_shndx)
                    == (0, 0, 0, 0, 0, 0),
                    "symbol table entry 0 is not the ELF null symbol")
            continue
        end = names.find(b"\x00", st_name)
        require(end != -1, "unterminated symbol name %d" % i)
        out.append({
            "index": i, "name_off": st_name, "value": st_value, "size": st_size,
            "bind": st_info >> 4, "type": st_info & 0xF, "shndx": st_shndx,
            "name": names[st_name:end].decode("ascii"),
        })
    return out


def parse_rela(data, eh, sections, sec):
    require(sec["entsize"] == 12, "%s: entsize %d, expected 12"
            % (sec["name"], sec["entsize"]))
    rdata = data[sec["offset"]:sec["offset"] + sec["size"]]
    require(len(rdata) % 12 == 0, "%s: size not a multiple of 12" % sec["name"])
    out = []
    for i in range(len(rdata) // 12):
        r_offset, r_info, r_addend = struct.unpack(">IIi", rdata[i * 12:(i + 1) * 12])
        out.append({
            "offset": r_offset, "type": r_info & 0xFF, "symbol": r_info >> 8,
            "addend": r_addend,
        })
    return out


def by_name(sections, name):
    hits = [s for s in sections if s["name"] == name]
    require(len(hits) == 1, "expected exactly one section %r, found %d"
            % (name, len(hits)))
    return hits[0]


def content(data, sec):
    require(sec["offset"] + sec["size"] <= len(data),
            "%s: content out of bounds" % sec["name"])
    return data[sec["offset"]:sec["offset"] + sec["size"]]


# ---------------------------------------------------------------------------
# Checks.
# ---------------------------------------------------------------------------

def check_identity(data, eh):
    require(eh["ident"][4] == ELFCLASS32, "not ELFCLASS32")
    require(eh["ident"][5] == ELFDATA2MSB, "not ELFDATA2MSB")
    require(eh["type"] == ET_REL, "e_type %d is not ET_REL" % eh["type"])
    require(eh["machine"] == EM_MCS251,
            "e_machine 0x%04X is not EM_MCS251" % eh["machine"])
    require(eh["flags"] == EF_MCS251_ABI_V1,
            "e_flags 0x%08X is not EF_MCS251_ABI_V1" % eh["flags"])
    require(eh["phnum"] == 0, "relocatable object must have no program headers")
    require(eh["entry"] == 0, "ET_REL e_entry must be 0")
    ok("original v1 identity: ELFCLASS32/MSB/ET_REL/EM_MCS251/e_flags=1")


def check_sections(sections):
    require(len(sections) >= 1 and sections[0]["name"] == ""
            and sections[0]["type"] == 0 and sections[0]["offset"] == 0,
            "section 0 is not the ELF null section")
    names = [s["name"] for s in sections[1:]]
    for want in EXPECTED_SECTIONS:
        require(names.count(want) == 1,
                "expected exactly one section %r, found %d" % (want, names.count(want)))
    for name in names:
        require(name in EXPECTED_SECTIONS, "unexpected section %r" % name)
        require("VECS" not in name, "unexpected VECS section %r" % name)
    ok("section set is exactly the frozen whitelist; no VECS section")


def check_note(data, sections):
    note = by_name(sections, ".note.mcs251.abi")
    require(note["type"] == SHT_NOTE, ".note.mcs251.abi is not SHT_NOTE")
    blob = content(data, note)
    require(len(blob) == 52, "note size %d, expected 52" % len(blob))
    namesz, descsz, ntype = struct.unpack(">III", blob[0:12])
    require(namesz == 7, "namesz %d, expected 7" % namesz)
    require(descsz == 32, "descsz %d, expected 32" % descsz)
    require(ntype == 1, "note type %d, expected 1" % ntype)
    require(blob[12:20] == NOTE_NAME, "note owner is not MCS251 padded to 8")
    words = struct.unpack(">8I", blob[20:52])
    require(words == NOTE_DESC_WORDS,
            "note descriptor %s mismatch" % (list(words),))
    ok("exactly one .note.mcs251.abi v1 (52B, MCS251, 8 BE descriptor words)")


def check_home(data, sections):
    home = by_name(sections, ".mcs251.HOME")
    require(home["type"] == SHT_PROGBITS, "HOME is not SHT_PROGBITS")
    require(home["flags"] == (SHF_ALLOC | SHF_EXECINSTR),
            "HOME flags 0x%X, expected ALLOC|EXECINSTR" % home["flags"])
    require(home["size"] == HOME_SIZE, "HOME size %d, expected 3" % home["size"])
    require(content(data, home) == HOME_BYTES,
            "HOME content is not 02 00 00 (ljmp placeholder)")
    ok("HOME: 3-byte ljmp reset trampoline 02 00 00")


def check_bseg_bytes(sections):
    """T08 step 6 (frozen): the new CRT owns a 16-byte BSEG_BYTES reservation
    for the bit-addressable byte window 0x20-0x2F."""
    sec = by_name(sections, BSEG_SECTION)
    require(sec["type"] == SHT_NOBITS,
            "%s is type %d, expected SHT_NOBITS" % (BSEG_SECTION, sec["type"]))
    require(sec["flags"] == (SHF_ALLOC | SHF_WRITE),
            "%s flags 0x%X, expected ALLOC|WRITE (writable NOBITS reservation)"
            % (BSEG_SECTION, sec["flags"]))
    require(sec["size"] == BSEG_SIZE,
            "%s size %d, expected 16" % (BSEG_SECTION, sec["size"]))
    require(sec["align"] == BSEG_ALIGN,
            "%s alignment %d, expected 1" % (BSEG_SECTION, sec["align"]))
    require(sec["addr"] == 0,
            "ET_REL %s sh_addr must be 0, got 0x%X" % (BSEG_SECTION, sec["addr"]))
    ok("BSEG_BYTES reservation: 16B writable NOBITS, align 1, ET_REL addr 0 "
       "(lld Region BSEG_BYTES allocates the frozen [0x20,0x30) window)")

    # Consistency with the BOOT clear code: the 16 frozen "mov 0xNN,#0" clears
    # must target exactly the bytes the reservation covers, and they must run
    # strictly before the XINIT walker ecall, so that existing XINIT explicit
    # initial values for [0x20,0x30) overwrite the zeros afterwards (T08 step
    # 6: clear first, explicit XINIT values cover afterwards). This CRT itself
    # carries no .mcs251.xinit section, so it contributes no explicit bit-byte
    # initial value of its own - only the reservation plus the clear.
    clears = [(off, bytes.fromhex(hx)[1])
              for off, ln, hx, lb in FROZEN_BOOT_TEMPLATE
              if lb.startswith("mov 0x")]
    require(len(clears) == BSEG_SIZE,
            "expected %d BSEG_BYTES clear instructions, template has %d"
            % (BSEG_SIZE, len(clears)))
    require([off for off, _ in clears] == [0x0C + 3 * i for i in range(BSEG_SIZE)],
            "BSEG_BYTES clears are not the contiguous 0x0C..0x3B block")
    require(sorted(dst for _, dst in clears)
            == list(range(BSEG_BASE, BSEG_BASE + BSEG_SIZE)),
            "BSEG_BYTES clear destinations do not cover exactly [0x%02X,0x%02X)"
            % (BSEG_BASE, BSEG_BASE + BSEG_SIZE))
    walker_call = [(off, lb) for off, ln, raw, lb in FROZEN_BOOT_TEMPLATE
                   if lb == "ecall __mcs251_globals_init"]
    require(walker_call == [(0x0C + 3 * BSEG_SIZE,
                             "ecall __mcs251_globals_init")],
            "the XINIT walker ecall does not immediately follow the "
            "BSEG_BYTES clear block")
    require(not any(s["name"].startswith(".mcs251.xinit") for s in sections),
            "CRT carries its own .mcs251.xinit section (unexpected explicit "
            "bit-byte initial values in the CRT itself)")
    ok("BSEG_BYTES [0x20,0x30) matches the 16 BOOT mov 0x20-0x2F,#0 clears; "
       "clear precedes the XINIT walker ecall, so existing XINIT explicit "
       "values for the window overwrite the zeros (T08 step 6)")


def decode_template(boot):
    """Byte-compare BOOT against the frozen template and return the decoded
    instruction list [(offset, length, opcode, label)]."""
    covered = {}
    for off, length, hexbytes, label in FROZEN_BOOT_TEMPLATE:
        for i in range(length):
            require(off + i not in covered,
                    "template overlap at 0x%02X (%s)" % (off + i, label))
            covered[off + i] = off
        require(off + length <= BOOT_SIZE,
                "template instruction %s past BOOT end" % label)
        want = bytes.fromhex(hexbytes)
        got = boot[off:off + length]
        require(got == want,
                "BOOT 0x%02X..0x%02X (%s): got %s, expected %s"
                % (off, off + length - 1, label, got.hex().upper(), hexbytes))
    require(len(covered) == BOOT_SIZE,
            "template covers %d of %d BOOT bytes" % (len(covered), BOOT_SIZE))
    return [(off, ln, bytes.fromhex(hx), lb)
            for off, ln, hx, lb in FROZEN_BOOT_TEMPLATE]


def check_boot(data, sections):
    boot_sec = by_name(sections, ".mcs251.BOOT")
    require(boot_sec["type"] == SHT_PROGBITS, "BOOT is not SHT_PROGBITS")
    require(boot_sec["flags"] == (SHF_ALLOC | SHF_EXECINSTR),
            "BOOT flags 0x%X, expected ALLOC|EXECINSTR" % boot_sec["flags"])
    require(boot_sec["size"] == BOOT_SIZE, "BOOT size %d, expected 0x%X"
            % (boot_sec["size"], BOOT_SIZE))
    boot = content(data, boot_sec)
    insns = decode_template(boot)
    ok("BOOT: 0x%X bytes byte-exact against frozen template "
       "(prologue + BSEG_BYTES clear + ECALLs + halt + walker + default)"
       % BOOT_SIZE)

    # --- policy over decoded instruction boundaries -----------------------
    # 1. mov direct,#imm writes only legal destinations (no IP/IPH init).
    dir_writes = []
    for off, ln, raw, label in insns:
        if raw[0] == 0x75:  # mov direct,#imm8
            require(ln == 3, "template shape error at 0x%02X" % off)
            dir_writes.append((off, raw[1], label))
    for off, dest, label in dir_writes:
        require(dest in ALLOWED_DIRECT_DESTS,
                "instruction at 0x%02X (%s) writes illegal direct 0x%02X "
                "(IP init?)" % (off, label, dest))
    ok("no IP address initialization: all %d mov direct,#imm writes target "
       "only BSEG_BYTES 0x20-0x2F and PSW/DPS" % len(dir_writes))

    # 2. No SETB anywhere; EA (bit 0xAF) only ever cleared, exactly twice.
    for off, ln, raw, label in insns:
        require(raw[0] != 0xD2,
                "SETB-family instruction at 0x%02X (%s)" % (off, label))
    ea_insns = [(off, raw) for off, ln, raw, _ in insns if 0xAF in raw[1:]]
    require(len(ea_insns) == 2 and [off for off, _ in ea_insns] == [0x00, DEFAULT_OFF],
            "EA bit 0xAF touched outside the two frozen clr EA sites")
    for off, raw in ea_insns:
        require(raw[0] == 0xC2,
                "EA bit 0xAF touched by non-CLR instruction at 0x%02X" % off)
    ok("no SETB EA: EA appears only as clr EA at 0x00 and in the default entry")

    # 3. Default entry: exact bytes, only clr/sjmp, no call/push/return/RETI.
    default = boot[DEFAULT_OFF:DEFAULT_OFF + DEFAULT_SIZE]
    require(default == DEFAULT_BYTES,
            "default entry bytes %s, expected C2AF80FE" % default.hex().upper())
    for off, ln, raw, label in insns:
        if off >= DEFAULT_OFF:
            require(raw[0] in (0xC2, 0x80),
                    "default entry contains forbidden instruction %s (%s)"
                    % (label, raw.hex().upper()))
    ok("default entry: 4 bytes C2 AF 80 FE, no call/push/return/RETI")

    # 4. Walker is the frozen reused block at its frozen new offset.
    walker = [x for x in insns if WALKER_OFF <= x[0] < WALKER_OFF + WALKER_SIZE]
    require(len(walker) == 33 and walker[0][0] == WALKER_OFF
            and walker[-1][0] == WALKER_OFF + WALKER_SIZE - 1,
            "walker template shape mismatch")
    require(all(raw[0] not in (0x75, 0xD2) for _, _, raw, _ in walker),
            "walker unexpectedly contains direct writes or SETB")
    ok("XINIT walker: frozen 86 bytes at 0x%02X, ECALL-reachable, ERET return "
       "only" % WALKER_OFF)

    # 5. X4: the XDATA_INIT walker block at its frozen offset, byte-exact
    # against the sdas251 gold template above (decoded via the same template
    # mechanism), ECALL-reachable, ERET return only.
    xwalker = [x for x in insns
               if XWALKER_OFF <= x[0] < XWALKER_OFF + XWALKER_SIZE]
    require(len(xwalker) == 40 and xwalker[0][0] == XWALKER_OFF
            and xwalker[-1][0] == XWALKER_OFF + XWALKER_SIZE - 1,
            "xdata walker template shape mismatch")
    require(xwalker[-1][3] == "eret", "xdata walker must end in ERET")
    require(all(raw[0] != 0xD2 for _, _, raw, _ in xwalker),
            "xdata walker unexpectedly contains SETB")
    # Policy: the walker's only mov-direct SFR writes are the 7A <reg>
    # <direct> forms loading the XDATA window registers; every direct
    # destination must be DPL(0x82)/DPH(0x83)/DPXL(0x84). The 75-form check
    # above (allowed direct set) does not cover 7A forms, so this is decided
    # here over the decoded instruction boundaries.
    for off, ln, raw, label in xwalker:
        if raw[0] == 0x7A and len(raw) == 3:
            require(raw[2] in (0x82, 0x83, 0x84),
                    "xdata walker instruction at 0x%02X (%s) writes illegal "
                    "direct 0x%02X (only DPL/DPH/DPXL are allowed)"
                    % (off, label, raw[2]))
    ok("XDATA_INIT walker: 98 bytes at 0x%02X, ERET return only, SFR writes "
       "restricted to DPL/DPH/DPXL (DPXL loaded per record, never restored "
       "-- DESIGN-SUPPLEMENT 3)" % XWALKER_OFF)
    return boot_sec


def check_isr_metadata(data, sections):
    sec = by_name(sections, ".mcs251.isr")
    require(sec["type"] == SHT_PROGBITS, ".mcs251.isr is not SHT_PROGBITS")
    require(sec["flags"] == 0, ".mcs251.isr flags 0x%X, expected 0 (non-ALLOC)"
            % sec["flags"])
    require(sec["align"] == 4, ".mcs251.isr alignment %d, expected 4" % sec["align"])
    require(sec["entsize"] == 0, ".mcs251.isr entsize %d, expected 0" % sec["entsize"])
    blob = content(data, sec)
    require(len(blob) == 2 * RECORD_SIZE and len(blob) % RECORD_SIZE == 0,
            ".mcs251.isr size %d is not 2 x 24" % len(blob))

    fields = struct.unpack(">HHBBBBHH4sI4s", blob[0:24])
    (version, rsize, kind, entry, hw, save, slot, caps,
     symref, asset, reserved) = fields
    want = RECORD_DEFAULT
    require((version, rsize) == (1, 24), "DEFAULT record header version/size bad")
    require(kind == want["record_kind"], "record 0 is not IRQ_DEFAULT (kind 3)")
    require(entry == want["entry_kind"], "DEFAULT entry_kind %d, expected 2" % entry)
    require(hw == want["hardware_profile"], "DEFAULT hardware_profile %d" % hw)
    require(save == want["save_profile"], "DEFAULT save_profile %d" % save)
    require(slot == want["vector_slot"], "DEFAULT vector_slot 0x%04X" % slot)
    require(caps == want["required_caps"], "DEFAULT required_caps 0x%04X" % caps)
    require(symref == b"\x00" * 4, "DEFAULT symbol_reference bytes not zero")
    require(asset == want["asset_profile"], "DEFAULT asset_profile %d" % asset)
    require(reserved == b"\x00" * 4, "DEFAULT reserved not zero")

    fields = struct.unpack(">HHBBBBHH4sI4s", blob[24:48])
    (version, rsize, kind, entry, hw, save, slot, caps,
     symref, asset, reserved) = fields
    want = RECORD_RESET
    require((version, rsize) == (1, 24), "RESET record header version/size bad")
    require(kind == want["record_kind"], "record 1 is not IRQ_RESET (kind 4)")
    require(entry == want["entry_kind"], "RESET entry_kind %d, expected 3" % entry)
    require(hw == want["hardware_profile"], "RESET hardware_profile %d" % hw)
    require(save == want["save_profile"], "RESET save_profile %d" % save)
    require(slot == want["vector_slot"], "RESET vector_slot 0x%04X" % slot)
    require(caps == want["required_caps"], "RESET required_caps 0x%04X" % caps)
    require(symref == b"\x00" * 4, "RESET symbol_reference bytes not zero")
    require(asset == want["asset_profile"], "RESET asset_profile %d" % asset)
    require(reserved == b"\x00" * 4, "RESET reserved not zero")
    ok("new metadata: IRQ_DEFAULT(kind3/IRQ_STOP/hw1/asset1) + "
       "IRQ_RESET(kind4/RESET/hw0/asset1), slot FFFF, caps 0001, 24B each")


def check_relocations(data, eh, sections, symbols, boot_sec, home_sec, isr_sec):
    symtab = by_name(sections, ".symtab")
    symbols_by_index = {s["index"]: s for s in symbols}

    def sym_name(idx):
        require(idx in symbols_by_index,
                "relocation symbol index %d out of range" % idx)
        return symbols_by_index[idx]["name"]

    def check_rela(name, target_sec, frozen, only_type=None, banned_type=None):
        sec = by_name(sections, name)
        require(sec["type"] == SHT_RELA, "%s is not SHT_RELA" % name)
        require(sec["link"] == symtab["index"],
                "%s sh_link does not reference .symtab" % name)
        require(sec["info"] == target_sec["index"],
                "%s sh_info does not reference %s" % (name, target_sec["name"]))
        relas = parse_rela(data, eh, sections, sec)
        seen = {}
        for r in relas:
            if only_type is not None:
                require(r["type"] == only_type,
                        "%s: relocation type %s not allowed here"
                        % (name, RELOC_NAMES.get(r["type"], str(r["type"]))))
            if banned_type is not None:
                require(r["type"] != banned_type,
                        "%s: relocation type %s not allowed here"
                        % (name, RELOC_NAMES.get(banned_type, str(banned_type))))
            require(r["addend"] == 0, "%s: addend %d, expected 0"
                    % (name, r["addend"]))
            width = RELOC_WIDTH.get(r["type"], 0)
            require(r["offset"] + width <= target_sec["size"],
                    "%s: field at 0x%X overruns section" % (name, r["offset"]))
            require(r["offset"] not in seen, "%s: duplicate offset 0x%X"
                    % (name, r["offset"]))
            seen[r["offset"]] = r
            if target_sec is boot_sec:
                in_default = DEFAULT_OFF <= r["offset"] < DEFAULT_OFF + DEFAULT_SIZE
                require(not in_default,
                        "%s: relocation at 0x%X inside default entry range"
                        % (name, r["offset"]))
        require(set(seen.keys()) == set(frozen.keys()),
                "%s: relocation offsets %s do not match frozen set %s"
                % (name, sorted(hex(k) for k in seen),
                   sorted(hex(k) for k in frozen)))
        for off, (rtype, rname) in frozen.items():
            r = seen[off]
            require(r["type"] == rtype,
                    "%s: offset 0x%X type %s, expected %s"
                    % (name, off, RELOC_NAMES.get(r["type"], r["type"]),
                       RELOC_NAMES.get(rtype, rtype)))
            require(sym_name(r["symbol"]) == rname,
                    "%s: offset 0x%X targets %r, expected %r"
                    % (name, off, sym_name(r["symbol"]), rname))
        return relas

    check_rela(".rela.mcs251.HOME", home_sec, FROZEN_HOME_RELOCS,
               banned_type=R_MCS251_ISR_REF)
    ok("HOME relocation: exactly one R_MCS251_J16 at 0x1 -> __mcs251_selfstart_boot")

    check_rela(".rela.mcs251.BOOT", boot_sec, FROZEN_BOOT_RELOCS,
               banned_type=R_MCS251_ISR_REF)
    ok("BOOT relocations: exactly the 12 frozen offsets "
       "(spx R16@0xA; ecall R24@0x3D/0x41/0x45; XINIT walker MID8@0x4C "
       "LO8@0x4D HI8@0x51 R16@0x54; XDATA walker MID8@0xA2 LO8@0xA3 HI8@0xA7 "
       "R16@0xAA), none inside the default entry")

    relas = check_rela(".rela.mcs251.isr", isr_sec,
                       {0x0C: (R_MCS251_ISR_REF, "__mcs251_isr_unhandled"),
                        0x24: (R_MCS251_ISR_REF, "__mcs251_reset")},
                       only_type=R_MCS251_ISR_REF)
    require(sorted(r["offset"] for r in relas) == [12, RECORD_SIZE + 12],
            "type9 offsets are not record_offset+12")
    ok(".mcs251.isr relocations: exactly two zero-width R_MCS251_ISR_REF at "
       "record_offset+12 (0xC, 0x24), four-byte symbol fields stay zero")


def check_symbols(data, eh, sections, symbols, boot_sec, home_sec):
    defined = {}
    undef = []
    for s in symbols:
        if s["shndx"] == SHN_UNDEF:
            undef.append(s)
        else:
            defined[s["name"]] = s

    def expect_func(name, sec, value, size):
        require(name in defined, "missing defined symbol %r" % name)
        s = defined[name]
        require(s["bind"] == STB_GLOBAL and s["type"] == STT_FUNC,
                "%r must be GLOBAL STT_FUNC" % name)
        target = by_name(sections, sec)
        require(s["shndx"] == target["index"], "%r not in %s" % (name, sec))
        require(s["value"] == value, "%r value 0x%X, expected 0x%X"
                % (name, s["value"], value))
        require(s["size"] == size, "%r size %d, expected %d" % (name, s["size"], size))
        require(target["flags"] & (SHF_ALLOC | SHF_EXECINSTR) ==
                (SHF_ALLOC | SHF_EXECINSTR),
                "%r target section is not executable PROGBITS" % name)
        require(s["value"] + s["size"] <= target["size"],
                "%r function range overruns its section" % name)

    # A3.4 targets of the type9 associations.
    expect_func("__mcs251_reset", ".mcs251.HOME", 0x0, HOME_SIZE)
    ok("IRQ_RESET target __mcs251_reset: named GLOBAL STT_FUNC, HOME+0, size 3")
    expect_func("__mcs251_isr_unhandled", ".mcs251.BOOT", DEFAULT_OFF, DEFAULT_SIZE)
    ok("IRQ_DEFAULT target __mcs251_isr_unhandled: named GLOBAL STT_FUNC, "
       "BOOT+0x%X, size 4" % DEFAULT_OFF)

    for name, off in (("__mcs251_selfstart_boot", 0x0),
                      ("__mcs251_globals_init", WALKER_OFF),
                      ("__mcs251_xdata_init", XWALKER_OFF)):
        require(name in defined, "missing defined symbol %r" % name)
        s = defined[name]
        require(s["bind"] == STB_GLOBAL and s["shndx"] == boot_sec["index"]
                and s["value"] == off,
                "%r must be a GLOBAL defined at BOOT+0x%X" % (name, off))
    ok("__mcs251_selfstart_boot (BOOT+0), __mcs251_globals_init (XINIT walker, "
       "SPEC 6.2 trigger) and __mcs251_xdata_init (XDATA walker, X4) defined "
       "GLOBAL")

    undef_names = sorted(s["name"] for s in undef)
    require(undef_names == ["__mcs251_stack_base", "_main", "l_XDATA_INIT",
                            "l_XINIT", "s_XDATA_INIT", "s_XINIT"],
            "undefined GLOBAL set %s mismatch (expect _main, "
            "__mcs251_stack_base, s_XINIT, l_XINIT, s_XDATA_INIT, "
            "l_XDATA_INIT)" % undef_names)
    for s in undef:
        require(s["bind"] == STB_GLOBAL, "undefined %r not GLOBAL" % s["name"])
    ok("undefined requests intact: _main, __mcs251_stack_base (v1 SPX gate), "
       "s_XINIT, l_XINIT, s_XDATA_INIT, l_XDATA_INIT (X4)")


def main():
    if len(sys.argv) != 2:
        print("usage: check-crt-irq.py FILE.o", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    eh = parse_elf(data)
    sections = parse_sections(data, eh)
    check_identity(data, eh)
    check_sections(sections)
    check_note(data, sections)
    check_home(data, sections)
    boot_sec = check_boot(data, sections)
    check_bseg_bytes(sections)
    check_isr_metadata(data, sections)
    symbols = parse_symbols(data, eh, sections)
    # Relocation checks and symbol-shape checks must identify the same entry.
    # This frozen CRT has unique named symbols; reject same-name decoys before
    # any name-keyed lookup can hide the exact symbol referenced by a RELA.
    named_symbols = [s["name"] for s in symbols if s["name"]]
    require(len(named_symbols) == len(set(named_symbols)),
            "duplicate named symbol in frozen CRT symbol table")
    check_relocations(data, eh, sections, symbols, boot_sec,
                      by_name(sections, ".mcs251.HOME"),
                      by_name(sections, ".mcs251.isr"))
    check_symbols(data, eh, sections, symbols, boot_sec,
                  by_name(sections, ".mcs251.HOME"))
    print("check-crt-irq: PASS (%d checks) %s" % (CHECKS[0], path))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fail as e:
        print("check-crt-irq: FAIL: %s" % e, file=sys.stderr)
        sys.exit(1)
