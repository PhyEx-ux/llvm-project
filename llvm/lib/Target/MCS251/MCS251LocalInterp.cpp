//===-- MCS251LocalInterp.cpp - Read-only -O0 IR interpretation -----------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "MCS251LocalInterp.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"

using namespace llvm;
using namespace llvm::MCS251;

// RC-6-A: Escape analysis for allocas. An alloca's address "escapes" if it is
// passed to a call/invoke, stored to memory, or used as an operand of any
// instruction other than a load-from or store-to the alloca itself. Once the
// address escapes, an external function may mutate the alloca's contents, so
// loads from it cannot be treated as constants even if there is a single
// constant store.
bool MCS251::allocaAddressEscapes(const AllocaInst *AI) {
  for (const User *U : AI->users()) {
    // A load that reads through the alloca pointer is fine.
    if (isa<LoadInst>(U))
      continue;
    // A store *to* the alloca (alloca is the pointer operand) is fine -- the
    // stored value may or may not be constant, but the address itself has not
    // escaped.
    if (auto *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getPointerOperand() == AI)
        continue;
      // The alloca address is the *value* being stored -- it escapes.
      return true;
    }
    // Any other use (call argument, bitcast, GEP, etc.) means the address
    // escaped.
    return true;
  }
  return false;
}

// RC-7 (P1-1/P1-2): Unified safety gate for alloca constant propagation. A
// stored constant may replace a load from the same alloca only when ALL of
// the following hold:
//   1. No volatile or atomic load/store touches the alloca: a volatile
//      access must remain in the program, and an external agent may also
//      change the observable value between the store and the load; an
//      atomic access carries ordering semantics that propagation drops.
//   2. Every store writes the same type/width: a narrower store (e.g. an i8
//      store into an i32 slot) partially overwrites the slot, so the
//      older wide constant is no longer intact.
//   3. The address does not escape (RC-6-A).
// (The store-must-dominate-load condition is checked by the caller via
// getProvenLoadConstant, which has the load available.)
static bool isAllocaSafeForConstantPropagation(const AllocaInst *AI) {
  if (allocaAddressEscapes(AI))
    return false;
  const StoreInst *FirstStore = nullptr;
  for (const User *U : AI->users()) {
    if (auto *LI = dyn_cast<LoadInst>(U)) {
      if (LI->isVolatile() || LI->isAtomic())
        return false;
      continue;
    }
    // Non-load/store users were already rejected by allocaAddressEscapes.
    auto *SI = dyn_cast<StoreInst>(U);
    if (!SI)
      continue;
    if (SI->isVolatile() || SI->isAtomic())
      return false;
    if (FirstStore && FirstStore->getValueOperand()->getType() !=
                          SI->getValueOperand()->getType())
      return false;
    if (!FirstStore)
      FirstStore = SI;
  }
  return true;
}

// RC-7 (P1-3): An alloca is a pure write-only scratch slot only when every
// user writes THROUGH the alloca pointer: no user reads it (load) and the
// address never escapes. A store whose *value operand* is the alloca
// address (store-of-address, e.g. `saved = &x`) escapes the alloca; a plain
// isa<StoreInst> user check misclassifies it as an ordinary write, which
// let DCE delete the writer computation while the escaped address kept
// pointing at the never-stored slot.
bool MCS251::allocaIsOnlyWritten(const AllocaInst *AI) {
  if (allocaAddressEscapes(AI))
    return false;
  for (const User *U : AI->users())
    if (isa<LoadInst>(U))
      return false;
  return true;
}

// P12-1: whole-function "slot contents are unobservable" proof. Unlike
// allocaIsOnlyWritten this also rejects volatile/atomic stores: the stored
// value of a volatile store is itself observable (the access, including what
// it writes, must remain exactly as written), so substituting it would
// change observable behavior. Every non-store user -- load, call, GEP,
// cast, PHI/select of the address, a store OF the address -- keeps the slot
// live.
bool MCS251::allocaIsNeverRead(const AllocaInst *AI) {
  for (const User *U : AI->users()) {
    const auto *SI = dyn_cast<StoreInst>(U);
    if (!SI || SI->getPointerOperand() != AI || SI->isVolatile() ||
        SI->isAtomic())
      return false; // reader, escape or observable access
  }
  return true;
}

