#!/usr/bin/env python3
# check-crt-v2.py - standalone acceptance checker for the A4 W7 v2-identity CRT
# objects (validation/mcs251-elf/runtime/crt-selfstart-v2.yaml and
# crt-irq-v2.yaml, built by gen-crt-v2.sh --identity v2 with the frozen
# yaml2obj plus the e_flags patch).
#
# Design A4-V2-OBJECT-IDENTITY-DESIGN.md section 6: the v2 fixtures preserve
# the v1 startup order, vectors, stack initialization, segment boundaries and
# entry BYTE FOR BYTE; only the identity changes.  This checker therefore
# asserts exactly that split:
#
#   identity half (new):
#   - e_flags is the v2 word 0x00000102 (low byte 2 agrees with Tag 4);
#   - exactly one .mcs251.attributes carrier with the frozen section shape
#     (SHT 0x70000003, flags 0, align 1, entsize/link/info 0);
#   - NO .note.mcs251.abi anywhere in the object (design 3.2: a v2 object
#     must not carry a v1 note that would mislead old readers);
#   - the carrier is decoded INDEPENDENTLY against the section 3.1 envelope
#     arithmetic (0x41 / BE VendorSize = 16+P / "MCS251\0" / scope 1 /
#     BE ScopeSize = 5+P / P bytes), shortest-form ULEB128, strictly
#     increasing tags, the 21 required tags each exactly once, and the
#     PM-ruled A4 registered values (2026-09-13 ruling: CallABIMajor=2,
#     CallABIMinor=1 as the single expression of pointer static slots,
#     RegisterParameterVariant=3, ASLayoutVersion=2, the four contract
#     versions=2, ObjectProtocolMinor=0, CodeModelProfile=1, capabilities
#     lo/hi=0, ABIOptions=0; XSmall placement 8);
#
#   template half (unchanged from the frozen v1 fixtures):
#   - selfstart variant: HOME 3-byte ljmp, eight 4+4 VECS fragment pairs,
#     BOOT 0xD5 bytes decoded against the frozen instruction template
#     (prologue + XINIT walker + X4 XDATA_INIT walker + the SBUF tail), the
#     frozen relocation map and the frozen symbol set - identical bytes to
#     crt-selfstart.yaml;
#   - irq variant: HOME 3-byte ljmp, BOOT 0x106 bytes against the frozen
#     T08 template, the 16-byte BSEG_BYTES reservation, the two 24-byte
#     .mcs251.isr asset records with ProtocolVersion STILL 1 (the ISR record
#     protocol and the ELF object identity are two different protocols; the
#     ISR bump belongs to G1, not A4), the frozen relocations and symbols -
#     identical bytes to crt-irq.yaml.
#
# Independence rules (aligned with check-crt-irq.py):
#   - stdlib only; imports no LLVM/lld/MCS251 product code;
#   - policy is decided only at frozen-template instruction boundaries and
#     against frozen byte literals; the checker never searches the file for
#     raw patterns;
#   - the v1 fixtures themselves are never read: the frozen literals are
#     embedded here, so a mutation of a v1 fixture cannot silently change
#     what this checker accepts.
#
# Usage: check-crt-v2.py FILE.o [FILE.o ...]
# The variant is auto-detected from the section set (VECS -> selfstart,
# .mcs251.isr -> irq).  Exit 0 with one PASS line per check per file; exit 1
# with FAIL:<detail> otherwise.

import struct
import sys

# ---------------------------------------------------------------------------
# Frozen interface constants (SPEC.md, ISR-TASK-BREAKDOWN A3/A5, T08, A4).
# ---------------------------------------------------------------------------

ELFCLASS32 = 1
ELFDATA2MSB = 2
ET_REL = 1
EM_MCS251 = 0x9999
EF_MCS251_ABI_V2 = 0x00000102      # A4: low byte 2 = object protocol, bit 8

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOTE = 7
SHT_NOBITS = 8
ATTRS_SECTION_TYPE = 0x70000003

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
    0: "R_MCS251_NONE", 1: "R_MCS251_16", 2: "R_MCS251_24",
    3: "R_MCS251_LO8", 4: "R_MCS251_MID8", 5: "R_MCS251_HI8",
    6: "R_MCS251_PC8", 7: "R_MCS251_J16", 8: "R_MCS251_J11",
    9: "R_MCS251_ISR_REF",
}
RELOC_WIDTH = {1: 2, 2: 3, 3: 1, 4: 1, 5: 1, 7: 2}   # type 9 is zero-width

HOME_SIZE = 3
HOME_BYTES = bytes.fromhex("020000")                 # ljmp placeholder

# ---------------------------------------------------------------------------
# A4 v2 identity carrier: the registered XSmall payload (design sections
# 2/3.1, PM ruling 2026-09-13).  172 bytes total = 17-byte envelope + 155.
# ---------------------------------------------------------------------------

# The frozen 172-byte production payload (17-byte envelope + 155-byte body).
# Cross-verified at W7 time against a live llc -filetype=obj emission AND the
# frozen bytes of lld/test/MCS251/v2-object-identity.test xs.yaml; the tag
# table below re-derives every field so a hex typo cannot pass silently.
CARRIER_HEX = (
    "41000000AB4D43533235310001000000A0"
    "04810400000002"
    "05810400000002"
    "06810400000001"
    "07810400000003"
    "0881040000F3FF"
    "09810400000020"
    "0A810400000020"
    "0B810400000020"
    "0C810400000002"
    "0D810400000008"
    "0E810400000002"
    "0F810400000002"
    "10810400000002"
    "11810400000002"
    "12810400000000"
    "13810400000000"
    "14810400000000"
    "18830C010400000020010400000008"
    "19810400000001"
    "1A810400000020"
    "1B810400000000"
)
assert len(CARRIER_HEX) // 2 == 172, "frozen carrier literal is not 172 bytes"

