//===-- MCS251TargetMachine.cpp - MCS-251 target machine -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251TargetMachine.h"
#include "MCS251.h"
#include "MCS251ContractCheck.h"
#include "MCS251LoweringPrep.h"
#include "MCS251TargetObjectFile.h"
#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "MCTargetDesc/MCS251RELObjectWriter.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/CommandLine.h"
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

#include <algorithm>

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

//===----------------------------------------------------------------------===//
// P1-2: MCS-251 storage-model contract resolution.
//
// The numeric contract used to ride the generic llvm::TargetOptions
// (Options.MCS251Memory), leaking the target into lib/CodeGen. It is now
// resolved here, target-side, from exactly two inputs, and every consumer
// (layout selection, streamer gate, object compatibility, the read-only
// contract check) reads the value stored in this target machine:
//
//   1. The target-feature string: "+mcs251-memory-contract=v-t-as0-p-e"
//      (dash-separated, because the feature string itself is comma-split).
//      This is the transport clang uses (BackendUtil appends it from the
//      frontend TargetOptions) and is authoritative when present.
//   2. The two llc command-line options, moved verbatim from
//      lib/CodeGen/CommandFlags.cpp; their spelling is unchanged.
//
// The parsers are the shared llvm::MCS251TargetParser entry points on both
// sides, so the frontend, the tools and this resolution can never drift.
//===----------------------------------------------------------------------===//

static cl::opt<std::string> MCS251MemoryContract(
    "mcs251-memory-contract", cl::Hidden,
    cl::desc("Numeric MCS-251 memory contract"), cl::init(""));

static cl::opt<std::string> MCS251MemoryModel(
    "mcs251-memory-model",
    cl::desc("MCS-251 storage model (tiny, xtiny, small, xsmall, large)"),
    cl::value_desc("model"), cl::init(""));

// Translate a user-facing -mcs251-memory-model name into the numeric
// transport contract. This command-line translation layer is one of the two
// places (with the clang driver) where model names may appear; everything
// downstream consumes the numeric fields only.
static bool translateMCS251MemoryModel(StringRef Model,
                                       MCS251::MemoryContract &Contract) {
  unsigned AS0Bits = 0, Placement = 0;
  if (Model == "tiny") {
    AS0Bits = 16;
    Placement = 1;
  } else if (Model == "xtiny") {
    AS0Bits = 16;
    Placement = 8;
  } else if (Model == "small") {
    AS0Bits = 32;
    Placement = 1;
  } else if (Model == "xsmall") {
    AS0Bits = 32;
    Placement = 8;
  } else if (Model == "large") {
    AS0Bits = 32;
    Placement = 3;
  } else {
    return false;
  }
  Contract = MCS251::MemoryContract{/*TransportVersion=*/1,
                                    /*ASLayoutVersion=*/2, AS0Bits, Placement,
                                    /*ExecutionContract=*/1};
  return true;
}

// P12-3: the feature-string prefix spelling and its formatter
// (formatMemoryContractFeature) live in the always-linked TargetParser
// library. clang's BackendUtil references the formatter unconditionally, so
// a build without the MCS251 backend must not depend on this translation
// unit for it.

// Scan the feature string for the contract feature. Returns false on a
// malformed or duplicated entry (fatal at the caller, matching the loud
// behavior of every other malformed selection).
static bool getMCS251ContractFeature(
    StringRef FS, std::optional<MCS251::MemoryContract> &Contract) {
  bool Seen = false;
  for (StringRef Entry : llvm::split(FS, ',')) {
    if (Entry.starts_with("-mcs251-memory-contract"))
      reportFatalUsageError("MCS251: the memory contract feature cannot be "
                            "disabled");
    if (!Entry.starts_with(MCS251::getMCS251ContractFeaturePrefix()))
      continue;
    if (Seen)
      return false;
    Seen = true;
    std::string Numeric(Entry.drop_front(
        MCS251::getMCS251ContractFeaturePrefix().size()));
    std::replace(Numeric.begin(), Numeric.end(), '-', ',');
    MCS251::MemoryContract Parsed;
    if (!MCS251::parseMemoryContract(Numeric, Parsed) ||
        !MCS251::isValidMemoryContract(Parsed))
      return false;
    Contract = Parsed;
  }
  return true;
}

