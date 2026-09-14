//===-- MCS251InstrInfo.cpp - MCS-251 instruction information ------------===//

#include "MCS251InstrInfo.h"
#include "MCS251.h"
#include "MCS251Subtarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "MCS251GenInstrInfo.inc"

MCS251InstrInfo::MCS251InstrInfo(const MCS251Subtarget &STI,
                                 unsigned PointerBits)
    : MCS251GenInstrInfo(STI, RI, MCS251::ADJCALLSTACKDOWN,
                        MCS251::ADJCALLSTACKUP), RI(PointerBits) {}

bool MCS251InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() != MCS251::SRL32one &&
      MI.getOpcode() != MCS251::SRA32one)
    return false;
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  assert(Dst == MI.getOperand(1).getReg() && "expected tied DR shift");
  Register Hi = RI.getSubReg(Dst, MCS251::sub_hi16);
  Register Lo = RI.getSubReg(Dst, MCS251::sub_lo16);
  Register LoHi = RI.getSubReg(Lo, MCS251::sub_hi8);
  unsigned HiOpc = MI.getOpcode() == MCS251::SRA32one ? MCS251::SRA16
                                                    : MCS251::SRL16;
  // QEMU-measured five-instruction shift: save old bit16 in A.7, then
  // merge it into the low word. DR0 is WR0:WR2 (most significant first).
  BuildMI(MBB, MI, DL, get(HiOpc), Hi).addReg(Hi);
  BuildMI(MBB, MI, DL, get(MCS251::MOVAI)).addImm(0);
  BuildMI(MBB, MI, DL, get(MCS251::RRCA));
  BuildMI(MBB, MI, DL, get(MCS251::SRL16), Lo).addReg(Lo);
  BuildMI(MBB, MI, DL, get(MCS251::OR8a), LoHi).addReg(LoHi);
  MI.eraseFromParent();
  return true;
}

// Spill/reload (Phase 9). The slot address is emitted in its unresolved
// frame-index form -- the mcs251_stack displacement operand carries the
// (FI, offset) pair and a placeholder dr60 base, and PEI folds it into the
// final @dr60/@dr16 displacement (see MCS251RegisterInfo::
// eliminateFrameIndex). Word-granular spill slots use the single WR form
// (mov @dr60+dis,wr / mov wr,@dr60+dis): i16 values MUST spill as one
// instruction pair, never as byte lanes.
[[noreturn]] void
MCS251InstrInfo::reportBadSpillClass(const TargetRegisterClass *RC) const {
  report_fatal_error(Twine("MCS251: cannot spill a value of register class ") +
                     RI.getRegClassName(RC));
}

void MCS251InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();

  if (MCS251::GPR8RegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8mrF))
        .addReg(MCS251::DR60) // placeholder base; PEI substitutes
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addReg(SrcReg, getKillRegState(IsKill))
        .setMIFlag(Flags);
    return;
  }
  if (MCS251::GPR16RegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV16mrF))
        .addReg(MCS251::DR60)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addReg(SrcReg, getKillRegState(IsKill))
        .setMIFlag(Flags);
    return;
  }
  if (MCS251::GPR32RegClass.hasSubClassEq(RC)) {
    // One complete DR use: InlineSpiller must not reason about two separate
    // partial-register definitions/uses. PEI expands after register allocation.
    BuildMI(MBB, MI, DL, get(MCS251::MOV32mrF))
        .addReg(MCS251::DR60)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addReg(SrcReg, getKillRegState(IsKill))
        .setMIFlag(Flags);
    return;
  }
  reportBadSpillClass(RC);
}

void MCS251InstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    unsigned SubReg, MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  assert(SubReg == 0 && "no sub-register reload path in this backend");

  if (MCS251::GPR8RegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8rmF), DestReg)
        .addReg(MCS251::DR60)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .setMIFlag(Flags);
    return;
  }
  if (MCS251::GPR16RegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV16rmF), DestReg)
        .addReg(MCS251::DR60)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .setMIFlag(Flags);
    return;
  }
  if (MCS251::GPR32RegClass.hasSubClassEq(RC)) {
    // A single full definition gives both Greedy and FastRA one live range.
    BuildMI(MBB, MI, DL, get(MCS251::MOV32rmF), DestReg)
        .addReg(MCS251::DR60)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .setMIFlag(Flags);
    return;
  }
  reportBadSpillClass(RC);
}

