//===-- MCS251LocalInterp.h - Read-only -O0 IR interpretation ---*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Shared, side-effect-free queries over the IR shapes clang emits at -O0
// (constants parked in allocas and reloaded, dead wide computations stored
// to never-read slots). Both MCS251ContractCheck (read-only verification)
// and MCS251LoweringPrep (the only pass allowed to rewrite IR) consume this
// single oracle, so the verdict "this value is a compile-time constant" or
// "this instruction is effectively dead" can never drift between the two.
//
// Nothing in this header mutates IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251LOCALINTERP_H
#define LLVM_LIB_TARGET_MCS251_MCS251LOCALINTERP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace llvm {
class AllocaInst;
class Constant;
class DataLayout;
class DominatorTree;
class Function;
class Instruction;
class LoadInst;
class Type;
class Value;

namespace MCS251 {

/// True when the alloca's address is observable outside plain load-from /
/// store-to accesses (call argument, stored as a value, GEP, cast, ...).
bool allocaAddressEscapes(const AllocaInst *AI);

/// True when every user writes THROUGH the alloca pointer and nothing ever
/// reads it: the slot is a pure write-only scratch. A store OF the address
/// counts as an escape, not as a write.
bool allocaIsOnlyWritten(const AllocaInst *AI);

/// P12-1: True when every user of the alloca, over the WHOLE function, is a
/// plain (non-volatile, non-atomic) store THROUGH the slot pointer: no load
/// ever reads the slot, the address never escapes and no observable access
/// touches it, so any value stored there is unobservable. This is the
/// function-level form of the write-only proof; instruction selection uses
/// it because a SelectionDAG covers exactly one basic block and cannot see
/// readers living in successor blocks.
bool allocaIsNeverRead(const AllocaInst *AI);

/// P12-1/P12-2: The single constant provably parked in the alloca over the
/// whole function, or null. Requires the unified safety gate (no volatile or
/// atomic access, no mixed-width stores, no address escape), exactly one
/// store -- of a Constant -- and that store dominating EVERY load from the
/// slot. Instruction selection resolves parked-constant loads through this
/// query when the storing block is not part of the current DAG (an -O0
/// optnone function parks constants in the entry block and reloads them in
/// successors). The caller is responsible for honoring its own load's
/// extension semantics when widening the result.
Constant *getProvenSlotConstant(const AllocaInst *AI);

/// The constant a load provably reads, or null. Requires: no volatile or
/// atomic access, no mixed-width stores, no escape, exactly one store of a
/// Constant of the loaded type, and that store dominates the load. A null
/// DominatorTree falls back to a conservative same-block textual order test.
Constant *getProvenLoadConstant(const AllocaInst *AI, const LoadInst *LI,
                                const DominatorTree *DT);

/// True for the result types the target has no register class for.
bool isWideOrFloatType(const Type *Ty);

/// True for the instruction kinds that may be removed when their result is
/// dead: pure binary/unary operators and casts. Calls, volatile accesses and
/// anything that may write memory or trap are never candidates.
bool isPureArithmetic(const Instruction &I);

/// Per-function, memoized local interpreter. Construct one per function and
/// query it any number of times; it never writes back to the IR.
class LocalInterp {
  const DataLayout &DL;
  const DominatorTree *DT;
  // Memo caches only: the queries are logically read-only.
  // nullptr value == proven not a compile-time constant.
  mutable DenseMap<const Value *, Constant *> ConstMemo;
  mutable SmallPtrSet<const Value *, 8> ConstInProgress;
  mutable DenseMap<const Instruction *, bool> DeadMemo;
  mutable SmallPtrSet<const Instruction *, 8> DeadInProgress;

public:
  LocalInterp(const DataLayout &DL, const DominatorTree *DT)
      : DL(DL), DT(DT) {}

  /// The compile-time constant \p V evaluates to, or null. Recurses through
  /// pure instructions whose operands all evaluate to constants (folding
  /// them with ConstantFoldInstOperands, never rewriting IR), through loads
  /// of proven single-constant-store allocas, and through non-volatile loads
  /// of constant globals.
  Constant *evaluate(const Value *V) const;

  /// True when every operand of \p I evaluates to a constant.
  bool allOperandsConstant(const Instruction &I) const;

  /// True when \p I is a pure wide/float computation whose result can only
  /// flow into (a) non-volatile stores to write-only allocas or (b) other
  /// pure wide/float computations that are themselves effectively dead.
  /// Side-effecting instructions and volatile accesses are never dead.
  bool isEffectivelyDead(const Instruction &I) const;
};

} // namespace MCS251
} // namespace llvm

#endif