# tag -> (name, kind, value).  kind "u32" or "mix" (two U32 atoms).
CARRIER_TAGS = [
    (4,  "object_protocol_version",    "u32", 2),
    (5,  "call_abi_major",             "u32", 2),
    (6,  "call_abi_minor",             "u32", 1),   # pointer static slots
    (7,  "register_parameter_variant", "u32", 3),
    (8,  "general_register_set",       "u32", 0x0000F3FF),
    (9,  "int_bits",                   "u32", 32),
    (10, "long_bits",                  "u32", 32),
    (11, "as0_pointer_bits",           "u32", 32),
    (12, "as_layout_version",          "u32", 2),
    (13, "default_placement",          "u32", 8),   # XSmall / InternalExtended
    (14, "init_protocol_version",      "u32", 2),
    (15, "placement_protocol_version", "u32", 2),
    (16, "stack_contract_version",     "u32", 2),
    (17, "function_contract_version",  "u32", 2),
    (18, "required_capabilities_lo",   "u32", 0),
    (19, "required_capabilities_hi",   "u32", 0),
    (20, "abi_options",                "u32", 0),
    # tags 21-23 reserved: omitted in the registered payload
    (24, "memory_model_profile",       "mix", (32, 8)),
    (25, "code_model_profile",         "u32", 1),
    (26, "code_pointer_bits",          "u32", 32),
    (27, "object_protocol_minor",      "u32", 0),
]
REQUIRED_TAG_COUNT = 21

# ---------------------------------------------------------------------------
# selfstart variant (crt-selfstart-v2.yaml): frozen template of the v1
# fixture.  BOOT layout: prologue (WTST, SPX, three ECALLs, SBUF 'S', halt),
# XINIT walker at 0x18 (86 bytes, crt-selfstart.lst boundaries), X4 XDATA
# walker at 0x6E (98 bytes, crt-xdata-init-walker.asm gold), SBUF '!' tail.
# ---------------------------------------------------------------------------

SS_BOOT_SIZE = 0xD5

SS_BOOT_TEMPLATE = [
    (0x00, 3, "75E900", "mov WTST,#0"),
    (0x03, 4, "7EF80000", "mov spx,#__mcs251_stack_base"),
    (0x07, 4, "9A000000", "ecall __mcs251_globals_init"),
    (0x0B, 4, "9A000000", "ecall __mcs251_xdata_init"),
    (0x0F, 4, "9A000000", "ecall _main"),
    (0x13, 3, "759953", "mov SBUF,#'S'"),
    (0x16, 2, "80FE", "sjmp __mcs251_halt"),
    # XINIT walker (86 bytes at 0x18; .lst 0x14-0x69 shifted +4, X4 note).
    (0x18, 4, "7E080000", "mov wr8,#<s_XINIT mid/lo>"),
    (0x1C, 4, "7A0C0000", "mov r12,#<s_XINIT hi>"),
    (0x20, 4, "7E240000", "mov wr4,#l_XINIT"),
    (0x24, 4, "BE240000", "cmp wr4,#0x0000"),
    (0x28, 2, "6843", "je __mcs251_xinit_done"),
    (0x2A, 3, "0B0A40", "mov wr8,@dr0"),
    (0x2D, 2, "0B0C", "inc dr0"),
    (0x2F, 2, "0B0C", "inc dr0"),
    (0x31, 2, "7DA4", "mov wr20,wr8"),
    (0x33, 3, "0B0A60", "mov wr12,@dr0"),
    (0x36, 2, "0B0C", "inc dr0"),
    (0x38, 2, "0B0C", "inc dr0"),
    (0x3A, 3, "0B0A80", "mov wr16,@dr0"),
    (0x3D, 2, "0B0C", "inc dr0"),
    (0x3F, 2, "0B0C", "inc dr0"),
    (0x41, 4, "9E240006", "sub wr4,#0x0006"),
    (0x45, 3, "7EE000", "mov r14,#0x00"),
    (0x48, 4, "BE640000", "cmp wr12,#0x0000"),
    (0x4C, 2, "6809", "je __mcs251_xinit_copy"),
    (0x4E, 3, "7A49E0", "mov @wr8,r14"),
    (0x51, 2, "0B44", "inc wr8"),
    (0x53, 2, "1B64", "dec wr12"),
    (0x55, 2, "80F1", "sjmp __mcs251_xinit_clear"),
    (0x57, 4, "BE840000", "cmp wr16,#0x0000"),
    (0x5B, 2, "68C7", "je __mcs251_xinit_record"),
    (0x5D, 3, "7E0BE0", "mov r14,@dr0"),
    (0x60, 2, "0B0C", "inc dr0"),
    (0x62, 3, "7AA9E0", "mov @wr20,r14"),
    (0x65, 2, "0BA4", "inc wr20"),
    (0x67, 2, "1B84", "dec wr16"),
    (0x69, 2, "1B24", "dec wr4"),
    (0x6B, 2, "80EA", "sjmp __mcs251_xinit_copy"),
    (0x6D, 1, "AA", "eret"),
    # X4 XDATA_INIT walker (98 bytes at 0x6E; sdas251 gold listing).
    (0x6E, 4, "7E080000", "mov dr0_lo16,#<s_XDATA_INIT window mid/lo>"),
    (0x72, 4, "7A0C0000", "mov dr0_hi16,#0x00<s_XDATA_INIT bank>"),
    (0x76, 4, "7E240000", "mov wr4,#l_XDATA_INIT"),
    (0x7A, 4, "BE240000", "cmp wr4,#0x0000"),
    (0x7E, 2, "684F", "je __mcs251_xdata_done"),
    (0x80, 3, "7E0BE0", "mov r14,@dr0 (bank)"),
    (0x83, 2, "0B0C", "inc dr0"),
    (0x85, 3, "0B0A40", "mov wr8,@dr0 (window)"),
    (0x88, 2, "0B0C", "inc dr0"),
    (0x8A, 2, "0B0C", "inc dr0"),
    (0x8C, 3, "0B0A60", "mov wr12,@dr0 (object_size)"),
    (0x8F, 2, "0B0C", "inc dr0"),
    (0x91, 2, "0B0C", "inc dr0"),
    (0x93, 3, "0B0A80", "mov wr16,@dr0 (payload_size)"),
    (0x96, 2, "0B0C", "inc dr0"),
    (0x98, 2, "0B0C", "inc dr0"),
    (0x9A, 4, "9E240007", "sub wr4,#0x0007"),
    (0x9E, 3, "7AE184", "mov dpxl,r14 (DPXL <- bank)"),
    (0xA1, 3, "7A8183", "mov dph,r8 (DPH <- window hi)"),
    (0xA4, 3, "7A9182", "mov dpl,r9 (DPL <- window lo)"),
    (0xA7, 4, "BE840000", "cmp wr16,#0x0000"),
    (0xAB, 2, "6815", "je __mcs251_xdata_zero"),
    (0xAD, 3, "7E0BE0", "mov r14,@dr0 (payload byte)"),
    (0xB0, 2, "0B0C", "inc dr0"),
    (0xB2, 2, "7CBE", "mov a,r14"),
    (0xB4, 1, "F0", "movx @dptr,a"),
    (0xB5, 1, "A3", "inc dptr"),
    (0xB6, 2, "1B24", "dec wr4"),
    (0xB8, 2, "1B84", "dec wr16"),
    (0xBA, 4, "BE840000", "cmp wr16,#0x0000"),
    (0xBE, 2, "68BA", "je __mcs251_xdata_record"),
    (0xC0, 2, "80EB", "sjmp __mcs251_xdata_copy"),
    (0xC2, 4, "BE640000", "cmp wr12,#0x0000"),
    (0xC6, 2, "68B2", "je __mcs251_xdata_record"),
    (0xC8, 1, "E4", "clr a"),
    (0xC9, 1, "F0", "movx @dptr,a"),
    (0xCA, 1, "A3", "inc dptr"),
    (0xCB, 2, "1B64", "dec wr12"),
    (0xCD, 2, "80F3", "sjmp __mcs251_xdata_zero"),
    (0xCF, 1, "AA", "eret"),
    (0xD0, 3, "759921", "mov SBUF,#'!'"),
    (0xD3, 2, "80FE", "sjmp __mcs251_ispin"),
]

