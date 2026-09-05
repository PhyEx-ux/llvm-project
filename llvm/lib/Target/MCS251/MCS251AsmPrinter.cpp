//===-- MCS251AsmPrinter.cpp - MCS-251 assembly writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emits complete ASxxxx (sdas251) assembly modules directly:
//
//         .module <name>
//         .source
//         .optsdcc <SDCC ABI signature>
//         .area CSEG (CODE)
//         .globl _foo
// _foo:
//         <instructions>
//
// Design notes (Phase 12, Step 1 "strip SDCC"):
//
//  - The module prologue below replaces the ELF prologue the generic
//    AsmPrinter machinery would print.  Everything else is already
//    dialect-compatible: the InstPrinter's operand spellings (#0x12,
//    #(_sym+1), @wr14-0x1234, ...) were validated against sdas251 as-is,
//    .globl/labels/comments match, and the file just ends after the last
//    instruction (ASxxxx needs no .end).
//
//  - Sections: the TargetMachine still uses TargetLoweringObjectFileELF
//    because the codegen internals need MCSection objects to reason about,
//    but no section switching directive can ever reach the output:
//    MCS251MCAsmInfo::printSwitchToSection is a no-op, so the ".text" the
//    first MCAsmStreamer::switchSection would print is suppressed and all
//    functions flow into the single CSEG area opened here.  A custom TLOF
//    returning a bespoke MCSection subclass would be the heavier-handed
//    alternative (MCSectionELF is final, so it would mean a new section
//    variant); suppressing the printed directive is the minimal change.
//
//  - The trailing ".section .note.GNU-stack" from AsmPrinter::doFinalization
//    is suppressed by MCS251MCAsmInfo::getStackSection returning nullptr.
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "MCS251MCInstLower.h"
#include "MCS251TargetMachine.h"
#include "MCTargetDesc/MCS251ABISignature.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// The .optsdcc line is the SDCC ABI signature this backend interoperates
// with.  sdld checks it (when linking with --mcs251-abi / strict checks) and
// the smoke harness compares it literally, so it must stay byte-for-byte
// identical to what the reference SDCC toolchain emits.  Locked against the
// Phase 12 specimen
// (/tmp/mcs251-p12/sample/llvm-mcs251-asxxxx-specimen.asm), which was
// assembled, linked and executed under QEMU to validate the exact string.
// The payload is shared with the object writer's O record
// (MCTargetDesc/MCS251ABISignature.h).
static const std::string OptsdccSignature =
    (".optsdcc " + std::string(MCS251::ABISignaturePayload));

// Derive the ASxxxx module name the way SDCC does: the stem of the source
// file name, with anything outside [A-Za-z0-9_] mapped to '_' and a '_'
// prepended when the result would start with a digit (ASxxxx symbols must
// begin with a letter).
static std::string getMCS251ModuleName(const Module &M) {
  std::string Name = sys::path::stem(M.getSourceFileName()).str();
  if (Name.empty())
    Name = sys::path::stem(M.getModuleIdentifier()).str();
  if (Name.empty())
    return "mcs251_module";
  for (char &C : Name)
    if (!isAlnum(static_cast<unsigned char>(C)) && C != '_')
      C = '_';
  if (isdigit(static_cast<unsigned char>(Name.front())))
    Name.insert(Name.begin(), '_');
  return Name;
}

namespace {
class MCS251AsmPrinter final : public AsmPrinter {
public:
  static char ID;

  MCS251AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override {
    return "MCS251 Assembly Printer";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    emitFunctionBody();
    return false;
  }

  void emitInstruction(const MachineInstr *MI) override {
    MCS251_MC::verifyInstructionPredicates(MI->getOpcode(),
                                           getSubtargetInfo().getFeatureBits());

    MCS251MCInstLower MCInstLowering(*this);
    MCInst TmpInst;
    MCInstLowering.Lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
  }

