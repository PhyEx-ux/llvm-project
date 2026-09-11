//===-- MCS251ContractCheck.h - Read-only MCS-251 IR contract ---*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// The MCS-251 target contract is stricter than generic LLVM IR validity (an
// addrspace(5) pointer, an f64 operation or an unregistered interrupt entry
// are all legal IR but outside the contract). This pass only reads the
// module: a violation is a report_fatal_error, and nothing is ever written
// back to the IR. Any -O0 shape that needs folding to be judged is evaluated
// locally through MCS251LocalInterp instead.
//
// It is mounted unconditionally by MCS251TargetMachine (not tied to the
// generic -disable-verify flag) in two phases: structural checks before any
// optimization can erase evidence, and arithmetic checks just before ISel.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251CONTRACTCHECK_H
#define LLVM_LIB_TARGET_MCS251_MCS251CONTRACTCHECK_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/MCS251TargetParser.h"
#include <optional>

namespace llvm {
class Module;
class ModulePass;

namespace MCS251 {

/// Validate the module-level MCS-251 address-space, ISR, bit-object and
/// (when \p CheckArithmetic) wide/float arithmetic contract. When \p Contract
/// is present the module data layout must also match it. Read-only.
Error verifyModuleContract(const Module &M,
                           const std::optional<MemoryContract> &Contract,
                           bool CheckArithmetic);

/// Legacy module pass wrapper used by the target's TargetPassConfig.
ModulePass *
createMCS251ContractCheckPass(std::optional<MemoryContract> Contract,
                              bool CheckArithmetic);

/// New pass manager wrapper (pipeline name "mcs251-contract-check").
class MCS251ContractCheckPass
    : public RequiredPassInfoMixin<MCS251ContractCheckPass> {
  std::optional<MemoryContract> Contract;
  bool CheckArithmetic;

public:
  explicit MCS251ContractCheckPass(std::optional<MemoryContract> Contract,
                                   bool CheckArithmetic)
      : Contract(Contract), CheckArithmetic(CheckArithmetic) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // namespace MCS251
} // namespace llvm

#endif