void MCS251InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  const DebugLoc &DL, Register DestReg,
                                  Register SrcReg, bool KillSrc,
                                  bool RenamableDest, bool RenamableSrc) const {
  if (MCS251::GPR8RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8rr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (MCS251::GPR16RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV16rr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (MCS251::GPR32RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV32rr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Moves into the fixed SFR return-value locations.
  if (DestReg == MCS251::DPL && MCS251::GPR8RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8dpl))
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (DestReg == MCS251::DPH && MCS251::GPR8RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8dph))
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (DestReg == MCS251::A && MCS251::GPR8RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8a))
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (DestReg == MCS251::B && MCS251::GPR8RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(MCS251::MOV8b))
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (DestReg == MCS251::DPTR && MCS251::GPR16RegClass.contains(SrcReg)) {
    // dptr can never be the destination of a register-to-register move;
    // load the lanes individually. Per the byte order ruling sub_lo8 is the
    // least significant byte, which the ABI expects in dpl. The kill flag
    // goes on the last use of the source pair.
    Register Lo = RI.getSubReg(SrcReg, MCS251::sub_lo8);
    Register Hi = RI.getSubReg(SrcReg, MCS251::sub_hi8);
    BuildMI(MBB, MI, DL, get(MCS251::MOV8dpl)).addReg(Lo);
    BuildMI(MBB, MI, DL, get(MCS251::MOV8dph))
        .addReg(Hi, getKillRegState(KillSrc));
    return;
  }

  // Reads of the fixed SFR argument locations (parameter live-ins produced
  // by LowerFormalArguments). MOV8rdpl/MOV8rdph declare their source only
  // via Uses, so the implicit dpl/dph use operand is attached automatically
  // on construction; operand 1 is that implicit use and carries the kill
  // flag, since operand 0 is the explicit destination.
  if (SrcReg == MCS251::DPL && MCS251::GPR8RegClass.contains(DestReg)) {
    auto MIB = BuildMI(MBB, MI, DL, get(MCS251::MOV8rdpl), DestReg);
    MIB->getOperand(1).setIsKill(KillSrc);
    return;
  }
  if (SrcReg == MCS251::DPH && MCS251::GPR8RegClass.contains(DestReg)) {
    auto MIB = BuildMI(MBB, MI, DL, get(MCS251::MOV8rdph), DestReg);
    MIB->getOperand(1).setIsKill(KillSrc);
    return;
  }
  if (SrcReg == MCS251::A && MCS251::GPR8RegClass.contains(DestReg)) {
    auto MIB = BuildMI(MBB, MI, DL, get(MCS251::MOV8ra), DestReg);
    MIB->getOperand(1).setIsKill(KillSrc);
    return;
  }
  if (SrcReg == MCS251::B && MCS251::GPR8RegClass.contains(DestReg)) {
    auto MIB = BuildMI(MBB, MI, DL, get(MCS251::MOV8rb), DestReg);
    MIB->getOperand(1).setIsKill(KillSrc);
    return;
  }

  if (SrcReg == MCS251::DPTR && MCS251::GPR16RegClass.contains(DestReg)) {
    // Mirror of the DPTR write direction above: the pair is never a source
    // of a 16-bit register-to-register move, so read the lanes separately
    // (sub_lo8 -> dpl, sub_hi8 -> dph) and kill on the last lane read.
    Register Lo = RI.getSubReg(DestReg, MCS251::sub_lo8);
    Register Hi = RI.getSubReg(DestReg, MCS251::sub_hi8);
    BuildMI(MBB, MI, DL, get(MCS251::MOV8rdpl), Lo);
    auto MIB = BuildMI(MBB, MI, DL, get(MCS251::MOV8rdph), Hi);
    MIB->getOperand(1).setIsKill(KillSrc);
    return;
  }

  llvm_unreachable("unsupported MCS251 register copy");
}

//===----------------------------------------------------------------------===//
//  Branch encoding facts and exact instruction sizes (assessment E1).
//
//  Every conditional branch is 2 bytes with a signed rel8 displacement
//  measured from the byte after the instruction (+/-128 bytes); sjmp is the
//  same shape; ejmp carries a 24-bit absolute target and reaches anywhere.
//  The post-layout MCS251BranchRelaxation pass uses these facts to rewrite
//  any rel8 branch that the final layout pushed out of range.  Before that
//  pass existed, FinalizeISel's three-part expansion relied on the skip
//  block staying layout-adjacent to its branch -- an invariant
//  MachineBlockPlacement does not actually guarantee, which made large
//  control-flow graphs fail assembly with "MCS251 PC-relative branch out of
//  range".
//
//  getInstSizeInBytes below mirrors the MC code emitter byte-for-byte
//  (MCTargetDesc/MCS251MCCodeEmitter.cpp).  getInstSizeVerifyMode asks the
//  AsmPrinter to check each declared size against the emitted byte count on
//  object output -- via ExactSizeAlways, so the check also runs in release
//  builds -- so any future drift between the two tables fails loudly
//  instead of corrupting branch displacement computation.
//
//  The generic TargetInstrInfo::analyzeBranch/insertBranch/removeBranch
//  hooks are deliberately NOT implemented: making terminators analyzable
//  would silently activate the generic branch folder, tail duplication and
//  MachineBasicBlock::updateTerminator, a pipeline-wide codegen change this
//  backend has not signed up for.  The relaxation pass parses the three
//  terminator shapes this backend emits directly instead.
//===----------------------------------------------------------------------===//