// P12-1/P12-2: whole-function parked-constant proof. Mirrors
// getProvenLoadConstant but answers at the slot level: the one store must
// dominate every load from the slot (the query has no single load at hand,
// so all readers must be covered), which in particular holds for the -O0
// entry-block parking shape. The DominatorTree is only built after the
// cheap rejections, and never mutates the function.
Constant *MCS251::getProvenSlotConstant(const AllocaInst *AI) {
  if (!isAllocaSafeForConstantPropagation(AI))
    return nullptr;
  const StoreInst *SingleStore = nullptr;
  for (const User *U : AI->users()) {
    auto *SI = dyn_cast<StoreInst>(U);
    if (!SI)
      continue;
    if (!isa<Constant>(SI->getValueOperand()))
      return nullptr; // slot holds a computed value, not a provable constant
    if (SingleStore)
      return nullptr; // more than one store: value not provably constant
    SingleStore = SI;
  }
  if (!SingleStore)
    return nullptr;
  DominatorTree DT(*const_cast<Function *>(AI->getFunction()));
  for (const User *U : AI->users()) {
    auto *LI = dyn_cast<LoadInst>(U);
    if (LI && !DT.dominates(SingleStore, LI))
      return nullptr; // a path where the store has not run reads undef
  }
  return const_cast<Constant *>(cast<Constant>(SingleStore->getValueOperand()));
}

// RC-7: Shared "is this load provably reading a known constant" query.
// Returns the constant when every safety condition holds:
//   - the alloca passes isAllocaSafeForConstantPropagation (no escape, no
//     volatile/atomic access, no mixed-type stores), and
//   - there is exactly one store of the loaded type, it stores a Constant,
//     and it dominates the load.  On a path where the store has not yet
//     executed the load would read undef, which is not "provably the stored
//     constant", so a non-dominating store disqualifies propagation.
// When no DominatorTree is available the check falls back to a conservative
// same-block textual order test.
Constant *MCS251::getProvenLoadConstant(const AllocaInst *AI,
                                        const LoadInst *LI,
                                        const DominatorTree *DT) {
  if (!isAllocaSafeForConstantPropagation(AI))
    return nullptr;
  const StoreInst *SingleStore = nullptr;
  for (const User *U : AI->users()) {
    auto *SI = dyn_cast<StoreInst>(U);
    if (!SI)
      continue;
    // Cannot happen after the mixed-type gate; kept defensive.
    if (SI->getValueOperand()->getType() != LI->getType())
      continue;
    if (!isa<Constant>(SI->getValueOperand()))
      return nullptr;
    if (SingleStore)
      return nullptr; // More than one store -- value not provably constant.
    SingleStore = SI;
  }
  if (!SingleStore)
    return nullptr;
  if (DT) {
    if (!DT->dominates(SingleStore, LI))
      return nullptr;
  } else if (SingleStore->getParent() != LI->getParent() ||
             !SingleStore->comesBefore(LI)) {
    return nullptr;
  }
  // getValueOperand() on a const StoreInst yields a const Value*.
  return const_cast<Constant *>(cast<Constant>(SingleStore->getValueOperand()));
}

bool MCS251::isWideOrFloatType(const Type *Ty) {
  return Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isIntegerTy(64);
}

// RC-6-B: Only pure arithmetic/cast instructions with no side effects are
// removal candidates. Calls, loads, stores and anything that may write memory
// or trap are never removed.
bool MCS251::isPureArithmetic(const Instruction &I) {
  if (isa<CallBase>(I))
    return false;
  return isa<BinaryOperator>(I) || isa<CastInst>(I) || isa<UnaryOperator>(I);
}

//===----------------------------------------------------------------------===//
// LocalInterp
//===----------------------------------------------------------------------===//

