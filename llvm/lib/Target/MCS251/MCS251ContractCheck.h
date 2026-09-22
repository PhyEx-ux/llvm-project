//===-- MCS251ContractCheck.h - Read-only MCS-251 IR contract ---*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// The MCS-251 target contract is stricter than generic LLVM IR validity (an
// addrspace(5) pointer, an f64 operation or an unregistered interrupt entry
// are all legal IR but outside the contract). This pass only reads the
// module: a violation is a fatal error report, and nothing is ever written
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

/// WP4 default correct-failure capabilities only (atomics, multi-argument
/// indirect calls, weak definitions, module asm, computed goto,
/// absolute-address static pointer initialization, debug information).
/// Read-only. This is the WP4 subset of verifyModuleContract, exposed so the
/// AsmPrinter's doInitialization can run it ahead of the identity
/// classification and of the base class's module-asm emission: in the legacy
/// pass manager every doInitialization hook runs before any runOnModule, so
/// without this guard those two fail-closed gates preempt the WP4
/// diagnostics on the direct-llc/MIR entries. The pass itself still runs the
/// full contract (this subset included) at pipeline start.
Error verifyModuleCapabilities(const Module &M);

/// WP4: the clang backend (BackendUtil) has a DiagnosticsEngine and runs the
/// checks itself before/after the optimization pipeline, so a deliberate
/// capability rejection must not travel through the fatal error path there --
/// that would re-enter clang's in-process crash-recovery path and print a
/// bug-report request / stack dump even though the failure is expected. When
/// the check is deferred, the target's pipeline mounts no contract-check
/// passes and the backend owns both the structural (pre-optimization) and the
/// arithmetic (pre-codegen) verdicts. Used as an RAII scope by BackendUtil;
/// llc never sets it.
///
/// setContractCheckDeferred / isContractCheckDeferred /
/// verifyModuleContractMessage are DECLARED in MCS251TargetParser.h (included
/// above) and DEFINED in the always-linked TargetParser library, because
/// clang's BackendUtil references them unconditionally -- a build without the
/// MCS251 backend must still link. This library only registers the checker
/// that produces the verdict. Do not redefine them here.

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