bool MCS251InstrInfo::isCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case MCS251::JE:
  case MCS251::JNE:
  case MCS251::JC:
  case MCS251::JNC:
  case MCS251::JG:
  case MCS251::JLE:
  case MCS251::JSL:
  case MCS251::JSGE:
  case MCS251::JSG:
  case MCS251::JSLE:
  // Bit-test branches share the rel8 shape (opcode + bit address + rel8);
  // operand 0 is the target for all of them. JBC is included: its taken edge
  // branches (after clearing the bit), its fallthrough does not -- relaxing it
  // keeps the SAME opcode/condition and only redirects the target through a
  // trampoline, so the bit is still tested and cleared exactly once.
  case MCS251::JB:
  case MCS251::JNB:
  case MCS251::JBC:
    return true;
  default:
    return false;
  }
}

bool MCS251InstrInfo::isUncondBranchOpcode(unsigned Opc) {
  return Opc == MCS251::SJMP || Opc == MCS251::EJMP;
}

// BT12: the bit-address operand position of a bit-addressed instruction (see
// the declaration for the contract). Only the explicit bit-addressed forms are
// listed; the carry forms (SETBC/CLRC/CPLC) take no operand and never carry a
// handle, precisely because the bit address is implicit in the mnemonic.
bool MCS251InstrInfo::isBitAddrOperand(unsigned Opc, unsigned OpIdx) {
  switch (Opc) {
  case MCS251::SETBBIT:
  case MCS251::CLRBIT:
  case MCS251::CPLBIT:
  case MCS251::MOVCBIT:
  case MCS251::MOVBITC:
    // The only explicit operand is the bit address.
    return OpIdx == 0;
  case MCS251::JB:
  case MCS251::JNB:
  case MCS251::JBC:
    // Operand 0 is the branch target; operand 1 is the bit address.
    return OpIdx == 1;
  default:
    return false;
  }
}

