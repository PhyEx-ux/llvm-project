//===-- MCS251FrameLowering.h - MCS-251 frame lowering ----------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251FRAMELOWERING_H
#define LLVM_LIB_TARGET_MCS251_MCS251FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class MCS251FrameLowering final : public TargetFrameLowering {
protected:
  bool hasFPImpl(const MachineFunction &MF) const override { return false; }

public:
  MCS251FrameLowering();
  void emitPrologue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
};
} // namespace llvm

#endif
