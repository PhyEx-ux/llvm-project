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
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Instructions.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/MCS251ContractVerifier.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/MCS251TargetParser.h"

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static StringRef getMCS251DataLayout(const TargetOptions &Options) {
  const MCS251::MemoryContract &Contract = Options.MCS251Memory;
  if (!Contract.isSpecified())
    return MCS251::getCompatibilityDataLayout();

  if (!MCS251::isValidMemoryContract(Contract))
    reportFatalUsageError("MCS251: invalid numeric memory contract");

  const auto Version =
      static_cast<MCS251::ASLayoutVersion>(Contract.ASLayoutVersion);
  const auto AS0Bits =
      static_cast<MCS251::AS0PointerBits>(Contract.AS0PointerBits);
  auto Desc = MCS251::getLayoutDesc(Version, AS0Bits);
  if (!Desc)
    reportFatalUsageError(
        "MCS251: numeric memory contract has no data layout");
  return Desc->DataLayout;
}

MCS251TargetMachine::MCS251TargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, getMCS251DataLayout(Options), TT, CPU, FS,
                               Options, getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<MCS251TargetObjectFile>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this),
      ELFObjectOutput(MCS251::getObjectFormat() == MCS251::ObjectFormat::ELF) {
  // Neither the REL path nor ELF ABI v1 uses address-significance tables.
  // Preserve the established no-op behavior of -addrsig.
  this->Options.EmitAddrsig = false;
  initAsmInfo();
}

MCS251TargetMachine::~MCS251TargetMachine() = default;

// REL remains the default, with its original explicit writer/streamer pair.
// ELF is opt-in and uses the real MC ELF writer. The triple/TLOF alone cannot
// select between them. Assembly/null output retains the legacy generic path.
Expected<std::unique_ptr<MCStreamer>> MCS251TargetMachine::createMCStreamer(
    raw_pwrite_stream &Out, raw_pwrite_stream *, CodeGenFileType FileType,
    MCContext &Ctx) {
  if (usesELFObjects() && FileType != CodeGenFileType::ObjectFile)
    return make_error<StringError>(
        "MCS251 ELF output requires -filetype=obj", inconvertibleErrorCode());
  ObjectFileOutput = FileType == CodeGenFileType::ObjectFile;
  if (ObjectFileOutput && Options.MCS251Memory.isSpecified() &&
      Options.MCS251Memory.AS0PointerBits == 16)
    return make_error<StringError>(
        "MCS251 16-bit pointer ABI cannot emit relocatable objects until the "
        "v2 ABI attributes and linker compatibility gate are implemented",
        inconvertibleErrorCode());
  if (usesELFObjects() && Options.MCOptions.Crel)
    return make_error<StringError>(
        "MCS251 ELF ABI v1 requires RELA; CREL is not supported",
        inconvertibleErrorCode());
  if (FileType != CodeGenFileType::ObjectFile)
    return CodeGenTargetMachineImpl::createMCStreamer(Out, nullptr, FileType,
                                                       Ctx);

  const MCInstrInfo &MII = *getMCInstrInfo();
  std::unique_ptr<MCCodeEmitter> E(createMCS251MCCodeEmitter(MII, Ctx));
  std::unique_ptr<MCAsmBackend> B(createMCS251MCAsmBackend(
      usesELFObjects() ? MCS251::ObjectFormat::ELF : MCS251::ObjectFormat::REL));
  if (usesELFObjects()) {
    auto W = B->createObjectWriter(Out);
    return std::unique_ptr<MCStreamer>(createMCS251ELFStreamer(
        getTargetTriple(), Ctx, std::move(B), std::move(W), std::move(E)));
  }
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

  void addIRPasses() override {
    // This target contract is stricter than generic LLVM IR validity. Run it
    // before any optimizer can erase an unused illegal declaration, alloca or
    // pointer payload, and do not tie it to the generic -disable-verify flag.
    // RC-6: only structural checks (types, address spaces) run here. The
    // arithmetic checks (i64/f32/f64 ops) run post-optimization in addPreISel
    // so that foldable or dead wide/float operations are not falsely rejected.
    addPass(MCS251::createMCS251ContractVerifierPass(
        &getTM<MCS251TargetMachine>().Options, /*CheckArithmetic=*/false));
    TargetPassConfig::addIRPasses();
  }

  bool addPreISel() override {
    // RC-6: After IR optimization, check that no unsupported i64/f32/f64
    // arithmetic survives to the backend. Foldable constants and dead code
    // have been eliminated by this point, so only genuinely live operations
    // that would reach instruction selection are rejected. The verifier also
    // performs minimal constant propagation (ignoring optnone) so that
    // foldable i64 operations at -O0 are eliminated before checking.
    addPass(MCS251::createMCS251ContractVerifierPass(
        &getTM<MCS251TargetMachine>().Options, /*CheckArithmetic=*/true));
    return false;
  }

  bool addInstSelector() override {
    addPass(createMCS251ISelDag(getTM<MCS251TargetMachine>(), getOptLevel()));
    return false;
  }
};
} // namespace

void MCS251TargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  // RC-6: Structural checks (types, address spaces) run at pipeline start,
  // before optimization can erase unused illegal declarations or payloads.
  PB.registerPipelineStartEPCallback(
      [this](ModulePassManager &MPM, OptimizationLevel) {
        MPM.addPass(
            MCS251::MCS251ContractVerifierPass(&Options, /*CheckArithmetic=*/false));
      });
  // RC-6: Arithmetic checks run after optimization so that foldable or dead
  // i64/f32/f64 operations are not falsely rejected.
  PB.registerOptimizerLastEPCallback(
      [this](ModulePassManager &MPM, OptimizationLevel,
             ThinOrFullLTOPhase) {
        MPM.addPass(
            MCS251::MCS251ContractVerifierPass(&Options, /*CheckArithmetic=*/true));
      });
}

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