  void emitStartOfAsmFile(Module &M) override {
    // ASxxxx module prologue.  ".source" is emitted bare, exactly like the
    // validated specimen and the smoke crt0 template (sdas251 accepts it
    // without a file argument; a filename argument was never exercised).
    const std::string ModuleName = getMCS251ModuleName(M);
    OutStreamer->emitRawText("\t.module " + ModuleName);
    OutStreamer->emitRawText("\t.source");
    OutStreamer->emitRawText(OptsdccSignature);
    OutStreamer->emitRawText("");
    OutStreamer->emitRawText("\t.area CSEG (CODE)");

    // Object path (Phase 13a): the MCS251ObjectStreamer deliberately
    // swallows the raw-text prologue above and the REL writer regenerates
    // the equivalent M/O/A records.  The writer has no access to the Module,
    // so bridge the sanitized module name through the MCContext (the only
    // other MainFileName consumer is DWARF line-table setup, which this
    // target never enables).
    OutStreamer->getContext().setMainFileName(ModuleName);
  }

  // The initial data path is deliberately read-only and shares the one CSEG
  // section with functions. Region-qualified MOVADDR32 accesses can read ROM;
  // using an ELF .rodata section would instead violate the REL writer's
  // single-section contract. Mutable data needs a separate area and startup
  // initialization, neither of which is implied by accepting constants here.
  void emitGlobalVariable(const GlobalVariable *GV) override {
    if (GV->isDeclaration()) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    auto Reject = []() {
      report_fatal_error(
          "MCS251: defined global data requires a byte-aligned read-only "
          "CSEG i8/i16/i32 scalar or nonempty initialized integer array; "
          "mutable data, zeroinitializers, custom sections, TLS, weak/COMDAT, "
          "aggregates and initializer relocations are not supported");
    };
    const DataLayout &DL = GV->getDataLayout();
    if (!GV->isConstant() || GV->isThreadLocal() || GV->getAddressSpace() != 0 ||
        GV->hasSection() || GV->hasComdat() ||
        (!GV->hasExternalLinkage() && !GV->hasLocalLinkage()) ||
        GV->getVisibility() != GlobalValue::DefaultVisibility ||
        GV->getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
        GV->getAlign().valueOrOne() != Align(1) ||
        DL.getABITypeAlign(GV->getValueType()) != Align(1))
      Reject();

    const Constant *Init = GV->getInitializer();
    auto IsSupportedInt = [](Type *Ty) {
      return Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32);
    };
    if (auto *AT = dyn_cast<ArrayType>(Init->getType())) {
      if (!AT->getNumElements() || !IsSupportedInt(AT->getElementType()) ||
          (!isa<ConstantDataArray>(Init) && !isa<ConstantArray>(Init)))
        Reject();
      // Validate the whole initializer before emitting any label or data.
      for (uint64_t I = 0; I != AT->getNumElements(); ++I)
        if (!isa_and_nonnull<ConstantInt>(Init->getAggregateElement(I)))
          Reject();
    } else if (!isa<ConstantInt>(Init) || !IsSupportedInt(Init->getType())) {
      Reject();
    }

    OutStreamer->switchSection(OutContext.getObjectFileInfo()->getTextSection());
    MCSymbol *Sym = getSymbol(GV);
    emitLinkage(GV, Sym);
    OutStreamer->emitLabel(Sym);
    if (auto *AT = dyn_cast<ArrayType>(Init->getType())) {
      // Emit elements, not .ascii/.asciz/.fill: those generic optimizations
      // use GAS spellings/escaping which are not the sdas251 string dialect.
      // emitIntValue preserves the MCAsmInfo big-endian .word fallback for
      // i32 and emits actual zero bytes for embedded NULs (not reservations).
      unsigned Bytes = DL.getTypeStoreSize(AT->getElementType());
      for (uint64_t I = 0; I != AT->getNumElements(); ++I)
        OutStreamer->emitIntValue(
            cast<ConstantInt>(Init->getAggregateElement(I))->getZExtValue(),
            Bytes);
    } else {
      OutStreamer->emitIntValue(cast<ConstantInt>(Init)->getZExtValue(),
                               DL.getTypeStoreSize(Init->getType()));
    }
  }
};
} // namespace

char MCS251AsmPrinter::ID = 0;

INITIALIZE_PASS(MCS251AsmPrinter, "mcs251-asm-printer",
                "MCS251 Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251AsmPrinter() {
  RegisterAsmPrinter<MCS251AsmPrinter> X(getTheMCS251Target());
}
