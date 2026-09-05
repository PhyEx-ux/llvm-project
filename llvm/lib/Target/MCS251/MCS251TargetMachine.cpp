//===-- MCS251TargetMachine.cpp - MCS-251 target machine -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251TargetMachine.h"
#include "MCS251.h"
#include "MCS251TargetObjectFile.h"
#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "MCTargetDesc/MCS251RELObjectWriter.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

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
      TLOF(std::make_unique<MCS251TargetObjectFile>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  // ASxxxx REL has no address-significance table; accept the flag as a no-op.
  this->Options.EmitAddrsig = false;
  initAsmInfo();
}

MCS251TargetMachine::~MCS251TargetMachine() = default;

// The object path streams ASxxxx REL, not ELF (Phase 13a).  The default
// implementation would ask the MCAsmBackend for an object writer, which for
// our (still ELF) TLOF means an ELFObjectWriter, so the REL streamer is
// supplied explicitly here.  Assembly/null output keeps the generic path.
Expected<std::unique_ptr<MCStreamer>> MCS251TargetMachine::createMCStreamer(
    raw_pwrite_stream &Out, raw_pwrite_stream *, CodeGenFileType FileType,
    MCContext &Ctx) {
  if (FileType != CodeGenFileType::ObjectFile)
    return CodeGenTargetMachineImpl::createMCStreamer(Out, nullptr, FileType,
                                                       Ctx);

  const MCSubtargetInfo &STI = getMCSubtargetInfo();
  const MCRegisterInfo &MRI = getMCRegisterInfo();
  const MCInstrInfo &MII = *getMCInstrInfo();
  std::unique_ptr<MCCodeEmitter> E(createMCS251MCCodeEmitter(MII, Ctx));
  std::unique_ptr<MCAsmBackend> B(
      createMCS251MCAsmBackend(getTarget(), STI, MRI, Options.MCOptions));
  std::unique_ptr<MCObjectWriter> W(createMCS251ObjectWriter(Out));
  MCStreamer *S = createMCS251RELStreamer(getTargetTriple(), Ctx,
                                          std::move(B), std::move(W),
                                          std::move(E));
  return std::unique_ptr<MCStreamer>(S);
}

namespace {
class MCS251PassConfig final : public TargetPassConfig {
public:
  MCS251PassConfig(MCS251TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    // Long conditional branches depend on the adjacent short-skip block.
    // See MCS251TargetLowering::expandLongConditionalBranch. Re-enable only
    // after implementing analyzeBranch/insertBranch/removeBranch AND real
    // branch relaxation. Until then the generic folder cannot rewrite CFGs.
    setEnableTailMerge(false);
  }

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
