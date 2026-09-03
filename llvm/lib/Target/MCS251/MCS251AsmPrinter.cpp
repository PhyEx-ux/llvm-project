//===-- MCS251AsmPrinter.cpp - MCS-251 assembly writer -------------------===//

#include "MCS251.h"
#include "MCS251MCInstLower.h"
#include "MCS251TargetMachine.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

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

    MCS251MCInstLower MCInstLowering(OutContext);
    MCInst TmpInst;
    MCInstLowering.Lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
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
