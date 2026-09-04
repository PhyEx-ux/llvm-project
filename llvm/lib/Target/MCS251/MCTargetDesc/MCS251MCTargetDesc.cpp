//===-- MCS251MCTargetDesc.cpp - MCS-251 MC target descriptions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251MCTargetDesc.h"
#include "MCS251InstPrinter.h"
#include "MCS251MCAsmInfo.h"
#include "MCS251MCCodeEmitter.h"
#include "MCS251RELObjectWriter.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "MCS251GenInstrInfo.inc"

#define ENABLE_INSTR_PREDICATE_VERIFIER
#define GET_INSTRINFO_MC_HELPERS
#include "MCS251GenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MCS251GenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MCS251GenSubtargetInfo.inc"

static MCInstrInfo *createMCS251MCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitMCS251MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMCS251MCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  InitMCS251MCRegisterInfo(X, 0);
  return X;
}

static MCAsmInfo *createMCS251MCAsmInfo(const MCRegisterInfo &MRI,
                                        const Triple &TT,
                                        const MCTargetOptions &Options) {
  return new MCS251MCAsmInfo(TT, Options);
}

static MCSubtargetInfo *createMCS251MCSubtargetInfo(const Triple &TT,
                                                    StringRef CPU,
                                                    StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createMCS251MCSubtargetInfoImpl(TT, CPU, CPU, FS);
}

static MCInstPrinter *createMCS251MCInstPrinter(
    const Triple &TT, unsigned SyntaxVariant, const MCAsmInfo &MAI,
    const MCInstrInfo &MII, const MCRegisterInfo &MRI) {
  if (SyntaxVariant != 0)
    return nullptr;
  return new MCS251InstPrinter(MAI, MII, MRI);
}

namespace {
// MCELFStreamer hard-codes an ELFObjectWriter in getWriter() and therefore
// cannot be used for ASxxxx REL.  MCObjectStreamer contains all target-neutral
// fragment/fixup mechanics we need; this small target streamer supplies the
// only ELF-ish operations AsmPrinter performs before handing fragments to the
// MCS251RELObjectWriter.
class MCS251ObjectStreamer final : public MCObjectStreamer {
public:
  MCS251ObjectStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                       std::unique_ptr<MCObjectWriter> OW,
                       std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter)) {}

  // Claim raw-text support so the generic AsmPrinter machinery runs, then
  // drop the text: the ASxxxx module prologue that
  // MCS251AsmPrinter::emitStartOfAsmFile emits via emitRawText is regenerated
  // by the REL writer as the M/O/A records, and no other emitRawText user
  // exists in this backend today.  (If one ever appears, it must be mapped
  // to records here -- dropping it silently would then be wrong.)
  bool hasRawTextSupport() const override { return true; }
  void emitRawTextImpl(StringRef) override {}

  // No section preamble: the single code area is declared by the writer's
  // A records, and section switching never reaches an object file anyway.
  void initSections(const MCSubtargetInfo &) override {}

  bool emitSymbolAttribute(MCSymbol *Symbol,
                           MCSymbolAttr Attribute) override {
    getAssembler().registerSymbol(*Symbol);
    // The MCContext builds MCSymbolELF symbols (the TargetMachine keeps the
    // ELF TLOF), so the ELF binding field doubles as our "is this symbol
    // .globl-ed" marker for the writer's S records.  ASxxxx has no concept
    // of the remaining attributes (visibility styles, cold, ...); they carry
    // no linkage semantics we support, so they are intentionally ignored.
    auto *ELFSym = static_cast<MCSymbolELF *>(Symbol);
    switch (Attribute) {
      case MCSA_Global:
        ELFSym->setBinding(ELF::STB_GLOBAL);
        break;
      case MCSA_Weak:
      case MCSA_WeakReference:
        ELFSym->setBinding(ELF::STB_WEAK);
        break;
      case MCSA_Local:
        ELFSym->setBinding(ELF::STB_LOCAL);
        break;
    default:
      break;
    }
    return true;
  }

  void emitCommonSymbol(MCSymbol *, uint64_t, Align) override {
    report_fatal_error("MCS251 REL writer: common symbols are not supported");
  }
};

} // namespace

MCStreamer *llvm::createMCS251RELStreamer(
    const Triple &, MCContext &Context, std::unique_ptr<MCAsmBackend> &&TAB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return new MCS251ObjectStreamer(Context, std::move(TAB), std::move(OW),
                                  std::move(Emitter));
}

std::unique_ptr<MCObjectWriter>
llvm::createMCS251ObjectWriter(raw_pwrite_stream &OS) {
  return createMCS251RELObjectWriter(OS);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251TargetMC() {
  Target &T = getTheMCS251Target();
  TargetRegistry::RegisterMCAsmInfo(T, createMCS251MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createMCS251MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMCS251MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMCS251MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMCS251MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createMCS251MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createMCS251MCAsmBackend);
  // The ELFStreamer slot is the only object-streamer hook in the registry;
  // the triple claims ELF binformat so far (the TLOF is ELF), so generic
  // clients (llvm-mc -filetype=obj, MCJIT) get the ASxxxx REL streamer here.
  TargetRegistry::RegisterELFStreamer(T, createMCS251RELStreamer);
}
