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
    : MCS251GenInstrInfo(STI, RI), RI() {}

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

  llvm_unreachable("unsupported MCS251 register copy");
}
