//===-- MCS251RegisterInfo.cpp - MCS-251 register information ------------===//

#include "MCS251RegisterInfo.h"
#include "MCS251.h"
#include "MCS251FrameLowering.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "MCS251GenRegisterInfo.inc"

MCS251RegisterInfo::MCS251RegisterInfo() : MCS251GenRegisterInfo(0) {}

const MCPhysReg *
MCS251RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  static const MCPhysReg NoCalleeSaved[] = {0};
  return NoCalleeSaved;
}

BitVector MCS251RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // DR60 (alias spx) is the extended stack pointer. DR56 (alias dpx) is the
  // extended data pointer, reserved for future indirect addressing use. The
  // alias machinery keeps wr56-wr62 and r56-r63 unallocatable as well.
  Reserved.set(MCS251::DR60);
  Reserved.set(MCS251::DR56);
  // DPL/DPH are the ABI return-value locations and DPTR is their 16-bit
  // overlay; none of them belong to an allocatable register class. Reserve
  // them anyway so that no allocator can ever hand them out.
  Reserved.set(MCS251::DPL);
  Reserved.set(MCS251::DPH);
  Reserved.set(MCS251::DPTR);
  return Reserved;
}

const TargetRegisterClass *
MCS251RegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &MCS251::GPR16RegClass;
}

bool MCS251RegisterInfo::eliminateFrameIndex(
    MachineBasicBlock::iterator II, int SPAdj, unsigned FIOperandNum,
    RegScavenger *RS) const {
  llvm_unreachable("MCS251 frame indices are not implemented");
}

Register
MCS251RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return MCS251::DR60;
}