SS_HOME_RELOCS = {0x01: (R_MCS251_J16, "__mcs251_selfstart_boot")}
SS_VECS_RELOCS = {0x01: (R_MCS251_24, "__mcs251_isr_unhandled")}  # per VECS.n
SS_BOOT_RELOCS = {
    0x05: (R_MCS251_16, "__mcs251_stack_base"),
    0x08: (R_MCS251_24, "__mcs251_globals_init"),
    0x0C: (R_MCS251_24, "__mcs251_xdata_init"),
    0x10: (R_MCS251_24, "_main"),
    0x1A: (R_MCS251_MID8, "s_XINIT"),
    0x1B: (R_MCS251_LO8, "s_XINIT"),
    0x1F: (R_MCS251_HI8, "s_XINIT"),
    0x22: (R_MCS251_16, "l_XINIT"),
    0x70: (R_MCS251_MID8, "s_XDATA_INIT"),
    0x71: (R_MCS251_LO8, "s_XDATA_INIT"),
    0x75: (R_MCS251_HI8, "s_XDATA_INIT"),
    0x78: (R_MCS251_16, "l_XDATA_INIT"),
}

SS_DEFINED_GLOBALS = {
    "__mcs251_selfstart_boot": (".mcs251.BOOT", 0x0),
    "__mcs251_globals_init": (".mcs251.BOOT", 0x18),
    "__mcs251_xdata_init": (".mcs251.BOOT", 0x6E),
    "__mcs251_isr_unhandled": (".mcs251.BOOT", 0xD0),
}
SS_LOCALS = {
    "__mcs251_reset": (".mcs251.HOME", 0x0),
    "__mcs251_halt": (".mcs251.BOOT", 0x16),
    "__mcs251_xinit_record": (".mcs251.BOOT", 0x24),
    "__mcs251_xinit_clear": (".mcs251.BOOT", 0x48),
    "__mcs251_xinit_copy": (".mcs251.BOOT", 0x57),
    "__mcs251_xinit_done": (".mcs251.BOOT", 0x6D),
    "__mcs251_xdata_record": (".mcs251.BOOT", 0x7A),
    "__mcs251_xdata_copy": (".mcs251.BOOT", 0xAD),
    "__mcs251_xdata_zero": (".mcs251.BOOT", 0xC2),
    "__mcs251_xdata_done": (".mcs251.BOOT", 0xCF),
    "__mcs251_ispin": (".mcs251.BOOT", 0xD3),
}
SS_UNDEF = ["_main", "__mcs251_stack_base", "s_XINIT", "l_XINIT",
            "s_XDATA_INIT", "l_XDATA_INIT"]

