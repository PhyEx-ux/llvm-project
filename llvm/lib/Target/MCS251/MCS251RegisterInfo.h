//===-- MCS251RegisterInfo.h - MCS-251 register information ----*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251REGISTERINFO_H
#define LLVM_LIB_TARGET_MCS251_MCS251REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "MCS251GenRegisterInfo.inc"

namespace llvm {
class MCS251RegisterInfo final : public MCS251GenRegisterInfo {
  unsigned PointerBits;

public:
  explicit MCS251RegisterInfo(unsigned PointerBits = 32);

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  const TargetRegisterClass *
  getPointerRegClass(unsigned Kind = 0) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;
};
} // namespace llvm

#endif
