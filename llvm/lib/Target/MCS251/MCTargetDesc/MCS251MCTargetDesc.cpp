//===-- MCS251MCTargetDesc.cpp - MCS-251 MC target descriptions ----------===//

#include "MCS251MCTargetDesc.h"
#include "MCS251InstPrinter.h"
#include "MCS251MCAsmInfo.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "MCS251GenInstrInfo.inc"

// Instantiates MCS251_MC::verifyInstructionPredicates(), used by the
// assembly printer before lowering an instruction to the MC layer.
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

static MCInstPrinter *createMCS251MCInstPrinter(const Triple &TT,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  if (SyntaxVariant != 0)
    return nullptr;
  return new MCS251InstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251TargetMC() {
  Target &T = getTheMCS251Target();
  TargetRegistry::RegisterMCAsmInfo(T, createMCS251MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createMCS251MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMCS251MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMCS251MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMCS251MCInstPrinter);
}
