//===-- MCS251Subtarget.h - MCS-251 subtarget information ------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251SUBTARGET_H
#define LLVM_LIB_TARGET_MCS251_MCS251SUBTARGET_H

#include "MCS251FrameLowering.h"
#include "MCS251ISelLowering.h"
#include "MCS251InstrInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "MCS251GenSubtargetInfo.inc"

namespace llvm {
class MCS251Subtarget final : public MCS251GenSubtargetInfo {
  MCS251InstrInfo InstrInfo;
  MCS251TargetLowering TLInfo;
  MCS251FrameLowering FrameLowering;
  SelectionDAGTargetInfo TSInfo;

public:
  MCS251Subtarget(const Triple &TT, const std::string &CPU,
                  const std::string &FS, const TargetMachine &TM);

  MCS251Subtarget &initializeSubtargetDependencies(StringRef CPU,
                                                    StringRef FS);
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  const MCS251InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const MCS251RegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const MCS251FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const MCS251TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
};
} // namespace llvm

#endif
