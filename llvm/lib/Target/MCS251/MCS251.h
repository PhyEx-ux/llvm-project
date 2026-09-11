//===-- MCS251.h - Top-level MCS-251 backend interface ----------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251_H
#define LLVM_LIB_TARGET_MCS251_MCS251_H

#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class MachineFunctionPass;
class MCS251TargetMachine;
class PassRegistry;

FunctionPass *createMCS251ISelDag(MCS251TargetMachine &TM,
                                  CodeGenOptLevel OptLevel);

// Post-layout branch relaxation (assessment E1): rewrites rel8 branches the
// final block order pushed out of range into equivalent ejmp-based forms.
MachineFunctionPass *createMCS251BranchRelaxationPass();

void initializeMCS251AsmPrinterPass(PassRegistry &);
void initializeMCS251BranchRelaxationPass(PassRegistry &);
void initializeMCS251DAGToDAGISelLegacyPass(PassRegistry &);
void initializeMCS251LoweringPrepLegacyPass(PassRegistry &);
} // namespace llvm

#endif