SS_SECTIONS = ([".mcs251.HOME"]
               + [".mcs251.VECS.%d.%s" % (i, k)
                  for i in range(8) for k in ("bytes", "hole")]
               + [".mcs251.BOOT", ".rela.mcs251.HOME"]
               + [".rela.mcs251.VECS.%d.bytes" % i for i in range(8)]
               + [".rela.mcs251.BOOT", ".mcs251.attributes",
                  ".symtab", ".strtab", ".shstrtab"])

# ---------------------------------------------------------------------------
# irq variant (crt-irq-v2.yaml): frozen T08 template (see check-crt-irq.py,
# whose tables this literal mirrors; kept independent and embedded).
# ---------------------------------------------------------------------------

IRQ_BOOT_SIZE = 0x106
IRQ_DEFAULT_OFF = 0x102
IRQ_DEFAULT_SIZE = 4
IRQ_BSEG_SIZE = 16
IRQ_RECORD_SIZE = 24

IRQ_BOOT_TEMPLATE = [
    (0x00, 2, "C2AF", "clr EA"),
    (0x02, 3, "75D000", "mov PSW,#0"),
    (0x05, 3, "75E300", "mov DPS,#0"),
    (0x08, 4, "7EF80000", "mov spx,#__mcs251_stack_base"),
]
for _i in range(16):
    IRQ_BOOT_TEMPLATE.append(
        (0x0C + 3 * _i, 3, "75%02X00" % (0x20 + _i),
         "mov 0x%02X,#0" % (0x20 + _i)))
IRQ_BOOT_TEMPLATE += [
    (0x3C, 4, "9A000000", "ecall __mcs251_globals_init"),
    (0x40, 4, "9A000000", "ecall __mcs251_xdata_init"),
    (0x44, 4, "9A000000", "ecall _main"),
    (0x48, 2, "80FE", "sjmp __mcs251_halt"),
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
    (0x102, 2, "C2AF", "clr EA"),
    (0x104, 2, "80FE", "sjmp self (default halt)"),
]

