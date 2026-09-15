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

// G7 S3 (G7-FLOAT-DESIGN-draft.md §2.4): post-RA expansion of the TFPU
// window pseudos into the trigger write (mov 0xED,#cmd) plus the fixed
// worst-case NOP-chain wait. Runs in addPreEmitPass BEFORE branch
// relaxation so the delay-chain sizes are part of its size arithmetic.
MachineFunctionPass *createMCS251TFPUExpandPass();

void initializeMCS251AsmPrinterPass(PassRegistry &);
void initializeMCS251BranchRelaxationPass(PassRegistry &);
void initializeMCS251DAGToDAGISelLegacyPass(PassRegistry &);
void initializeMCS251LoweringPrepLegacyPass(PassRegistry &);
void initializeMCS251TFPUExpandPass(PassRegistry &);
} // namespace llvm

#endif
