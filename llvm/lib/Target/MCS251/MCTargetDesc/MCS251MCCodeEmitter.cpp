//===-- MCS251MCCodeEmitter.cpp - MCS-251 code emitter --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Binary encoding for the MCS-251 MC layer (Phase 13a, de-SDCC Step 2).
// Every encoding below was validated byte-for-byte against sdas251 output
// (Moka's Phase-13a gold: instruction-forms-gold.tsv, llvm-form-inventory.tsv
// and the *.lst experiments under /tmp/mcs251-p13a) -- the .rel this emitter
// feeds must be interchangeable with what sdas251 produces from the Step-1
// assembly text.
//
// Conventions:
//
//  * Opcode values below carry a "native Area-III spelling" flag in bit 8
//    (0x100), mirroring sdas251's own 16-bit opcode table (mcs251pst.c).  The
//    emitted byte sequence follows sdas251's source-mode rule
//    (mcs251mch.c needs_prefix, mcs251_source_mode == 1):
//
//        prefix A5  iff  (opcode & 0x0F) >= 6  &&  opcode has no high byte
//
//    i.e. native Area-III opcodes (7E/7A/2F/9A/8A/...) go out bare, while the
//    conflicting classic Area-II opcodes (mov a,rn = E8+rn, mov rn,a = F8+rn)
//    are A5-escaped.  Measured samples: "mov a,r0" -> A5 E8, "eret" -> AA
//    (bare: eret is 0x1AA in the sdas table, hence native), "mov r3,r12" ->
//    7C 3C bare, "ecall" -> 9A bare.  Note the hardware executes what the
//    assembler emits; the QEMU runs in the smoke harness arbitrate this.
//
//  * Register codes: r0-r15 -> 0-15, wrN -> N/2, drN -> N/4, dr56 -> 14,
//    dr60 -> 15 (regCode() maps the LLVM register enum onto these).  A
//    register-to-register second byte is (dst<<4)|src.
//
//  * Fixed SFR ABI locations are reached through their direct address bytes
//    (A=0xE0, B=0xF0, DPL=0x82, DPH=0x83), never through their placeholder
//    HWEncoding values (see MCS251RegisterInfo.td).
//
//  * Multi-byte immediates and addresses are big-endian.
//
//  * Relocatable operands: only 16-bit (fixup_mcs251_16, ASxxxx mode 0x002)
//    and 24-bit (fixup_mcs251_24, mode 0x082) fields exist.  In particular an
//    @wr+dis16 displacement is NEVER relocated (sdas251 emits no R record for
//    it either: the displacement is a plain immediate and the base register
//    carries the symbol) -- a symbolic displacement here would double-count
//    at link time, so it is a hard error, not a silent fixup.
//
//===----------------------------------------------------------------------===//

#include "MCS251MCCodeEmitter.h"
#include "MCS251FixupKinds.h"
#include "MCS251MCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

// regCode() below relies on the generated register enum being contiguous
// within each declaration run of MCS251RegisterInfo.td.  Pin that assumption
// at compile time: if the .td is ever reordered these fire instead of
// silently producing wrong encodings.
static_assert(MCS251::R63 - MCS251::R0 == 63, "R0..R63 enum not contiguous");
static_assert(MCS251::WR30 - MCS251::WR0 == 15, "WR0..WR30 enum not contiguous");
static_assert(MCS251::DR28 - MCS251::DR0 == 7, "DR0..DR28 enum not contiguous");
static_assert(MCS251::DR60 - MCS251::DR56 == 1, "DR56/DR60 enum not adjacent");