IRQ_HOME_RELOCS = {0x01: (R_MCS251_J16, "__mcs251_selfstart_boot")}
IRQ_BOOT_RELOCS = {
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
IRQ_ISR_RELOCS = {
    0x0C: (R_MCS251_ISR_REF, "__mcs251_isr_unhandled"),
    0x24: (R_MCS251_ISR_REF, "__mcs251_reset"),
}

# .mcs251.isr asset records (A3.2/A3.3): ProtocolVersion stays 1 (the ISR
# metadata protocol is independent of the ELF object identity; G1 owns the
# bump).  Fields: version, record_size, kind, entry, hw, save, slot, caps.
IRQ_ISR_RECORDS = [
    # IRQ_DEFAULT: kind 3, entry_kind 2 (IRQ_STOP), hw 1, asset 1
    {"version": 1, "record_size": 24, "kind": 3, "entry": 2, "hw": 1,
     "save": 0, "slot": 0xFFFF, "caps": 0x0001, "asset": 1},
    # IRQ_RESET: kind 4, entry_kind 3 (RESET), hw 0, asset 1
    {"version": 1, "record_size": 24, "kind": 4, "entry": 3, "hw": 0,
     "save": 0, "slot": 0xFFFF, "caps": 0x0001, "asset": 1},
]

IRQ_SECTIONS = [
    ".mcs251.HOME", ".mcs251.BOOT", ".mcs251.BSEG_BYTES", ".mcs251.isr",
    ".rela.mcs251.HOME", ".rela.mcs251.BOOT", ".rela.mcs251.isr",
    ".mcs251.attributes", ".symtab", ".strtab", ".shstrtab",
]


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
# Minimal ELF32 big-endian parser (identical shape to check-crt-irq.py).
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
# v2 identity checks.
# ---------------------------------------------------------------------------

def check_identity(data, eh):
    require(eh["ident"][4] == ELFCLASS32, "not ELFCLASS32")
    require(eh["ident"][5] == ELFDATA2MSB, "not ELFDATA2MSB")
    require(eh["type"] == ET_REL, "e_type %d is not ET_REL" % eh["type"])
    require(eh["machine"] == EM_MCS251,
            "e_machine 0x%04X is not EM_MCS251" % eh["machine"])
    require(eh["flags"] == EF_MCS251_ABI_V2,
            "e_flags 0x%08X is not the v2 word 0x00000102" % eh["flags"])
    require(eh["phnum"] == 0, "relocatable object must have no program headers")
    require(eh["entry"] == 0, "ET_REL e_entry must be 0")
    ok("v2 identity: ELFCLASS32/MSB/ET_REL/EM_MCS251/e_flags=0x102")


def check_carrier(data, eh, sections):
    """The .mcs251.attributes carrier, decoded independently (design 3.1)."""
    attrs = [s for s in sections if s["name"] == ".mcs251.attributes"]
    require(len(attrs) == 1,
            "expected exactly one .mcs251.attributes, found %d" % len(attrs))
    sec = attrs[0]
    require(sec["type"] == ATTRS_SECTION_TYPE,
            "carrier type 0x%X, expected 0x70000003" % sec["type"])
    require(sec["flags"] == 0, "carrier flags 0x%X, expected 0 (non-ALLOC)"
            % sec["flags"])
    require(sec["align"] == 1, "carrier alignment %d, expected 1" % sec["align"])
    require(sec["entsize"] == 0 and sec["link"] == 0 and sec["info"] == 0,
            "carrier entsize/link/info must all be 0")
    blob = content(data, sec)
    require(blob == bytes.fromhex(CARRIER_HEX),
            "carrier bytes are not the frozen 172-byte production payload "
            "(first difference at %s)"
            % next((i for i in range(min(len(blob), 172))
                    if blob[i] != bytes.fromhex(CARRIER_HEX)[i]), "len"))

    # Identity exclusivity: no v1 note anywhere (design 3.2/4.2).
    require(not any(s["name"] == ".note.mcs251.abi" for s in sections),
            "v2 object carries the v1 .note.mcs251.abi note")

    # Envelope arithmetic (3.1): 0x41 | BE VendorSize=16+P | "MCS251\0" |
    # scope 1 | BE ScopeSize=5+P | P payload bytes.
    require(blob[0] == 0x41, "envelope format byte 0x%02X, expected 0x41" % blob[0])
    payload = blob[17:]
    vendor_size = int.from_bytes(blob[1:5], "big")
    scope_tag = blob[12]
    scope_size = int.from_bytes(blob[13:17], "big")
    require(vendor_size == 16 + len(payload),
            "VendorSize %d, expected 16+%d" % (vendor_size, len(payload)))
    require(blob[5:12] == b"MCS251\x00", "vendor string is not MCS251\\0")
    require(scope_tag == 1, "scope tag %d, expected 1 (File)" % scope_tag)
    require(scope_size == 5 + len(payload),
            "ScopeSize %d, expected 5+%d" % (scope_size, len(payload)))
    ok("carrier: 172B envelope exact (0x41, VendorSize 171, MCS251\\0, "
       "scope 1, ScopeSize 160, 155B payload); no v1 note anywhere")

    # TLV decode: shortest-form ULEB, strictly increasing, 21 required tags.
    def uleb(buf, pos, what):
        val = 0
        shift = 0
        start = pos
        while True:
            require(pos < len(buf), "truncated ULEB128 in %s" % what)
            byte = buf[pos]
            pos += 1
            val |= (byte & 0x7F) << shift
            if not byte & 0x80:
                break
            shift += 7
        require(pos - start == 1 or buf[pos - 1] != 0,
                "non-shortest ULEB128 in %s" % what)
        require(pos - start <= 5, "ULEB128 longer than 5 bytes in %s" % what)
        return val, pos

    pos = 0
    seen = []
    values = {}
    while pos < len(payload):
        tag, pos = uleb(payload, pos, "tag")
        require(pos < len(payload), "missing TypeFlags for tag %d" % tag)
        tf = payload[pos]
        pos += 1
        crit = bool(tf & 0x80)
        vt = tf & 0x7F
        length, pos = uleb(payload, pos, "record length")
        require(pos + length <= len(payload),
                "tag %d value overruns the payload" % tag)
        val = payload[pos:pos + length]
        pos += length
        if vt == 0x01:              # VT_U32, big-endian on MCS-251
            require(length == 4, "tag %d U32 length %d, expected 4" % (tag, length))
            values[tag] = int.from_bytes(val, "big")
        elif vt == 0x03:            # VT_MIX of U32 atoms
            p = 0
            atoms = []
            while p < len(val):
                require(val[p] == 0x01,
                        "tag %d MIX atom type 0x%02X, expected U32" % (tag, val[p]))
                p += 1
                alen, p = uleb(val, p, "MIX atom length")
                require(p + alen <= len(val), "tag %d MIX atom overruns" % tag)
                require(alen == 4, "tag %d MIX atom length %d, expected 4"
                        % (tag, alen))
                atoms.append(int.from_bytes(val[p:p + 4], "big"))
                p += 4
            require(p == len(val), "tag %d MIX atoms do not end exactly at "
                    "the record end" % tag)
            values[tag] = tuple(atoms)
        else:
            raise Fail("tag %d has unregistered value type 0x%02X" % (tag, vt))
        require(crit, "tag %d is not Critical (all required tags are)" % tag)
        require(not seen or tag > seen[-1],
                "tag %d out of strictly increasing order" % tag)
        seen.append(tag)

    require(len(seen) == REQUIRED_TAG_COUNT,
            "decoded %d tags, expected the 21 RequiredTags" % len(seen))
    for tag, name, kind, want in CARRIER_TAGS:
        require(tag in values, "required tag %d (%s) missing" % (tag, name))
        got = values[tag]
        if kind == "u32":
            require(not isinstance(got, tuple),
                    "tag %d (%s) encoded as MIX, expected U32" % (tag, name))
            require(got == want, "tag %d (%s) = %d, expected %d"
                    % (tag, name, got, want))
        else:
            require(isinstance(got, tuple) and len(got) == 2,
                    "tag %d (%s) not a two-atom MIX" % (tag, name))
            require(got == tuple(want), "tag %d (%s) = %r, expected %r"
                    % (tag, name, got, want))
    for tag in (21, 22, 23):
        require(tag not in values,
                "reserved tag %d present; the registered payload omits 21-23"
                % tag)

    # Cross-field agreement the codec and lld both rely on.
    require(values[4] == (eh["flags"] & 0xFF),
            "Tag 4 (%d) disagrees with the e_flags protocol byte (%d)"
            % (values[4], eh["flags"] & 0xFF))
    require(values[24] == (values[11], values[13]),
            "Tag 24 memory_model_profile %r disagrees with Tag 11/13 (%d,%d)"
            % (values[24], values[11], values[13]))
    ok("carrier payload: 21 required tags, strict order, shortest ULEB, "
       "PM-ruled values (OPV=2, CallABI 2/1, RPV=3, ASLV=2, contracts=2, "
       "placement=8 XSmall, MMP=(32,8), CMP=1, caps=0, ABIOptions=0, "
       "OPm=0); Tag4==e_flags low byte, Tag11/13/24 agree")


# ---------------------------------------------------------------------------
# Template half (frozen from the v1 fixtures).
# ---------------------------------------------------------------------------

def check_home(data, sections):
    home = by_name(sections, ".mcs251.HOME")
    require(home["type"] == SHT_PROGBITS, "HOME is not SHT_PROGBITS")
    require(home["flags"] == (SHF_ALLOC | SHF_EXECINSTR),
            "HOME flags 0x%X, expected ALLOC|EXECINSTR" % home["flags"])
    require(home["size"] == HOME_SIZE, "HOME size %d, expected 3" % home["size"])
    require(content(data, home) == HOME_BYTES,
            "HOME content is not 02 00 00 (ljmp placeholder)")
    ok("HOME: 3-byte ljmp reset trampoline 02 00 00 (v1 template unchanged)")


def decode_template(boot, template, size, what):
    covered = {}
    for off, length, hexbytes, label in template:
        for i in range(length):
            require(off + i not in covered,
                    "template overlap at 0x%02X (%s)" % (off + i, label))
            covered[off + i] = off
        require(off + length <= size,
                "template instruction %s past %s end" % (label, what))
        want = bytes.fromhex(hexbytes)
        got = boot[off:off + length]
        require(got == want,
                "%s 0x%02X..0x%02X (%s): got %s, expected %s"
                % (what, off, off + length - 1, label, got.hex().upper(),
                   hexbytes))
    require(len(covered) == size,
            "template covers %d of %d %s bytes" % (len(covered), size, what))
    return [(off, ln, bytes.fromhex(hx), lb) for off, ln, hx, lb in template]


def check_rela_set(data, eh, sections, symbols, name, target_sec, frozen,
                   only_type=None, banned_type=None):
    symtab = by_name(sections, ".symtab")
    symbols_by_index = {s["index"]: s for s in symbols}
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
        sym_idx = r["symbol"]
        require(sym_idx in symbols_by_index,
                "%s: relocation symbol index %d out of range" % (name, sym_idx))
        require(symbols_by_index[sym_idx]["name"] == rname,
                "%s: offset 0x%X targets %r, expected %r"
                % (name, off, symbols_by_index[sym_idx]["name"], rname))
    return relas


def check_symbols(sections, symbols, defined_globals, locals_map, undef_names,
                  extra_func_symbols=()):
    defined = {}
    undef = []
    for s in symbols:
        if s["shndx"] == SHN_UNDEF:
            undef.append(s)
        else:
            defined[s["name"]] = s
    for name, (secname, off) in defined_globals.items():
        require(name in defined, "missing defined symbol %r" % name)
        s = defined[name]
        require(s["bind"] == STB_GLOBAL, "%r must be GLOBAL" % name)
        target = by_name(sections, secname)
        require(s["shndx"] == target["index"], "%r not in %s" % (name, secname))
        require(s["value"] == off, "%r value 0x%X, expected 0x%X"
                % (name, s["value"], off))
    for name, secname, off, size in extra_func_symbols:
        require(name in defined, "missing defined symbol %r" % name)
        s = defined[name]
        require(s["bind"] == STB_GLOBAL and s["type"] == STT_FUNC,
                "%r must be GLOBAL STT_FUNC" % name)
        target = by_name(sections, secname)
        require(s["shndx"] == target["index"], "%r not in %s" % (name, secname))
        require(s["value"] == off, "%r value 0x%X, expected 0x%X"
                % (name, s["value"], off))
        require(s["size"] == size, "%r size %d, expected %d"
                % (name, s["size"], size))
        require(s["value"] + s["size"] <= target["size"],
                "%r function range overruns its section" % name)
    for name, (secname, off) in locals_map.items():
        require(name in defined, "missing local symbol %r" % name)
        s = defined[name]
        require(s["bind"] != STB_GLOBAL, "local %r must not be GLOBAL" % name)
        target = by_name(sections, secname)
        require(s["shndx"] == target["index"], "%r not in %s" % (name, secname))
        require(s["value"] == off, "%r value 0x%X, expected 0x%X"
                % (name, s["value"], off))
    got_undef = sorted(s["name"] for s in undef)
    require(got_undef == sorted(undef_names),
            "undefined GLOBAL set %s mismatch (expected %s)"
            % (got_undef, sorted(undef_names)))
    for s in undef:
        require(s["bind"] == STB_GLOBAL, "undefined %r not GLOBAL" % s["name"])
    named = [s["name"] for s in symbols if s["name"]]
    require(len(named) == len(set(named)),
            "duplicate named symbol in the frozen CRT symbol table")


def check_selfstart(data, eh, sections, symbols):
    names = [s["name"] for s in sections[1:]]
    for want in SS_SECTIONS:
        require(names.count(want) == 1,
                "expected exactly one section %r, found %d"
                % (want, names.count(want)))
    for name in names:
        require(name in SS_SECTIONS, "unexpected section %r" % name)
    ok("section set is exactly the v1 selfstart whitelist + carrier")

    # VECS: eight 4-byte PROGBITS ejmp fragments + 4-byte NOBITS holes,
    # alternating in section order (SPEC 4.3).
    for i in range(8):
        bytes_name = ".mcs251.VECS.%d.bytes" % i
        hole_name = ".mcs251.VECS.%d.hole" % i
        bs = by_name(sections, bytes_name)
        hs = by_name(sections, hole_name)
        require(bs["type"] == SHT_PROGBITS and
                bs["flags"] == (SHF_ALLOC | SHF_EXECINSTR) and bs["size"] == 4,
                "%s is not a 4-byte ALLOC|EXEC PROGBITS" % bytes_name)
        require(hs["type"] == SHT_NOBITS and hs["size"] == 4,
                "%s is not a 4-byte NOBITS hole" % hole_name)
        require(content(data, bs) == bytes.fromhex("8A000000"),
                "%s content is not the ejmp placeholder 8A 00 00 00" % bytes_name)
        require(names.index(bytes_name) < names.index(hole_name),
                "VECS fragment pair %d not in bytes-then-hole order" % i)
    ok("VECS: 8 fragment pairs (4B ejmp + 4B NOBITS hole), alternating order")

    boot_sec = by_name(sections, ".mcs251.BOOT")
    require(boot_sec["type"] == SHT_PROGBITS, "BOOT is not SHT_PROGBITS")
    require(boot_sec["flags"] == (SHF_ALLOC | SHF_EXECINSTR),
            "BOOT flags 0x%X, expected ALLOC|EXECINSTR" % boot_sec["flags"])
    require(boot_sec["size"] == SS_BOOT_SIZE, "BOOT size %d, expected 0x%X"
            % (boot_sec["size"], SS_BOOT_SIZE))
    insns = decode_template(content(data, boot_sec), SS_BOOT_TEMPLATE,
                            SS_BOOT_SIZE, "BOOT")
    ok("BOOT: 0x%X bytes byte-exact against the frozen v1 template "
       "(prologue + XINIT walker + XDATA walker + SBUF tail)" % SS_BOOT_SIZE)
    # Policy at frozen boundaries: the startup order globals_init ->
    # xdata_init -> _main, and the halt immediately after _main returns.
    order = [lb for _, _, _, lb in insns if lb.startswith("ecall ")]
    require(order == ["ecall __mcs251_globals_init",
                      "ecall __mcs251_xdata_init", "ecall _main"],
            "startup ECALL order changed: %r" % order)
    ok("startup order preserved: globals_init -> xdata_init -> _main -> halt")

    check_rela_set(data, eh, sections, symbols, ".rela.mcs251.HOME",
                   by_name(sections, ".mcs251.HOME"), SS_HOME_RELOCS,
                   banned_type=R_MCS251_ISR_REF)
    for i in range(8):
        check_rela_set(data, eh, sections, symbols,
                       ".rela.mcs251.VECS.%d.bytes" % i,
                       by_name(sections, ".mcs251.VECS.%d.bytes" % i),
                       SS_VECS_RELOCS, banned_type=R_MCS251_ISR_REF)
    check_rela_set(data, eh, sections, symbols, ".rela.mcs251.BOOT",
                   boot_sec, SS_BOOT_RELOCS, banned_type=R_MCS251_ISR_REF)
    ok("relocations: HOME J16, 8x VECS R24 (default entry), BOOT 12 frozen "
       "offsets - all identical to the v1 fixture")

    check_symbols(sections, symbols, SS_DEFINED_GLOBALS, SS_LOCALS, SS_UNDEF)
    ok("symbols: frozen defined/local/undefined sets identical to the v1 "
       "fixture (_main, __mcs251_stack_base, XINIT/XDATA_INIT boundaries)")


def check_irq(data, eh, sections, symbols):
    names = [s["name"] for s in sections[1:]]
    for want in IRQ_SECTIONS:
        require(names.count(want) == 1,
                "expected exactly one section %r, found %d"
                % (want, names.count(want)))
    for name in names:
        require(name in IRQ_SECTIONS, "unexpected section %r" % name)
        require("VECS" not in name, "unexpected VECS section %r" % name)
    ok("section set is exactly the v1 IRQ whitelist + carrier; no VECS")

    boot_sec = by_name(sections, ".mcs251.BOOT")
    require(boot_sec["size"] == IRQ_BOOT_SIZE, "BOOT size %d, expected 0x%X"
            % (boot_sec["size"], IRQ_BOOT_SIZE))
    insns = decode_template(content(data, boot_sec), IRQ_BOOT_TEMPLATE,
                            IRQ_BOOT_SIZE, "BOOT")
    ok("BOOT: 0x%X bytes byte-exact against the frozen T08 template "
       "(prologue + BSEG clear + ECALLs + walkers + default entry)"
       % IRQ_BOOT_SIZE)

    # Policy at frozen boundaries (T08): no SETB; EA only at the two clr EA
    # sites; default entry is the frozen 4-byte word.
    for off, ln, raw, label in insns:
        require(raw[0] != 0xD2, "SETB-family instruction at 0x%02X (%s)"
                % (off, label))
    ea_insns = [(off, raw) for off, ln, raw, _ in insns if 0xAF in raw[1:]]
    require(len(ea_insns) == 2 and [o for o, _ in ea_insns] == [0x00, IRQ_DEFAULT_OFF],
            "EA bit 0xAF touched outside the two frozen clr EA sites")
    for off, raw in ea_insns:
        require(raw[0] == 0xC2, "EA touched by non-CLR at 0x%02X" % off)
    default = content(data, boot_sec)[IRQ_DEFAULT_OFF:IRQ_DEFAULT_OFF + 4]
    require(default == bytes.fromhex("C2AF80FE"),
            "default entry %s, expected C2AF80FE" % default.hex().upper())
    ok("no SETB; EA only at 0x00 and the default entry C2AF80FE")

    bseg = by_name(sections, ".mcs251.BSEG_BYTES")
    require(bseg["type"] == SHT_NOBITS and
            bseg["flags"] == (SHF_ALLOC | SHF_WRITE) and
            bseg["size"] == IRQ_BSEG_SIZE and bseg["align"] == 1 and
            bseg["addr"] == 0,
            "BSEG_BYTES reservation shape changed")
    ok("BSEG_BYTES: 16B writable NOBITS reservation, align 1, ET_REL addr 0")

    # ISR records: ProtocolVersion 1 kept (G1 owns the bump; W7 card).
    isr = by_name(sections, ".mcs251.isr")
    require(isr["type"] == SHT_PROGBITS and isr["flags"] == 0 and
            isr["align"] == 4 and isr["entsize"] == 0,
            ".mcs251.isr shape changed")
    blob = content(data, isr)
    require(len(blob) == 2 * IRQ_RECORD_SIZE,
            ".mcs251.isr size %d, expected 2x24" % len(blob))
    for idx, want in enumerate(IRQ_ISR_RECORDS):
        f = struct.unpack(">HHBBBBHH4sI4s",
                          blob[idx * 24:(idx + 1) * 24])
        (version, rsize, kind, entry, hw, save, slot, caps,
         symref, asset, reserved) = f
        require((version, rsize) == (want["version"], want["record_size"]),
                "record %d header version/size bad (%d/%d)"
                % (idx, version, rsize))
        require(version == 1,
                "record %d ProtocolVersion %d, expected 1 (ISR protocol "
                "stays v1 in A4; the ELF identity is a separate protocol)"
                % (idx, version))
        require(kind == want["kind"] and entry == want["entry"],
                "record %d kind/entry mismatch" % idx)
        require(hw == want["hw"] and save == want["save"],
                "record %d hw/save mismatch" % idx)
        require(slot == want["slot"] and caps == want["caps"],
                "record %d slot/caps mismatch" % idx)
        require(symref == b"\x00" * 4 and reserved == b"\x00" * 4,
                "record %d symbol_reference/reserved not zero" % idx)
        require(asset == want["asset"], "record %d asset_profile mismatch" % idx)
    ok("ISR records: IRQ_DEFAULT + IRQ_RESET, ProtocolVersion 1 kept, "
       "24B each, asset_profile 1")

    check_rela_set(data, eh, sections, symbols, ".rela.mcs251.HOME",
                   by_name(sections, ".mcs251.HOME"), IRQ_HOME_RELOCS,
                   banned_type=R_MCS251_ISR_REF)
    check_rela_set(data, eh, sections, symbols, ".rela.mcs251.BOOT",
                   boot_sec, IRQ_BOOT_RELOCS, banned_type=R_MCS251_ISR_REF)
    relas = check_rela_set(data, eh, sections, symbols, ".rela.mcs251.isr",
                           isr, IRQ_ISR_RELOCS, only_type=R_MCS251_ISR_REF)
    require(sorted(r["offset"] for r in relas) ==
            [12, IRQ_RECORD_SIZE + 12],
            "type9 offsets are not record_offset+12")
    ok("relocations: HOME J16, BOOT 12 frozen offsets, two zero-width "
       "R_MCS251_ISR_REF at record_offset+12")

    check_symbols(
        sections, symbols,
        defined_globals={
            "__mcs251_selfstart_boot": (".mcs251.BOOT", 0x0),
            "__mcs251_globals_init": (".mcs251.BOOT", 0x4A),
            "__mcs251_xdata_init": (".mcs251.BOOT", 0xA0),
        },
        locals_map={
            "__mcs251_halt": (".mcs251.BOOT", 0x48),
            "__mcs251_xinit_record": (".mcs251.BOOT", 0x56),
            "__mcs251_xinit_clear": (".mcs251.BOOT", 0x7A),
            "__mcs251_xinit_copy": (".mcs251.BOOT", 0x89),
            "__mcs251_xinit_done": (".mcs251.BOOT", 0x9F),
            "__mcs251_xdata_record": (".mcs251.BOOT", 0xAC),
            "__mcs251_xdata_copy": (".mcs251.BOOT", 0xDF),
            "__mcs251_xdata_zero": (".mcs251.BOOT", 0xF4),
            "__mcs251_xdata_done": (".mcs251.BOOT", 0x101),
        },
        undef_names=["_main", "__mcs251_stack_base", "s_XINIT", "l_XINIT",
                     "s_XDATA_INIT", "l_XDATA_INIT"],
        extra_func_symbols=[
            ("__mcs251_reset", ".mcs251.HOME", 0x0, 3),
            ("__mcs251_isr_unhandled", ".mcs251.BOOT", IRQ_DEFAULT_OFF,
             IRQ_DEFAULT_SIZE),
        ])
    ok("symbols: frozen IRQ sets (STT_FUNC reset/default entries, walker "
       "globals, lld-synthesized undefined requests)")


def main():
    if len(sys.argv) < 2:
        print("usage: check-crt-v2.py FILE.o [FILE.o ...]", file=sys.stderr)
        return 2
    for path in sys.argv[1:]:
        CHECKS[0] = 0  # per-file: the count reports this file, not a total
        with open(path, "rb") as f:
            data = f.read()
        eh = parse_elf(data)
        sections = parse_sections(data, eh)
        names = [s["name"] for s in sections]
        is_selfstart = any(n.startswith(".mcs251.VECS.") for n in names)
        is_irq = ".mcs251.isr" in names
        require(is_selfstart != is_irq,
                "cannot classify variant: VECS=%s isr=%s"
                % (is_selfstart, is_irq))
        check_identity(data, eh)
        check_carrier(data, eh, sections)
        symbols = parse_symbols(data, eh, sections)
        if is_selfstart:
            check_home(data, sections)
            check_selfstart(data, eh, sections, symbols)
            variant = "selfstart"
        else:
            check_home(data, sections)
            check_irq(data, eh, sections, symbols)
            variant = "irq"
        print("check-crt-v2: PASS (%d checks, %s) %s"
              % (CHECKS[0], variant, path))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fail as e:
        print("check-crt-v2: FAIL: %s" % e, file=sys.stderr)
        sys.exit(1)
    except Exception as e:  # structural damage (truncated table, bad entsize...)
        print("check-crt-v2: FAIL: malformed object (%s: %s)"
              % (type(e).__name__, e), file=sys.stderr)
        sys.exit(1)