Constant *LocalInterp::evaluate(const Value *V) const {
  if (const auto *C = dyn_cast<Constant>(V))
    return const_cast<Constant *>(C);
  const auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return nullptr; // arguments, basic blocks, metadata operands
  auto It = ConstMemo.find(I);
  if (It != ConstMemo.end())
    return It->second;
  // SSA values cannot depend on themselves without a PHI, and PHIs are never
  // evaluated below; the guard only protects against malformed input.
  if (!ConstInProgress.insert(I).second)
    return nullptr;

  Constant *Result = nullptr;
  if (const auto *LI = dyn_cast<LoadInst>(I)) {
    // A volatile or atomic load must stay in the program and may observe a
    // value changed by an external agent; it is never a constant.
    if (!LI->isVolatile() && !LI->isAtomic()) {
      if (const auto *AI = dyn_cast<AllocaInst>(LI->getPointerOperand())) {
        Result = getProvenLoadConstant(AI, LI, DT);
      } else if (Constant *Ptr = evaluate(LI->getPointerOperand())) {
        // A load through a constant pointer (e.g. a `constant` global) is a
        // pure query on the initializer; nothing is written back.
        Result = ConstantFoldLoadFromConstPtr(Ptr, LI->getType(), DL);
      }
    }
  } else if (!I->getType()->isVoidTy() && !isa<PHINode>(I) &&
             !isa<AllocaInst>(I) && !I->isTerminator() &&
             !I->mayHaveSideEffects() && !I->mayReadFromMemory()) {
    bool Evaluable = true;
    if (const auto *CB = dyn_cast<CallBase>(I)) {
      // Only intrinsics the IR constant folder knows to be pure functions of
      // their arguments (e.g. llvm.sqrt.f32) are interpretable.
      const Function *F = CB->getCalledFunction();
      Evaluable = F && canConstantFoldCallTo(CB, F);
    }
    if (Evaluable) {
      SmallVector<Constant *, 8> Ops;
      for (const Use &Op : I->operands()) {
        Constant *C = evaluate(Op.get());
        if (!C) {
          Evaluable = false;
          break;
        }
        Ops.push_back(C);
      }
      if (Evaluable)
        Result = ConstantFoldInstOperands(I, Ops, DL);
    }
  }

  ConstInProgress.erase(I);
  ConstMemo[I] = Result;
  return Result;
}

bool LocalInterp::allOperandsConstant(const Instruction &I) const {
  for (const Use &Op : I.operands())
    if (!evaluate(Op.get()))
      return false;
  return true;
}

// RC-6 / RC-6-B: an instruction is effectively dead when its result can only
// reach (a) non-volatile stores into write-only allocas or (b) pure wide/
// float computations that are themselves effectively dead -- exactly the set
// MCS251LoweringPrep removes (its removal is the fixpoint of this recursion).
// A side effect keeps the instruction alive regardless of result use: calls,
// invokes and volatile accesses are never dead.
bool LocalInterp::isEffectivelyDead(const Instruction &I) const {
  if (isa<CallBase>(I))
    return false;
  if (const auto *LI = dyn_cast<LoadInst>(&I))
    if (LI->isVolatile())
      return false;
  if (const auto *SI = dyn_cast<StoreInst>(&I))
    if (SI->isVolatile())
      return false;

  auto It = DeadMemo.find(&I);
  if (It != DeadMemo.end())
    return It->second;
  // A use cycle can only run through a PHI, which is not a removal candidate
  // (it is neither a binary/unary operator nor a cast) and so terminates the
  // walk below; the guard is defensive.
  if (!DeadInProgress.insert(&I).second)
    return false;

  bool Dead = true;
  for (const User *U : I.users()) {
    if (const auto *SI = dyn_cast<StoreInst>(U)) {
      // RC-6-B: a volatile store keeps its value operand alive. RC-7 (P1-3):
      // the alloca must be a pure write-only scratch slot; a store OF the
      // alloca address escapes it and makes the stored value observable.
      const auto *AI = dyn_cast<AllocaInst>(SI->getPointerOperand());
      if (SI->getValueOperand() == &I && !SI->isVolatile() && AI &&
          allocaIsOnlyWritten(AI))
        continue;
      Dead = false;
      break;
    }
    const auto *UI = dyn_cast<Instruction>(U);
    if (UI && isWideOrFloatType(UI->getType()) && isPureArithmetic(*UI) &&
        isEffectivelyDead(*UI))
      continue;
    Dead = false;
    break;
  }

  DeadInProgress.erase(&I);
  DeadMemo[&I] = Dead;
  return Dead;
}