namespace {
static constexpr unsigned SFR_B = 0xF0;
static constexpr unsigned SFR_DPL = 0x82;
static constexpr unsigned SFR_DPH = 0x83;

// Map an LLVM register to its architectural encoding (see file comment).
static unsigned regCode(MCRegister R) {
  unsigned N = R.id();
  if (N >= MCS251::R0 && N <= MCS251::R63)
    return N - MCS251::R0;
  // The generated enum is contiguous by register name.  The architectural
  // code, unlike the enum value, is one code per WR/DR register.
  if (N >= MCS251::WR0 && N <= MCS251::WR30)
    return N - MCS251::WR0;
  if (N >= MCS251::DR0 && N <= MCS251::DR28)
    return N - MCS251::DR0;
  if (N == MCS251::DR56)
    return 14;
  if (N == MCS251::DR60)
    return 15;
  llvm_unreachable("unexpected MCS251 register");
}

static void put8(unsigned V, SmallVectorImpl<char> &CB) {
  CB.push_back(char(V & 0xff));
}
static void put16(unsigned V, SmallVectorImpl<char> &CB) {
  put8(V >> 8, CB);
  put8(V, CB);
}

// Emit an opcode byte with the source-mode A5 escape prefix when required
// (see the file comment for the rule).
static void putOpcode(unsigned Op, SmallVectorImpl<char> &CB) {
  const bool Native = (Op & 0x100) != 0;
  if ((Op & 0x0f) >= 6 && !Native)
    put8(0xa5, CB);
  put8(Op, CB);
}

static void putExpr16(const MCOperand &Op, unsigned Offset,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups) {
  if (Op.isExpr()) {
    Fixups.push_back(MCFixup::create(Offset, Op.getExpr(),
                                     MCS251::fixup_mcs251_16));
    put16(0, CB);
    return;
  }
  if (!Op.isImm())
    report_fatal_error("MCS251: expected a 16-bit immediate or expression");
  put16(unsigned(Op.getImm()), CB);
}

static void putExpr24(const MCOperand &Op, unsigned Offset,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups) {
  if (Op.isExpr()) {
    Fixups.push_back(MCFixup::create(Offset, Op.getExpr(),
                                     MCS251::fixup_mcs251_24));
    put8(0, CB);
    put16(0, CB);
    return;
  }
  if (!Op.isImm())
    report_fatal_error("MCS251: expected a 24-bit immediate or expression");
  unsigned V = unsigned(Op.getImm());
  put8(V >> 16, CB);
  put16(V, CB);
}

static void putImm8(const MCOperand &Op, SmallVectorImpl<char> &CB) {
  // Symbolic 8-bit immediates would need the ASxxxx byte-of-24-bit modes
  // (0x103/0x183/0x383), which are deliberately not part of the first object
  // writer version.  Error loudly instead of truncating the symbol value.
  if (!Op.isImm())
    report_fatal_error("MCS251: symbolic 8-bit immediate is not supported; "
                       "use a 16-bit or 24-bit operand");
  put8(unsigned(Op.getImm()), CB);
}

// An @wr+dis16 / @dr+dis16 displacement: always a plain 16-bit immediate.
// A symbol here must not become a relocation (see the file comment), and a
// non-immediate in release builds must not silently mis-encode either.
static void putDisp16(const MCOperand &Op, SmallVectorImpl<char> &CB) {
  if (!Op.isImm())
    report_fatal_error("MCS251: symbolic @wr+dis16 displacement reached the "
                       "MC emitter; ASxxxx has no dis16 relocation (the base "
                       "register carries the symbol)");
  put16(unsigned(Op.getImm()) & 0xffff, CB);
}

static void putBranch(const MCOperand &Op, unsigned Offset,
                      SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups) {
  if (Op.isExpr()) {
    // Direct compiler branches are local and resolve during MC layout.  Keep
    // this as a generic PC-relative byte fixup rather than pretending that a
    // branch displacement is an ASxxxx 16-bit symbol relocation.  The backend
    // applies the MCS-251 two-byte-instruction adjustment when resolved.
    Fixups.push_back(
        MCFixup::create(Offset, Op.getExpr(), FK_Data_1, /*PCRel=*/true));
    put8(0, CB);
    return;
  }
  if (!Op.isImm())
    report_fatal_error("MCS251: expected a branch expression or immediate");
  put8(unsigned(Op.getImm()), CB);
}

static MCRegister getReg(const MCInst &MI, unsigned I) {
  if (!MI.getOperand(I).isReg())
    report_fatal_error("MCS251: expected register operand in MC emitter");
  return MI.getOperand(I).getReg();
}
static unsigned R(const MCInst &MI, unsigned I) {
  return regCode(getReg(MI, I));
}
static unsigned RR(const MCInst &MI, unsigned D, unsigned S) {
  return (R(MI, D) << 4) | R(MI, S);
}
static unsigned Imm(const MCInst &MI, unsigned I) {
  if (!MI.getOperand(I).isImm())
    report_fatal_error("MCS251: expected immediate operand in MC emitter");
  return unsigned(MI.getOperand(I).getImm());
}

// Pseudos must have been expanded before the MC layer (custom inserters in
// FinalizeISel / PEI).  Reaching the emitter with one is a compiler bug;
// crashing loudly here beats emitting a wrong encoding.
static void rejectPseudo(const MCInst &MI) {
  switch (MI.getOpcode()) {
#define MCS251_PSEUDO(Name) case MCS251::Name:
    MCS251_PSEUDO(MOV32ri)
    MCS251_PSEUDO(MUL8)
    MCS251_PSEUDO(MUL16)
    MCS251_PSEUDO(UMUL16WIDE)
    MCS251_PSEUDO(ADJCALLSTACKDOWN)
    MCS251_PSEUDO(ADJCALLSTACKUP)
    MCS251_PSEUDO(SELECT8)
    MCS251_PSEUDO(SELECT16)
    MCS251_PSEUDO(SELECT32)
    MCS251_PSEUDO(MOV8rmF)
    MCS251_PSEUDO(MOV8mrF)
    MCS251_PSEUDO(MOV16rmF)
    MCS251_PSEUDO(MOV16mrF)
    MCS251_PSEUDO(ZEXT8)
    MCS251_PSEUDO(BRCC)
    MCS251_PSEUDO(BRCC8S)
    MCS251_PSEUDO(FIADDR)
    MCS251_PSEUDO(DYNALLOCA)
    MCS251_PSEUDO(SRL32ri)
    MCS251_PSEUDO(SRA32ri)
    MCS251_PSEUDO(VSHIFT8)
    MCS251_PSEUDO(VSHIFT16)
    MCS251_PSEUDO(VSHIFT32)
    MCS251_PSEUDO(SRL32one)
    MCS251_PSEUDO(SRA32one)
    // ADD16fi keeps its frame index only until PEI, which retargets it to
    // ADD16ri (see MCS251RegisterInfo::eliminateFrameIndex).
    MCS251_PSEUDO(ADD16fi)
      report_fatal_error("MCS251: pseudo instruction reached the MC code "
                         "emitter (custom-inserter expansion missing)");
#undef MCS251_PSEUDO
  default:
    break;
  }
}
} // namespace