// Resolve the contract for one target machine creation. Priority: explicit
// feature string, then the two command-line options (mutually exclusive,
// as before), then the xsmall default that llc-class tools always
// materialized for this triple.
static std::optional<MCS251::MemoryContract>
resolveMCS251MemoryContract(StringRef FS) {
  std::optional<MCS251::MemoryContract> Contract;
  if (!getMCS251ContractFeature(FS, Contract)) {
    reportFatalUsageError("MCS251: invalid '+mcs251-memory-contract=' target feature");
  }
  if (Contract) {
    // The feature transport (clang) and the command-line transport (llc)
    // must not both select a contract; a silent winner would hide a
    // frontend/backend layout disagreement.
    if (MCS251MemoryModel.getNumOccurrences() != 0 ||
        MCS251MemoryContract.getNumOccurrences() != 0)
      reportFatalUsageError("-mcs251-memory-model/-mcs251-memory-contract conflict with the "
          "'+mcs251-memory-contract=' target feature");
    return Contract;
  }

  const bool HasModel = MCS251MemoryModel.getNumOccurrences() != 0;
  const bool HasContractOpt =
      MCS251MemoryContract.getNumOccurrences() != 0;
  if (HasModel && HasContractOpt)
    reportFatalUsageError("-mcs251-memory-model and -mcs251-memory-contract "
                          "are mutually exclusive");
  if (HasModel) {
    MCS251::MemoryContract MC;
    // Presence and value are distinct: an explicitly empty model name is
    // malformed, like an explicitly empty wire contract.
    if (!translateMCS251MemoryModel(MCS251MemoryModel, MC))
      reportFatalUsageError("invalid -mcs251-memory-model");
    return MC;
  }
  if (HasContractOpt) {
    MCS251::MemoryContract MC;
    if (!MCS251::parseMemoryContract(MCS251MemoryContract, MC) ||
        !MCS251::isValidMemoryContract(MC))
      reportFatalUsageError("invalid -mcs251-memory-contract");
    return MC;
  }
  // No user selection: materialize the same xsmall contract the clang cc1
  // default and the old llc path materialized, so clang-produced IR and a
  // bare llc invocation agree on the data layout instead of conflicting.
  return MCS251::MemoryContract{1, 2, 32, 8, 1};
}

static StringRef
getMCS251DataLayout(const std::optional<MCS251::MemoryContract> &Contract) {
  if (!Contract || !Contract->isSpecified())
    return MCS251::getCompatibilityDataLayout();

  if (!MCS251::isValidMemoryContract(*Contract))
    reportFatalUsageError("MCS251: invalid numeric memory contract");

  const auto Version =
      static_cast<MCS251::ASLayoutVersion>(Contract->ASLayoutVersion);
  const auto AS0Bits =
      static_cast<MCS251::AS0PointerBits>(Contract->AS0PointerBits);
  auto Desc = MCS251::getLayoutDesc(Version, AS0Bits);
  if (!Desc)
    reportFatalUsageError("MCS251: numeric memory contract has no data layout");
  return Desc->DataLayout;
}

