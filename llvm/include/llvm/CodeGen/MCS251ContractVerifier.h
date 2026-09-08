//===-- MCS251ContractVerifier.h - MCS-251 IR contract checks -*- C++ -*-===//
#ifndef LLVM_CODEGEN_MCS251CONTRACTVERIFIER_H
#define LLVM_CODEGEN_MCS251CONTRACTVERIFIER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"

namespace llvm {
class Module;
class TargetOptions;

namespace MCS251 {
/// Validate the module-level MCS-251 address-space and memory contract.
/// This is deliberately independent of the generic LLVM verifier: a legal
/// LLVM addrspace(5) type can still be outside the MCS-251 target contract.
/// When CheckArithmetic is true, only the arithmetic checks (i64/f32/f64
/// operations) are run -- this is intended for post-optimization so that
/// foldable or dead wide/float operations are not falsely rejected.
Error verifyModuleContract(const Module &M, const TargetOptions *Options = nullptr,
                           bool CheckArithmetic = true);

/// Legacy module pass used at the target's module-level pre-ISel boundary.
ModulePass *
createMCS251ContractVerifierPass(const TargetOptions *Options = nullptr,
                                 bool CheckArithmetic = true);

class MCS251ContractVerifierPass
    : public RequiredPassInfoMixin<MCS251ContractVerifierPass> {
  const TargetOptions *Options;
  bool CheckArithmetic;

public:
  explicit MCS251ContractVerifierPass(const TargetOptions *Options = nullptr,
                                      bool CheckArithmetic = true)
      : Options(Options), CheckArithmetic(CheckArithmetic) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // namespace MCS251
} // namespace llvm

#endif
