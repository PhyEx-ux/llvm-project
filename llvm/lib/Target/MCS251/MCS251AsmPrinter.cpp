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
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Module.h"
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
static constexpr const char *OptsdccSignature =
    ".optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small "
    "stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 "
    "all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 "
    "compiler-build=mcs251-abi1.0-r1";

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
    OutStreamer->emitRawText("\t.module " + getMCS251ModuleName(M));
    OutStreamer->emitRawText("\t.source");
    OutStreamer->emitRawText(OptsdccSignature);
    OutStreamer->emitRawText("");
    OutStreamer->emitRawText("\t.area CSEG (CODE)");
  }

  // Defined global data is rejected until data-area emission exists
  // (Step 2+ of the Phase 12 plan).  Everything the base implementation
  // would do is wrong here in a *silent* way, which is the one thing the
  // MCS251 backend must never do: MCS251MCAsmInfo::printSwitchToSection is
  // a no-op, so the switchSection calls in AsmPrinter::emitGlobalVariable
  // would not print ".data"/".rodata"/".section" but leave the output in
  // the CODE area CSEG opened by emitStartOfAsmFile.  The DataLayout's
  // 1-byte i8/i16 alignments make emitAlignment return early, and
  // .globl/.word/.byte/.ds are all legal sdas251 spelling -- so the data
  // would assemble, link and run, just sitting in code space while loads
  // and stores address it as region-00 data.  A loud fatal error beats a
  // quietly mislinked image.
  //
  // External declarations are unaffected: the base path emits nothing for
  // them (default visibility + no initializer returns before any data).
  // Known small exemption: extern_weak globals reach the WeakRefDirective
  // bookkeeping in AsmPrinter::doInitialization and are silently skipped
  // there (the base getWeakRefDirective is empty on this target), so they
  // never get here in the first place.
  void emitGlobalVariable(const GlobalVariable *GV) override {
    if (GV->isDeclaration()) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }
    report_fatal_error(
        "MCS251: defined global data is not supported yet (it would be "
        "emitted into the CODE area CSEG; data-area support is Step 2 of "
        "the Phase 12 plan)");
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
