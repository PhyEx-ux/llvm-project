//===-- MCS251InstrInfo.cpp - MCS-251 instruction information ------------===//

#include "MCS251InstrInfo.h"
#include "MCS251.h"
#include "MCS251Subtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "MCS251GenInstrInfo.inc"

MCS251InstrInfo::MCS251InstrInfo(const MCS251Subtarget &STI)
    : MCS251GenInstrInfo(STI, RI, MCS251::ADJCALLSTACKDOWN,
                        MCS251::ADJCALLSTACKUP), RI() {}

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
