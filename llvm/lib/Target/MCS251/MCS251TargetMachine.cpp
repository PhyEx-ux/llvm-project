//===-- MCS251TargetMachine.cpp - MCS-251 target machine -----------------===//

#include "MCS251TargetMachine.h"
#include "MCS251.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

MCS251TargetMachine::MCS251TargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

MCS251TargetMachine::~MCS251TargetMachine() = default;

namespace {
class MCS251PassConfig final : public TargetPassConfig {
public:
  MCS251PassConfig(MCS251TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  bool addInstSelector() override {
    addPass(createMCS251ISelDag(getTM<MCS251TargetMachine>(), getOptLevel()));
    return false;
  }
};
} // namespace

TargetPassConfig *
MCS251TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MCS251PassConfig(*this, PM);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251Target() {
  RegisterTargetMachine<MCS251TargetMachine> X(getTheMCS251Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeMCS251AsmPrinterPass(PR);
  initializeMCS251DAGToDAGISelLegacyPass(PR);
}
