//===-- MCS251.h - Top-level MCS-251 backend interface ----------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251_H
#define LLVM_LIB_TARGET_MCS251_MCS251_H

#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class MCS251TargetMachine;
class PassRegistry;

FunctionPass *createMCS251ISelDag(MCS251TargetMachine &TM,
                                  CodeGenOptLevel OptLevel);

void initializeMCS251AsmPrinterPass(PassRegistry &);
void initializeMCS251DAGToDAGISelLegacyPass(PassRegistry &);
} // namespace llvm

#endif