unsigned MCS251InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  // Exact encoded size of every target instruction, mirroring the MC code
  // emitter (policed by getInstSizeVerifyMode on object output, in release
  // builds too).  Generic opcodes emit no bytes by the time they reach the
  // printer; anything else reaching the emitter with a pseudo is a hard
  // error there.
  if (Opc <= (unsigned)TargetOpcode::GENERIC_OP_END)
    return 0;
  // Pseudo target opcodes exist only before FinalizeISel/PEI have finished;
  // the pre-RA heuristic callers of this hook (e.g. MachineFunction::
  // estimateFunctionSizeInBytes under Early Tail Duplication) only need an
  // estimate, so report 0 there.  After register allocation (NoVRegs) a
  // pseudo is an unexpanded compiler bug and must not silently contribute a
  // fake size to the relaxation arithmetic.
  if (MI.isPseudo()) {
    if (MI.getParent()->getParent()->getProperties().hasProperty(
            MachineFunctionProperties::Property::NoVRegs))
      report_fatal_error("MCS251: cannot size unexpanded pseudo '" +
                         Twine(getName(Opc)) + "' after register allocation");
    return 0;
  }
  switch (Opc) {
  case MCS251::JE:
  case MCS251::JNE:
  case MCS251::JC:
  case MCS251::JNC:
  case MCS251::JG:
  case MCS251::JLE:
  case MCS251::JSL:
  case MCS251::JSGE:
  case MCS251::JSG:
  case MCS251::JSLE:
  case MCS251::SJMP:
    return 2; // opcode + rel8
  case MCS251::EJMP:
  case MCS251::ECALL:
    return 4; // opcode + addr24
  case MCS251::ECALLr:
    // 0x199 (native, no A5 escape) + specifier byte (regCode<<4)|0x8.
    return 2;
  case MCS251::ERET:
  case MCS251::RETI:
  case MCS251::MULAB:
  case MCS251::RRCA:
  case MCS251::RLCA:
  case MCS251::CLRC:
  // XDATA channel: the single-byte MOVX @DPTR pair (sdas251 gold E0 / F0;
  // low nibble 0, no A5 escape).
  case MCS251::MOVXALD:
  case MCS251::MOVXAST:
  // BRJT: `jmp @a+dptr` (0x73, low nibble 3 < 6, no A5 escape).
  case MCS251::JMPIAD:
  // CY bit forms: opcode+1 of the single-byte classic bit family.
  case MCS251::SETBC:
  case MCS251::CPLC:
    return 1;
  case MCS251::MOVADDR32:
    return 8;
  case MCS251::MOVDRri:
  case MCS251::MOVHDRi:
  case MCS251::MOV16ri:
  case MCS251::ADD16ri:
  case MCS251::SUB16ri:
  case MCS251::AND16ri:
  case MCS251::OR16ri:
  case MCS251::XOR16ri:
  case MCS251::CMP16ri:
    return 4;
  case MCS251::MOV8ri:
  // BRJT: `mov dptr,#jt` = 90 hi lo (QEMU-measured 3-byte 0x90 form).
  case MCS251::MOVDPTRri:
  case MCS251::MOV8dpl:
  case MCS251::MOV8dph:
  case MCS251::MOV8dpxl:
  case MCS251::MOV8rdpl:
  case MCS251::MOV8rdph:
  case MCS251::MOV8b:
  case MCS251::MOV8rb:
  case MCS251::MOV8rm:
  case MCS251::MOV8di:
  case MCS251::MOV8mr:
  case MCS251::MOV8id:
  case MCS251::ADD8ri:
  case MCS251::SUB8ri:
  case MCS251::AND8ri:
  case MCS251::OR8ri:
  case MCS251::XOR8ri:
  case MCS251::CMP8ri:
    return 3;
  case MCS251::MOVAI:
  case MCS251::OR8a:
    // MOVAI is opcode 0x74 + imm8: 0x74's low nibble is below 6, so
    // putOpcode adds no A5 escape.  OR8a is native 0x14c + specifier byte.
    return 2;
  case MCS251::MOV8rmD:
  case MCS251::MOV8mrD:
    return 4;
  // Bit-addressed ops: opcode + bit address.
  case MCS251::SETBBIT:
  case MCS251::CLRBIT:
  case MCS251::CPLBIT:
  case MCS251::MOVCBIT:
  case MCS251::MOVBITC:
    return 2;
  // Bit branches: opcode + bit address + rel8.
  case MCS251::JB:
  case MCS251::JNB:
  case MCS251::JBC:
    return 3;
  case MCS251::MOV8a:
  case MCS251::MOV8ra:
    // Always exactly 2 bytes.  The classic rn form (0xe8+rn / 0xf8+rn) has a
    // low nibble >= 6 and no native bit, so the emitter A5-escapes it
    // (gold: "mov a,r0" = A5 E8, "mov r0,a" = A5 F8); the escaped 0x17c form
    // is opcode + specifier.  Exact before register allocation too, so no
    // physical-register fallback is needed.
    return 2;
  case MCS251::MULW:
  case MCS251::INCSPX1:
  case MCS251::INCSPX2:
  case MCS251::INCSPX4:
  case MCS251::DECSPX1:
  case MCS251::DECSPX2:
  case MCS251::DECSPX4:
  case MCS251::SETFP:
  case MCS251::RESTORESP:
  case MCS251::PUSHFP:
  case MCS251::POPFP:
  case MCS251::ISR_PUSH_PSW:
  case MCS251::ISR_POP_PSW:
  case MCS251::ISR_PUSH_DR0:
  case MCS251::ISR_PUSH_DR4:
  case MCS251::ISR_PUSH_DR8:
  case MCS251::ISR_PUSH_DR12:
  case MCS251::ISR_PUSH_DR16:
  case MCS251::ISR_PUSH_DR20:
  case MCS251::ISR_PUSH_DR24:
  case MCS251::ISR_PUSH_DR28:
  case MCS251::ISR_PUSH_DPX:
  case MCS251::ISR_POP_DR0:
  case MCS251::ISR_POP_DR4:
  case MCS251::ISR_POP_DR8:
  case MCS251::ISR_POP_DR12:
  case MCS251::ISR_POP_DR16:
  case MCS251::ISR_POP_DR20:
  case MCS251::ISR_POP_DR24:
  case MCS251::ISR_POP_DR28:
  case MCS251::ISR_POP_DPX:
    return 2;
  case MCS251::MOV8rmP:
  case MCS251::MOV8rmS:
  case MCS251::MOV8mrP:
  case MCS251::MOV8mrS:
  case MCS251::MOV16rmS:
  case MCS251::MOV16mrS: {
    // Zero displacement has the short 3-byte form; the displaced forms are
    // 4 bytes.  The displacement is the plain immediate that follows the
    // base register: operand 1 for the store forms (base, disp, src) and
    // operand 2 for the load forms (dst, base, disp) -- matching the
    // operand indices the MC code emitter reads.
    bool IsStoreForm = MI.getOpcode() == MCS251::MOV8mrP ||
                       MI.getOpcode() == MCS251::MOV8mrS ||
                       MI.getOpcode() == MCS251::MOV16mrS;
    const MachineOperand &Disp = MI.getOperand(IsStoreForm ? 1 : 2);
    if (Disp.isImm() && Disp.getImm() == 0)
      return 3;
    return 4;
  }
  // Register-register forms: opcode byte + RR specifier byte.
  case MCS251::MOV8rr:
  case MCS251::MOV16rr:
  case MCS251::MOV32rr:
  case MCS251::ADD32rr:
  case MCS251::SUB32rr:
  case MCS251::ADD8rr:
  case MCS251::AND8rr:
  case MCS251::OR8rr:
  case MCS251::XOR8rr:
  case MCS251::ADD16rr:
  case MCS251::AND16rr:
  case MCS251::OR16rr:
  case MCS251::XOR16rr:
  case MCS251::SUB8rr:
  case MCS251::SUB16rr:
  case MCS251::CMP8rr:
  case MCS251::CMP16rr:
  case MCS251::CMP32rr:
  // Native 1-bit shifts: opcode byte + mode byte.
  case MCS251::SLL8:
  case MCS251::SRL8:
  case MCS251::SRA8:
  case MCS251::SLL16:
  case MCS251::SRL16:
  case MCS251::SRA16:
    return 2;
  default:
    // A fully expanded target opcode that this table does not know would
    // corrupt the relaxation arithmetic -- fail loudly instead of guessing.
    report_fatal_error("MCS251: cannot size instruction '" +
                       Twine(getName(Opc)) + "' for branch relaxation");
  }
}