void MCS251MCCodeEmitter::encodeInstruction(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &) const {
  rejectPseudo(MI);
  const unsigned Op = MI.getOpcode();
  auto B = [&](unsigned O) { putOpcode(O, CB); };
  auto E16 = [&](unsigned I) {
    putExpr16(MI.getOperand(I), CB.size(), CB, Fixups);
  };
  auto E24 = [&](unsigned I) {
    putExpr24(MI.getOperand(I), CB.size(), CB, Fixups);
  };

  switch (Op) {
  // Register-register moves.  The tied two-address ALU instructions below
  // have a separate lhs operand and therefore use operand 2 for the source.
  case MCS251::MOV8rr:
    B(0x17c); put8(RR(MI, 0, 1), CB); break;
  case MCS251::MOV16rr:
    B(0x17d); put8(RR(MI, 0, 1), CB); break;
  case MCS251::MOV32rr:
    B(0x17f); put8(RR(MI, 0, 1), CB); break;
  // DR immediate loads carry the high-half rule in the low nibble: 0x8 fills
  // the upper word with zeroes (mov), 0xC leaves it to a following movh.
  // (gold: mov_dr_imm16_zero "7e 38 12 34" for dr12; linked sample
  // "7E 08 56 78" for dr0 -- NOT nibble 0.)
  case MCS251::MOVADDR32: {
    const MCExpr *Expr = MI.getOperand(1).getExpr();
    auto Byte = [&](MCFixupKind Kind) {
      Fixups.push_back(MCFixup::create(CB.size(), Expr, Kind));
      put8(0, CB);
    };
    B(0x17e); put8((R(MI, 0) << 4) | 0x08, CB);
    Byte(MCS251::fixup_mcs251_mid8);
    Byte(MCS251::fixup_mcs251_lo8);
    B(0x17a); put8((R(MI, 0) << 4) | 0x0c, CB);
    put8(0, CB);
    Byte(MCS251::fixup_mcs251_hi8);
    break;
  }
  case MCS251::MOVDRri:
    B(0x17e); put8((R(MI, 0) << 4) | 0x08, CB); E16(1); break;
  case MCS251::MOVHDRi:
    B(0x17a); put8((R(MI, 0) << 4) | 0x0c, CB); E16(2); break;
  case MCS251::MOV8ri:
    B(0x17e); put8(R(MI, 0) << 4, CB); putImm8(MI.getOperand(1), CB); break;
  case MCS251::MOV16ri:
    B(0x17e); put8((R(MI, 0) << 4) | 4, CB); E16(1); break;

  // Fixed ABI/SFR locations are direct addresses, not their placeholder
  // register HWEncoding values.
  case MCS251::MOV8dpl:
    B(0x17a); put8((R(MI, 0) << 4) | 1, CB); put8(SFR_DPL, CB); break;
  case MCS251::MOV8dph:
    B(0x17a); put8((R(MI, 0) << 4) | 1, CB); put8(SFR_DPH, CB); break;
  case MCS251::MOV8rdpl:
    B(0x17e); put8((R(MI, 0) << 4) | 1, CB); put8(SFR_DPL, CB); break;
  case MCS251::MOV8rdph:
    B(0x17e); put8((R(MI, 0) << 4) | 1, CB); put8(SFR_DPH, CB); break;
  case MCS251::MOVAI:
    B(0x74); putImm8(MI.getOperand(0), CB); break;
  case MCS251::OR8a:
    B(0x14c); put8((R(MI, 0) << 4) | 0x0b, CB); break;
  case MCS251::MOV8a:
    if (R(MI, 0) < 8)
      B(0x0e8 + R(MI, 0));
    else {
      B(0x17c); put8(0xb0 | R(MI, 0), CB);
    }
    break;
  case MCS251::MOV8b:
    B(0x17a); put8((R(MI, 0) << 4) | 1, CB); put8(SFR_B, CB); break;
  case MCS251::MOV8ra:
    if (R(MI, 0) < 8)
      B(0x0f8 + R(MI, 0));
    else {
      B(0x17c); put8((R(MI, 0) << 4) | 0x0b, CB);
    }
    break;
  case MCS251::MOV8rb:
    B(0x17e); put8((R(MI, 0) << 4) | 1, CB); put8(SFR_B, CB); break;

  // Data addressing.  The direct forms use a byte address; @wr forms use a
  // WR code in the high nibble of the mode byte and a byte lane in the third.
  case MCS251::MOV8rm:
    B(0x17e); put8((R(MI, 1) << 4) | 9, CB); put8(R(MI, 0) << 4, CB); break;
  case MCS251::MOV8rmD:
    B(0x109); put8((R(MI, 0) << 4) | R(MI, 1), CB);
    putDisp16(MI.getOperand(2), CB); break;
  case MCS251::MOV8di:
    B(0x17e); put8((R(MI, 0) << 4) | 1, CB); put8(Imm(MI, 1), CB); break;
  case MCS251::MOV8mr:
    B(0x17a); put8((R(MI, 0) << 4) | 9, CB); put8(R(MI, 1) << 4, CB); break;
  case MCS251::MOV8mrD:
    B(0x119); put8((R(MI, 2) << 4) | R(MI, 0), CB);
    putDisp16(MI.getOperand(1), CB); break;
  case MCS251::MOV8id:
    B(0x17a); put8((R(MI, 1) << 4) | 1, CB); put8(Imm(MI, 0), CB); break;

  // sdas251 source-mode gold and frozen-QEMU probe: A4 / AD 64.
  case MCS251::MULAB: B(0xa4); break;
  case MCS251::MULW: B(0x1ad); put8(0x64, CB); break;

  // Native arithmetic and logical operations.
  case MCS251::ADD32rr:
    B(0x12f); put8(RR(MI, 0, 2), CB); break;
  case MCS251::SUB32rr:
    B(0x19f); put8(RR(MI, 0, 2), CB); break;
  case MCS251::ADD8rr:
    B(0x12c); put8(RR(MI, 0, 2), CB); break;
  case MCS251::AND8rr:
    B(0x15c); put8(RR(MI, 0, 2), CB); break;
  case MCS251::OR8rr:
    B(0x14c); put8(RR(MI, 0, 2), CB); break;
  case MCS251::XOR8rr:
    B(0x16c); put8(RR(MI, 0, 2), CB); break;
  case MCS251::ADD16rr:
    B(0x12d); put8(RR(MI, 0, 2), CB); break;
  case MCS251::AND16rr:
    B(0x15d); put8(RR(MI, 0, 2), CB); break;
  case MCS251::OR16rr:
    B(0x14d); put8(RR(MI, 0, 2), CB); break;
  case MCS251::XOR16rr:
    B(0x16d); put8(RR(MI, 0, 2), CB); break;
  case MCS251::SUB8rr:
    B(0x19c); put8(RR(MI, 0, 2), CB); break;
  case MCS251::SUB16rr:
    B(0x19d); put8(RR(MI, 0, 2), CB); break;
#define MCS251_IMM8(Name, O) \
  case MCS251::Name: B(O); put8(R(MI, 0) << 4, CB); putImm8(MI.getOperand(2), CB); break;
  MCS251_IMM8(ADD8ri, 0x12e)
  MCS251_IMM8(SUB8ri, 0x19e)
  MCS251_IMM8(AND8ri, 0x15e)
  MCS251_IMM8(OR8ri, 0x14e)
  MCS251_IMM8(XOR8ri, 0x16e)
#undef MCS251_IMM8
#define MCS251_IMM16(Name, O) \
  case MCS251::Name: B(O); put8((R(MI, 0) << 4) | 4, CB); E16(2); break;
  MCS251_IMM16(ADD16ri, 0x12e)
  MCS251_IMM16(SUB16ri, 0x19e)
  MCS251_IMM16(AND16ri, 0x15e)
  MCS251_IMM16(OR16ri, 0x14e)
  MCS251_IMM16(XOR16ri, 0x16e)
#undef MCS251_IMM16

  // Native 1-bit shifts (Phase 14).  Opcode then specifier byte
  // (code<<4)|mode, byte mode 0, word mode 4.  sdas251 V05.50.4 gold
  // (source mode, opcode bare): sll r3 -> 3E 30, srl r7 -> 1E 70,
  // sra r1 -> 0E 10, sll wr2 -> 3E 14, srl wr6 -> 1E 34, sra wr30 -> 0E F4.
#define MCS251_SHIFT8(Name, O) \
  case MCS251::Name: B(O); put8(R(MI, 0) << 4, CB); break;
#define MCS251_SHIFT16(Name, O) \
  case MCS251::Name: B(O); put8((R(MI, 0) << 4) | 4, CB); break;
  MCS251_SHIFT8(SLL8, 0x13e)
  MCS251_SHIFT8(SRL8, 0x11e)
  MCS251_SHIFT8(SRA8, 0x10e)
  MCS251_SHIFT16(SLL16, 0x13e)
  MCS251_SHIFT16(SRL16, 0x11e)
  MCS251_SHIFT16(SRA16, 0x10e)
#undef MCS251_SHIFT8
#undef MCS251_SHIFT16

  // Classic A-rotates and CY clear for the 32-bit shift expansion.
  // Single-byte, low nibble < 6, so no A5 source-mode escape (gold:
  // rrc a = 13, rlc a = 33, clr c = C3).
  case MCS251::RRCA: B(0x13); break;
  case MCS251::RLCA: B(0x33); break;
  case MCS251::CLRC: B(0xc3); break;

  // Compare has no output/tied operand.
  case MCS251::CMP8rr:
    B(0x1bc); put8(RR(MI, 0, 1), CB); break;
  case MCS251::CMP16rr:
    B(0x1bd); put8(RR(MI, 0, 1), CB); break;
  case MCS251::CMP32rr:
    B(0x1bf); put8(RR(MI, 0, 1), CB); break;
  case MCS251::CMP8ri:
    B(0x1be); put8(R(MI, 0) << 4, CB); putImm8(MI.getOperand(1), CB); break;
  case MCS251::CMP16ri:
    B(0x1be); put8((R(MI, 0) << 4) | 4, CB); E16(1); break;

  // The compiler's long-branch expansion resolves these local rel8 branches;
  // unresolved direct branch symbols are intentionally not an ASxxxx 16-bit
  // relocation.
  case MCS251::JE: B(0x168); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JNE: B(0x178); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JC: B(0x040); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JNC: B(0x050); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JG: B(0x138); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JLE: B(0x128); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JSL: B(0x148); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JSGE: B(0x158); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JSG: B(0x118); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::JSLE: B(0x108); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::SJMP: B(0x080); putBranch(MI.getOperand(0), CB.size(), CB, Fixups); break;
  case MCS251::EJMP: B(0x18a); E24(0); break;
  case MCS251::ECALL: B(0x19a); E24(0); break;
  case MCS251::ECALLr:
    B(0x199); put8((R(MI, 0) << 4) | 8, CB); break;
  case MCS251::ERET: B(0x1aa); break;

  // Stack/frame forms.
  case MCS251::INCSPX1: B(0x10b); put8(0xfc, CB); break;
  case MCS251::INCSPX2: B(0x10b); put8(0xfd, CB); break;
  case MCS251::INCSPX4: B(0x10b); put8(0xfe, CB); break;
  case MCS251::DECSPX1: B(0x11b); put8(0xfc, CB); break;
  case MCS251::DECSPX2: B(0x11b); put8(0xfd, CB); break;
  case MCS251::DECSPX4: B(0x11b); put8(0xfe, CB); break;
  case MCS251::SETFP: B(0x17f); put8(0x4f, CB); break;
  case MCS251::RESTORESP: B(0x17f); put8(0xf4, CB); break;
  case MCS251::PUSHFP: B(0x1ca); put8(0x4b, CB); break;
  case MCS251::POPFP: B(0x1da); put8(0x4b, CB); break;
  // Frame-relative accesses (@dr60+dis16 / @dr56+dis16).  A zero displacement
  // has a three-byte short form WITHOUT the disp16 field -- the assembly path
  // never prints "+0x0000" (MCS251InstPrinter::printStackAddr omits a zero
  // displacement) and sdas251 encodes the bare "@dr60" spelling as these
  // (gold: mov_rm_at_dr "7e 4b 30", mov_wr_at_dr "0b 4a 20",
  // mov_idx_dr_rm/... for the displaced forms).  The object file must contain
  // exactly what the assembly text would have produced.
  case MCS251::MOV8rmP:
  case MCS251::MOV8rmS:
    if (MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 0) {
      B(0x17e); put8((R(MI, 1) << 4) | 0x0b, CB); put8(R(MI, 0) << 4, CB);
    } else {
      B(0x129); put8((R(MI, 0) << 4) | R(MI, 1), CB);
      putDisp16(MI.getOperand(2), CB);
    }
    break;
  case MCS251::MOV16rmS:
    if (MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 0) {
      B(0x10b); put8((R(MI, 1) << 4) | 0x0a, CB); put8(R(MI, 0) << 4, CB);
    } else {
      B(0x169); put8((R(MI, 0) << 4) | R(MI, 1), CB);
      putDisp16(MI.getOperand(2), CB);
    }
    break;
  case MCS251::MOV8mrP:
  case MCS251::MOV8mrS:
    if (MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) {
      B(0x17a); put8((R(MI, 0) << 4) | 0x0b, CB); put8(R(MI, 2) << 4, CB);
    } else {
      B(0x139); put8((R(MI, 2) << 4) | R(MI, 0), CB);
      putDisp16(MI.getOperand(1), CB);
    }
    break;
  case MCS251::MOV16mrS:
    if (MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) {
      B(0x11b); put8((R(MI, 0) << 4) | 0x0a, CB); put8(R(MI, 2) << 4, CB);
    } else {
      B(0x179); put8((R(MI, 2) << 4) | R(MI, 0), CB);
      putDisp16(MI.getOperand(1), CB);
    }
    break;
  default:
    report_fatal_error("MCS251: unsupported instruction in MC emitter: " +
                       MCII.getName(Op));
  }
}

MCCodeEmitter *llvm::createMCS251MCCodeEmitter(const MCInstrInfo &MCII,
                                                MCContext &Ctx) {
  return new MCS251MCCodeEmitter(MCII, Ctx);
}
