//===-- MCS251LoweringPrep.h - target-owned -O0 IR preparation --*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// The IR-mutating half of the old lib/CodeGen MCS251ContractVerifier
// ("constantPropAndFold"): alloca constant propagation, constant folding and
// the targeted wide/float DCE that keep unsupported i64/f32/f64 shapes away
// from instruction selection. It is the ONLY component allowed to rewrite IR
// for this purpose, it skips functions marked optnone (their IR must reach
// instruction selection untouched; the read-only MCS251ContractCheck judges
// them through MCS251LocalInterp instead), and its dead/foldable verdicts
// come from that same shared oracle so the two can never drift apart.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251LOWERINGPREP_H
#define LLVM_LIB_TARGET_MCS251_MCS251LOWERINGPREP_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

namespace MCS251 {

/// Legacy module pass used by the target's TargetPassConfig.
ModulePass *createMCS251LoweringPrepPass();

/// New pass manager wrapper (pipeline name "mcs251-lowering-prep").
class MCS251LoweringPrepPass
    : public RequiredPassInfoMixin<MCS251LoweringPrepPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace MCS251
} // namespace llvm

#endif