TargetInstrInfo::InstSizeVerifyMode
MCS251InstrInfo::getInstSizeVerifyMode(const MachineInstr &MI) const {
  if (MI.getOpcode() <= (unsigned)TargetOpcode::GENERIC_OP_END)
    return InstSizeVerifyMode::NoVerify;
  // Plain ExactSize would only be honored in +asserts builds (upstream
  // NDEBUG semantics), silently dropping the cross-check exactly where it
  // matters: optimized release binaries.  ExactSizeAlways keeps the
  // AsmPrinter check alive in release builds too; no other target returns
  // it, so the release-build cost stays an MCS251-local opt-in.
  return InstSizeVerifyMode::ExactSizeAlways;
}

// The AsmPrinter's ExactSize check compares emitted bytes to
// getInstSizeInBytes; it never inspects the tablegen MCInstrDesc `Size`.  A
// wrong `let Size = N` on the bit-addressed family therefore used to be
// invisible (the base MCS251Inst class defaults Size=1).  Catch that drift
// here, on any MI the machine verifier visits, by requiring the TD Size to
// equal the exact encoded size from getInstSizeInBytes.
bool MCS251InstrInfo::verifyInstruction(const MachineInstr &MI,
                                        StringRef &ErrInfo) const {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case MCS251::SETBBIT:
  case MCS251::CLRBIT:
  case MCS251::CPLBIT:
  case MCS251::MOVCBIT:
  case MCS251::MOVBITC:
  case MCS251::JB:
  case MCS251::JNB:
  case MCS251::JBC:
  case MCS251::SETBC:
  case MCS251::CPLC:
  // X2: the MOVX @DPTR pair carries a fixed Size = 1 in the .td; keep the
  // tablegen Size honest against the emitter's single E0/F0 byte the same
  // way the bit-addressed family is policed.
  case MCS251::MOVXALD:
  case MCS251::MOVXAST:
    break;
  default:
    return true;
  }
  unsigned TDSize = get(Opc).getSize();
  unsigned ExactSize = getInstSizeInBytes(MI);
  if (TDSize != ExactSize) {
    // ErrInfo is a StringRef that MachineVerifier consumes synchronously right
    // after this call, so a function-local static buffer is a safe owner.
    static thread_local std::string SizeErrMsg;
    SizeErrMsg = ("MCS251: tablegen Size (" + Twine(TDSize) + ") of '" +
                  getName(Opc) + "' disagrees with the encoded size (" +
                  Twine(ExactSize) + ")")
                     .str();
    ErrInfo = StringRef(SizeErrMsg);
    return false;
  }
  return true;
}