MCS251TargetMachine::MCS251TargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T,
                               getMCS251DataLayout(
                                   resolveMCS251MemoryContract(FS)),
                               TT, CPU, FS,
                               Options, getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<MCS251TargetObjectFile>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this),
      ELFObjectOutput(MCS251::getObjectFormat() == MCS251::ObjectFormat::ELF),
      MemoryContract(resolveMCS251MemoryContract(FS)) {
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
  if (ObjectFileOutput && MemoryContract &&
      MemoryContract->isSpecified() &&
      MemoryContract->AS0PointerBits == 16)
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
    // The FinalizeISel long-branch expansion and the EJMP-based lowering
    // already avoid long rel8 branches in the common shapes; see
    // MCS251TargetLowering::expandLongConditionalBranch.  Correctness no
    // longer depends on that shape: addPreEmitPass relaxes any rel8 branch
    // that the final layout pushed out of range.  Tail merging stays off for
    // now -- re-enabling it is a pure code-size optimization decision, no
    // longer a correctness requirement.
    setEnableTailMerge(false);
  }

  void addPreEmitPass() override {
    // G7 S3: expand the TFPU window pseudos into the trigger write plus the
    // fixed worst-case NOP-chain delay. This must run AFTER register
    // allocation (the whole window is one instruction while the allocator
    // relocates live values out of R0-R7) and BEFORE branch relaxation (the
    // NOP chain changes instruction sizes).
    addPass(createMCS251TFPUExpandPass());
    // E1 (branch relaxation): the jcc family and sjmp only reach +/-128
    // bytes, and MachineBlockPlacement is free to displace a branch's skip
    // block or fallthrough target, so after the layout is final every
    // out-of-range rel8 branch is rewritten into an equivalent always-
    // reachable ejmp-based form.  Runs after all block-reordering passes;
    // nothing after it changes instruction or block sizes.
    addPass(createMCS251BranchRelaxationPass());
  }

  void addIRPasses() override {
    // This target contract is stricter than generic LLVM IR validity. Run it
    // before any optimizer can erase an unused illegal declaration, alloca or
    // pointer payload, and do not tie it to the generic -disable-verify flag.
    // RC-6: only structural checks (types, address spaces) run here. The
    // arithmetic checks (i64/f32/f64 ops) run post-optimization in addPreISel
    // so that foldable or dead wide/float operations are not falsely rejected.
    // The check itself is read-only: it never folds or erases IR.
    //
    // WP4: when the clang backend owns the verdict (it reports through its
    // DiagnosticsEngine instead of the fatal error path, avoiding the
    // in-process crash-recovery path), the pass is not mounted here.
    if (!MCS251::isContractCheckDeferred())
      addPass(MCS251::createMCS251ContractCheckPass(
          getTM<MCS251TargetMachine>().getMemoryContract(),
          /*CheckArithmetic=*/false));
    // P1-2: the only IR-mutating preparation (alloca constant propagation,
    // constant folding, targeted wide/float DCE), mounted early and skipping
    // optnone functions entirely. Non-optnone functions must be prepared
    // before the arithmetic check below judges them.
    addPass(MCS251::createMCS251LoweringPrepPass());
    TargetPassConfig::addIRPasses();
  }

  bool addPreISel() override {
    // RC-6: After IR preparation/optimization, check that no unsupported
    // i64/f32/f64 arithmetic survives to the backend. Foldable constants and
    // dead code have been eliminated (MCS251LoweringPrep for non-optnone
    // functions, local interpretation inside this check for optnone ones),
    // so only genuinely live operations that would reach instruction
    // selection are rejected.
    if (!MCS251::isContractCheckDeferred())
      addPass(MCS251::createMCS251ContractCheckPass(
          getTM<MCS251TargetMachine>().getMemoryContract(),
          /*CheckArithmetic=*/true));
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
  // before optimization can erase unused illegal declarations or payloads;
  // the IR-mutating preparation follows immediately after.
  PB.registerPipelineStartEPCallback(
      [this](ModulePassManager &MPM, OptimizationLevel) {
        // WP4: skipped when the clang backend owns the checks (see addIRPasses).
        if (!MCS251::isContractCheckDeferred())
          MPM.addPass(MCS251::MCS251ContractCheckPass(
              getMemoryContract(), /*CheckArithmetic=*/false));
        MPM.addPass(MCS251::MCS251LoweringPrepPass());
      });
  // RC-6: Arithmetic checks run after optimization so that foldable or dead
  // i64/f32/f64 operations are not falsely rejected.
  PB.registerOptimizerLastEPCallback(
      [this](ModulePassManager &MPM, OptimizationLevel,
             ThinOrFullLTOPhase) {
        // WP4: skipped when the clang backend owns the checks (see addIRPasses).
        if (!MCS251::isContractCheckDeferred())
          MPM.addPass(MCS251::MCS251ContractCheckPass(
              getMemoryContract(), /*CheckArithmetic=*/true));
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
  initializeMCS251BranchRelaxationPass(PR);
  initializeMCS251DAGToDAGISelLegacyPass(PR);
  initializeMCS251LoweringPrepLegacyPass(PR);
  initializeMCS251TFPUExpandPass(PR);
}
