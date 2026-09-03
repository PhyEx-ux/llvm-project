//===-- MCS251TargetMachine.h - MCS-251 target machine ---------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251TARGETMACHINE_H
#define LLVM_LIB_TARGET_MCS251_MCS251TARGETMACHINE_H

#include "MCS251Subtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <optional>

namespace llvm {
class MCS251TargetMachine final : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  MCS251Subtarget Subtarget;

public:
  MCS251TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                      StringRef FS, const TargetOptions &Options,
                      std::optional<Reloc::Model> RM,
                      std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                      bool JIT);
  ~MCS251TargetMachine() override;

  const MCS251Subtarget *
  getSubtargetImpl(const Function &F) const override {
    return &Subtarget;
  }

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};
} // namespace llvm

#endif
