//===-- MCS251LoweringPrep.cpp - target-owned -O0 IR preparation -----------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Port of the old lib/CodeGen MCS251ContractVerifier "constantPropAndFold"
// helper, now an ordinary target IR pass:
//
//   1. Alloca constant propagation: a load from an alloca with exactly one
//      dominating constant store, no escape, no volatile/atomic access and no
//      mixed-width stores is replaced by that constant (RC-7 safety gate,
//      shared with the read-only check through MCS251LocalInterp).
//   2. Constant folding: every instruction whose operands became constants
//      is folded away (RC-4), so no illegal-type operation reaches the DAG
//      with foldable operands.
//   3. Targeted DCE for dead wide/float instructions (RC-6-B): pure
//      arithmetic whose result only flows into never-read allocas.
//
// Functions marked optnone are skipped entirely: their IR must remain
// byte-identical through this pass. The read-only MCS251ContractCheck uses
// MCS251LocalInterp to reach the same foldable/dead verdicts without
// rewriting anything, and instruction selection substitutes locally for the
// two optnone shapes that can survive (dead wide results and wide results
// provably parked on constants).
//
//===----------------------------------------------------------------------===//

#include "MCS251LoweringPrep.h"
#include "MCS251.h"
#include "MCS251LocalInterp.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"

using namespace llvm;
using namespace llvm::MCS251;

namespace {

class MCS251LoweringPrepLegacy final : public ModulePass {
public:
  static char ID;
  MCS251LoweringPrepLegacy() : ModulePass(ID) {
    initializeMCS251LoweringPrepLegacyPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "MCS-251 lowering preparation (fold + targeted DCE)";
  }

  bool runOnModule(Module &M) override {
    bool Changed = false;
    for (Function &F : M)
      Changed |= runOnFunction(F);
    return Changed;
  }

  bool runOnFunction(Function &F);
};

} // namespace

char MCS251LoweringPrepLegacy::ID = 0;
INITIALIZE_PASS(MCS251LoweringPrepLegacy, "mcs251-lowering-prep",
                "MCS-251 lowering preparation (fold + targeted DCE)", false,
                false)

ModulePass *llvm::MCS251::createMCS251LoweringPrepPass() {
  return new MCS251LoweringPrepLegacy();
}

// One function of the old constantPropAndFold, minus the "intentionally
// ignores optnone" behavior: optnone functions are untouched here. Verdicts
// come from the shared MCS251LocalInterp oracle instead of local copies of
// the escape/dead analyses.
bool MCS251LoweringPrepLegacy::runOnFunction(Function &F) {
  if (F.isDeclaration())
    return false;
  // P1-2 ruling: optnone functions pass through byte-identical. Their
  // foldable/dead wide operations are judged read-only by the contract check
  // and substituted locally by instruction selection, never by an IR rewrite.
  if (F.hasOptNone())
    return false;

  const DataLayout &DL = F.getDataLayout();
  bool AnyChanged = false;

  // RC-6/RC-7: propagate single-constant-store alloca loads. Dominance
  // information comes from a DominatorTree built per function; erased loads
  // never change block layout or the relative order of the remaining
  // instructions, so the tree stays valid across the rewrites below.
  {
    DominatorTree DT(F);
    bool Changed = false;
    for (BasicBlock &BB : F) {
      for (auto It = BB.begin(); It != BB.end();) {
        Instruction &I = *It++;
        auto *LI = dyn_cast<LoadInst>(&I);
        if (!LI)
          continue;
        auto *AI = dyn_cast<AllocaInst>(LI->getPointerOperand());
        if (!AI)
          continue;
        // Unified alloca safety gate plus the dominating
        // single-constant-store condition: skip the whole alloca when it has
        // any volatile/atomic access, any mixed-width store, an escaped
        // address, or a store that does not dominate the load.
        if (Constant *C = getProvenLoadConstant(AI, LI, &DT)) {
          LI->replaceAllUsesWith(C);
          LI->eraseFromParent();
          Changed = true;
        }
      }
    }
    AnyChanged |= Changed;
  }

  // RC-4: Always run a constant-folding sweep, even if no alloca loads were
  // propagated. Instructions with all-constant operands that were not folded
  // by the IR optimizer must be folded here so they do not reach the DAG as
  // illegal-type operations. This catches both alloca-propagated constants
  // and direct constant intrinsic calls (e.g. llvm.sqrt.f32(2.0)) that have
  // no alloca load dependency.
  for (BasicBlock &BB : F) {
    for (auto It = BB.begin(); It != BB.end();) {
      Instruction &I = *It++;
      if (I.getNumOperands() == 0)
        continue;
      if (auto *C = ConstantFoldInstruction(&I, DL)) {
        I.replaceAllUsesWith(C);
        I.eraseFromParent();
        AnyChanged = true;
      }
    }
  }

  // RC-6-B: Targeted DCE for dead wide/float instructions. At -O0, clang
  // stores dead i64/f32/f64 computations to allocas that are never loaded.
  // Remove the store and the computation so they don't reach the DAG where
  // ReplaceNodeResults would reject them. The deadness verdict comes from the
  // shared LocalInterp::isEffectivelyDead recursion (memoized per round),
  // which never treats calls, volatile accesses or any instruction with side
  // effects as dead.
  bool DCEChanged = true;
  while (DCEChanged) {
    DCEChanged = false;
    DominatorTree DT(F);
    LocalInterp LI(DL, &DT);
    SmallVector<Instruction *, 8> ToErase;
    SmallVector<StoreInst *, 8> StoresToErase;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (!isWideOrFloatType(I.getType()))
          continue;
        if (!isPureArithmetic(I))
          continue;
        if (!LI.isEffectivelyDead(I))
          continue;
        // Collect the sink stores (non-volatile stores of I into write-only
        // allocas) so they are erased together with the computation; plain
        // use-empty dead instructions have no sink stores to collect.
        for (User *U : I.users()) {
          auto *SI = dyn_cast<StoreInst>(U);
          if (!SI || SI->isVolatile() || SI->getValueOperand() != &I)
            continue;
          auto *AI = dyn_cast<AllocaInst>(SI->getPointerOperand());
          if (AI && allocaIsOnlyWritten(AI))
            StoresToErase.push_back(SI);
        }
        ToErase.push_back(&I);
      }
    }
    if (!ToErase.empty()) {
      for (StoreInst *SI : StoresToErase)
        SI->eraseFromParent();
      // Erase use-free instructions first and repeat: isEffectivelyDead can
      // collect whole def/use chains in one round, and a definition must not
      // be erased while a (not yet erased) collected user still references
      // it. The restricted use graph is acyclic (SSA), so this terminates.
      while (!ToErase.empty()) {
        SmallVector<Instruction *, 8> Remaining;
        for (Instruction *I : ToErase) {
          if (I->use_empty())
            I->eraseFromParent();
          else
            Remaining.push_back(I);
        }
        if (Remaining.size() == ToErase.size())
          llvm_unreachable("effectively-dead set has an internal use cycle");
        ToErase = std::move(Remaining);
      }
      DCEChanged = true;
      AnyChanged = true;
    }
  }
  return AnyChanged;
}

PreservedAnalyses llvm::MCS251::MCS251LoweringPrepPass::run(
    Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    MCS251LoweringPrepLegacy LP;
    Changed |= LP.runOnFunction(F);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
