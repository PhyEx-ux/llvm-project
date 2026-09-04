//===-- MCS251InstrInfo.h - MCS-251 instruction information ----*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251INSTRINFO_H
#define LLVM_LIB_TARGET_MCS251_MCS251INSTRINFO_H

#include "MCS251RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "MCS251GenInstrInfo.inc"

namespace llvm {
class MCS251Subtarget;

class MCS251InstrInfo final : public MCS251GenInstrInfo {
  const MCS251RegisterInfo RI;

  [[noreturn]] void reportBadSpillClass(const TargetRegisterClass *RC) const;

public:
  explicit MCS251InstrInfo(const MCS251Subtarget &STI);
  const MCS251RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, Register SrcReg,
                           bool IsKill, int FrameIndex,
                           const TargetRegisterClass *RC, Register VReg,
                           MachineInstr::MIFlag Flags) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            Register VReg, unsigned SubReg,
                            MachineInstr::MIFlag Flags) const override;
};
} // namespace llvm

#endif
