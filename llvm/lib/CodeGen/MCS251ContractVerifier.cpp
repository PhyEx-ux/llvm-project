//===-- MCS251ContractVerifier.cpp - MCS-251 IR contract checks -----------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MCS251ContractVerifier.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/MCS251TargetParser.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>

using namespace llvm;
using namespace llvm::MCS251;

namespace {

// RC-6-A: Escape analysis for allocas. An alloca's address "escapes" if it is
// passed to a call/invoke, stored to memory, or used as an operand of any
// instruction other than a load-from or store-to the alloca itself. Once the
// address escapes, an external function may mutate the alloca's contents, so
// loads from it cannot be treated as constants even if there is a single
// constant store.
static bool allocaAddressEscapes(const AllocaInst *AI) {
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
static bool
isAllocaSafeForConstantPropagation(const AllocaInst *AI) {
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
static bool allocaIsOnlyWritten(const AllocaInst *AI) {
  if (allocaAddressEscapes(AI))
    return false;
  for (const User *U : AI->users())
    if (isa<LoadInst>(U))
      return false;
  return true;
}

// RC-7: Shared "is this load provably reading a known constant" query used
// by both the constant propagator and the foldable-operand check, so the two
// can never disagree. Returns the constant when every safety condition
// holds:
//   - the alloca passes isAllocaSafeForConstantPropagation (no escape, no
//     volatile/atomic access, no mixed-type stores), and
//   - there is exactly one store of the loaded type, it stores a Constant,
//     and it dominates the load.  On a path where the store has not yet
//     executed the load would read undef, which is not "provably the stored
//     constant", so a non-dominating store disqualifies propagation.
// When no DominatorTree is available the check falls back to a conservative
// same-block textual order test.
static Constant *getProvenLoadConstant(const AllocaInst *AI,
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

// RC-6: At -O0, clang stores local constants to allocas and reloads them.
// The IR optimizer (mem2reg + instcombine) is blocked by optnone, so these
// loads reach the verifier as non-constant operands, causing foldable i64/f32
// operations to be falsely rejected. This function propagates
// single-constant-store alloca loads to their constant values and folds the
// resulting all-constant instructions. It intentionally ignores optnone
// because it only makes provably-true replacements.
static bool constantPropAndFold(Module &M) {
  const DataLayout &DL = M.getDataLayout();
  bool AnyChanged = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    // RC-7: dominance information for getProvenLoadConstant.  Erased loads
    // never change block layout or the relative order of the remaining
    // instructions, so the tree stays valid across the rewrites below.
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
        // RC-7 (P1-1/P1-2): unified alloca safety gate plus the dominating
        // single-constant-store condition. Skip the whole alloca when it has
        // any volatile/atomic access, any mixed-width store, an escaped
        // address, or a store that does not dominate the load -- in all
        // these cases the load is not provably the stored constant.
        if (Constant *C = getProvenLoadConstant(AI, LI, &DT)) {
          LI->replaceAllUsesWith(C);
          LI->eraseFromParent();
          Changed = true;
        }
      }
    }
    // RC-4: Always run a constant-folding pass, even if no alloca loads were
    // propagated. Instructions with all-constant operands that were not folded
    // by the IR optimizer (blocked by optnone) must be folded here so they do
    // not reach the DAG as illegal-type operations. This catches both alloca-
    // propagated constants and direct constant intrinsic calls (e.g.
    // llvm.sqrt.f32(2.0)) that have no alloca load dependency.
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
    // ReplaceNodeResults would reject them. Only target instructions with
    // wide/float result types to avoid breaking frame/allocation tests.
    //
    // SAFETY: DCE only removes pure arithmetic instructions (add, sub, mul,
    // zext, etc.) whose results are dead or only stored to never-loaded
    // allocas. It never removes:
    //  - Call/Invoke instructions (they may have side effects)
    //  - Volatile loads or stores
    //  - Any instruction with side effects
    auto IsWideOrFloatType = [](Type *Ty) {
      return Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isIntegerTy(64);
    };
    // Only pure arithmetic/cast instructions with no side effects are DCE
    // candidates. Calls, volatile accesses, and anything that may write memory
    // or trap are never removed.
    auto IsPureArithmetic = [](const Instruction &I) {
      if (isa<CallBase>(I))
        return false;
      if (auto *LI = dyn_cast<LoadInst>(&I))
        if (LI->isVolatile())
          return false;
      if (auto *SI = dyn_cast<StoreInst>(&I))
        if (SI->isVolatile())
          return false;
      // Only binary/unary operators and casts are safe to DCE.
      return isa<BinaryOperator>(I) || isa<CastInst>(I) ||
             isa<UnaryOperator>(I);
    };
    bool DCEChanged = true;
    bool DCEDidChange = false;
    while (DCEChanged) {
      DCEChanged = false;
      // Collect instructions to remove first, then erase after iteration to
      // avoid iterator invalidation (the next instruction may be a dead store
      // that gets erased along with the current instruction).
      SmallVector<Instruction *, 8> ToErase;
      SmallVector<StoreInst *, 8> StoresToErase;
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (!IsWideOrFloatType(I.getType()))
            continue;
          // RC-6-B: Never DCE calls, volatile accesses, or any non-pure
          // arithmetic instruction.
          if (!IsPureArithmetic(I))
            continue;
          // Check if all uses are stores to never-loaded allocas.
          if (I.use_empty()) {
            ToErase.push_back(&I);
            continue;
          }
          bool OnlyDeadStores = true;
          SmallVector<StoreInst *, 4> DeadStores;
          for (User *U : I.users()) {
            auto *SI = dyn_cast<StoreInst>(U);
            if (!SI || SI->getValueOperand() != &I) {
              OnlyDeadStores = false;
              break;
            }
            // RC-6-B: Never erase volatile stores.
            if (SI->isVolatile()) {
              OnlyDeadStores = false;
              break;
            }
            auto *AI = dyn_cast<AllocaInst>(SI->getPointerOperand());
            if (!AI) {
              OnlyDeadStores = false;
              break;
            }
            // RC-7 (P1-3): the alloca must be a pure write-only scratch
            // slot. A store OF the alloca address (e.g. `saved = &x`)
            // escapes it and makes this store observable, so the writer
            // cannot be DCE'd.
            if (!allocaIsOnlyWritten(AI)) {
              OnlyDeadStores = false;
              break;
            }
            DeadStores.push_back(SI);
          }
          if (OnlyDeadStores && !DeadStores.empty()) {
            StoresToErase.append(DeadStores);
            ToErase.push_back(&I);
          }
        }
      }
      if (!ToErase.empty()) {
        for (StoreInst *SI : StoresToErase)
          SI->eraseFromParent();
        for (Instruction *I : ToErase)
          I->eraseFromParent();
        DCEChanged = true;
        DCEDidChange = true;
      }
    }
    AnyChanged = AnyChanged || Changed || DCEDidChange;
  }
  return AnyChanged;
}

class MCS251ContractVerifierLegacy final : public ModulePass {
  const TargetOptions *Options;
  bool CheckArithmetic;

public:
  static char ID;
  explicit MCS251ContractVerifierLegacy(const TargetOptions *Options = nullptr,
                                        bool CheckArithmetic = true)
      : ModulePass(ID), Options(Options), CheckArithmetic(CheckArithmetic) {}

  StringRef getPassName() const override { return "MCS-251 contract verifier"; }
  bool runOnModule(Module &M) override {
    bool Changed = false;
    if (CheckArithmetic)
      Changed = constantPropAndFold(M);
    if (Error Err = verifyModuleContract(M, Options, CheckArithmetic))
      report_fatal_error(Twine(toString(std::move(Err))));
    return Changed;
  }
};

char MCS251ContractVerifierLegacy::ID = 0;

static Error reject(StringRef What) {
  return createStringError(inconvertibleErrorCode(),
                           "MCS251 contract violation: %s",
                           What.str().c_str());
}

static Error checkType(const Type *Ty, SmallPtrSetImpl<const Type *> &Seen) {
  if (!Seen.insert(Ty).second)
    return Error::success();

  if (const auto *PT = dyn_cast<PointerType>(Ty)) {
    unsigned AS = PT->getAddressSpace();
    // AS5 is intentionally not allocated by the MCS-251 contract. The
    // whitelist also prevents DataLayout's p0 fallback from silently turning
    // an unassigned address space into a supported one.
    switch (AS) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 7:
    case 8:
    case 9:
      break;
    default:
      return reject((Twine("unsupported pointer address space ") +
                     Twine(AS))
                        .str());
    }
  }

  for (Type *Contained : Ty->subtypes())
    if (Error Err = checkType(Contained, Seen))
      return Err;
  return Error::success();
}

static Error checkValue(const Value &V, SmallPtrSetImpl<const Type *> &Seen) {
  return checkType(V.getType(), Seen);
}

// Calls through a named LLVM function lower as GlobalAddress nodes.  Validate
// their identity before ISel, rather than relying on pointer width or allowing
// a default-AS0 declaration to reach LowerCall in a v2 module.  In particular,
// a 32-bit AS0/AS3 function pointer must not be mistaken for the AS4 CODE
// container merely because it has the same scalar width.
static Error checkDirectCallTarget(const CallBase &CB, unsigned ProgramAS) {
  const Value *Callee = CB.getCalledOperand()->stripPointerCasts();
  const auto *GV = dyn_cast<GlobalValue>(Callee);
  if (!GV)
    return Error::success();
  if (!isa<Function>(GV))
    return reject("direct call target is not a function");
  if (GV->getAddressSpace() != ProgramAS)
    return reject("direct call target function is not in the configured CODE "
                  "address space");
  return Error::success();
}

// RC-3: Constant expressions (e.g. ptrtoint of a ptr addrspace(5)) can embed
// unsupported address spaces behind an innocent result type. checkType only
// inspects the type tree, so we must also walk the operand tree of Constants.
// RC-6-C: A visited set (ConstSeen) prevents infinite recursion on
// self-referential constants such as `@node = global ptr @node`.
static Error checkConstantTree(const Constant *C,
                               SmallPtrSetImpl<const Type *> &Seen,
                               SmallPtrSetImpl<const Constant *> &ConstSeen) {
  if (!ConstSeen.insert(C).second)
    return Error::success();
  if (Error Err = checkType(C->getType(), Seen))
    return Err;
  // RC-6-D: ConstantExpr GEP carries a source element type that is not visible
  // through the result type or the operand list. A GEP into ptr addrspace(5)
  // would otherwise pass the verifier while embedding an unsupported address
  // space in the source element type.
  if (const auto *GEP = dyn_cast<GEPOperator>(C))
    if (Error Err = checkType(GEP->getSourceElementType(), Seen))
      return Err;
  for (const Use &Op : C->operands()) {
    if (const auto *Child = dyn_cast<Constant>(Op.get()))
      if (Error Err = checkConstantTree(Child, Seen, ConstSeen))
        return Err;
  }
  return Error::success();
}

// DF0 task 2: f32/f64/i64 arithmetic, comparison and conversion operations are
// not yet implemented. Type legalization silently promotes i64 to two i32 ops
// and softens float to generic libcalls that have no target impl, producing
// wrong code or late cryptic errors. Catch them at the IR level instead.
// Pure zext/trunc/ptrtoint/inttoptr/bitcast round-trips through i64 are
// harmless (they fold to the narrow type) and are NOT rejected here; only
// operations that actually compute on the wide/float value are.
// DT carries the dominance information used by the shared alloca
// constant-propagation safety query (RC-7); it may be null, in which case
// getProvenLoadConstant falls back to a conservative same-block check.
static Error checkUnsupportedArithmetic(const Instruction &I,
                                        const DominatorTree *DT) {
  auto IsWideOrFloat = [](Type *Ty) {
    return Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isIntegerTy(64);
  };

  // RC-6: Skip operations that will never reach the backend:
  //  1. All-constant operations are folded by the SelectionDAG constant
  //     folder during legalization (e.g. udiv i64 100, 4 -> i64 25).
  //  2. Instructions with no users are dead code eliminated by the DAG.
  //  3. At -O0, clang stores local constants to allocas and reloads them, so
  //     the operand of e.g. udiv is a LoadInst, not a Constant. Track one
  //     level of load-from-constant-store to recognize these as foldable.
  //  4. Functions with optnone cannot be optimized, so the verifier must
  //     not reject operations the optimizer would have eliminated at -O2.
  //     The DAG-level ReplaceNodeResults handles the actual folding/rejection.
  auto IsFoldableConstant = [DT](const Value *V) {
    if (isa<Constant>(V))
      return true;
    if (auto *LI = dyn_cast<LoadInst>(V)) {
      if (auto *AI = dyn_cast<AllocaInst>(LI->getPointerOperand())) {
        // RC-7: same unified safety gate as constantPropAndFold -- volatile
        // or atomic accesses, mixed-width stores, escaped addresses and
        // non-dominating stores all make the loaded value non-constant.
        return getProvenLoadConstant(AI, LI, DT) != nullptr;
      }
    }
    return false;
  };
  auto AllOperandsFoldableConstant = [&](const Instruction &Inst) {
    for (const Use &Op : Inst.operands())
      if (!IsFoldableConstant(Op.get()))
        return false;
    return true;
  };
  // RC-6: Check if the instruction is effectively dead -- its result is only
  // used by stores to allocas that are never loaded. At -O0, clang stores
  // local variables to allocas even when they are dead, so use_empty() is
  // false but the value never reaches the backend.
  // RC-6-B: This must never return true for instructions with side effects
  // (calls, volatile accesses). A dead result does not mean the instruction
  // is dead -- the side effect must still execute.
  auto IsEffectivelyDead = [](const Instruction &Inst) {
    // Calls, invokes, and volatile accesses have side effects and are never
    // dead regardless of whether their result is used.
    if (isa<CallBase>(Inst))
      return false;
    if (auto *LI = dyn_cast<LoadInst>(&Inst))
      if (LI->isVolatile())
        return false;
    if (auto *SI = dyn_cast<StoreInst>(&Inst))
      if (SI->isVolatile())
        return false;
    if (Inst.use_empty())
      return true;
    for (const User *U : Inst.users()) {
      auto *SI = dyn_cast<StoreInst>(U);
      if (!SI || SI->getValueOperand() != &Inst)
        return false;
      // RC-6-B: A volatile store keeps its value operand alive.
      if (SI->isVolatile())
        return false;
      auto *AI = dyn_cast<AllocaInst>(SI->getPointerOperand());
      if (!AI)
        return false;
      // RC-7 (P1-3): the alloca must be a pure write-only scratch slot. A
      // store OF the alloca address (e.g. `saved = &x`) is a StoreInst user
      // but escapes the address, making the stored value observable through
      // the escaped pointer -- the writer cannot be treated as dead.
      if (!allocaIsOnlyWritten(AI))
        return false;
    }
    return true;
  };
  if (AllOperandsFoldableConstant(I) || IsEffectivelyDead(I))
    return Error::success();
  // RC-5: Vector types with wide/float elements are equally unsupported.
  auto ElementTypeIsWideOrFloat = [](Type *Ty) {
    if (auto *VecTy = dyn_cast<VectorType>(Ty))
      Ty = VecTy->getElementType();
    return Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isIntegerTy(64);
  };

  // FP-specific binary/unary ops.
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    Type *Ty = BO->getType();
    if (IsWideOrFloat(Ty) || ElementTypeIsWideOrFloat(Ty)) {
      switch (BO->getOpcode()) {
      default:
        break;
      case Instruction::FAdd:
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:
      case Instruction::FRem:
        return reject("f32/f64 arithmetic is not yet implemented; "
                      "soft-float runtime is not connected");
      case Instruction::Add:
      case Instruction::Sub:
      case Instruction::Mul:
      case Instruction::UDiv:
      case Instruction::SDiv:
      case Instruction::URem:
      case Instruction::SRem:
      case Instruction::Shl:
      case Instruction::LShr:
      case Instruction::AShr:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
        return reject("i64 integer arithmetic is not yet implemented; "
                      "wide-integer runtime is not connected");
      }
    }
  }

  // RC-5: fneg is a UnaryOperator. Float softening silently turns it into an
  // integer XOR, bypassing the DAG-level Custom lowering. Reject at the IR
  // level so the failure is loud.
  if (auto *UO = dyn_cast<UnaryOperator>(&I)) {
    Type *Ty = UO->getType();
    if ((IsWideOrFloat(Ty) || ElementTypeIsWideOrFloat(Ty)) &&
        UO->getOpcode() == Instruction::FNeg)
      return reject("f32/f64 fneg is not yet implemented; "
                    "soft-float runtime is not connected");
  }

  // RC-5: Intrinsic float math (fabs, sqrt, ceil, floor, etc.) is lowered
  // through float softening to generic libcalls that have no target impl,
  // producing wrong code or cryptic "no libcall" errors. Reject at the IR
  // level for any intrinsic that operates on a float/wide operand.
  // RC-7 (P2): float and wide-integer (i64) intrinsics are classified
  // separately so that e.g. llvm.uadd.sat.i64 reports the wide-integer
  // diagnostic instead of the misleading soft-float one.
  if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
    auto BaseType = [](Type *Ty) -> Type * {
      if (auto *VecTy = dyn_cast<VectorType>(Ty))
        return VecTy->getElementType();
      return Ty;
    };
    SmallVector<Type *, 8> OperandTypes;
    OperandTypes.push_back(II->getType());
    for (const Value *Op : II->args())
      OperandTypes.push_back(Op->getType());
    bool OperatesOnFloat = false;
    bool OperatesOnI64 = false;
    for (Type *Ty : OperandTypes) {
      Ty = BaseType(Ty);
      if (Ty->isFloatTy() || Ty->isDoubleTy())
        OperatesOnFloat = true;
      else if (Ty->isIntegerTy(64))
        OperatesOnI64 = true;
    }
    if (OperatesOnFloat || OperatesOnI64) {
      // Allow lifetime and debug intrinsics through; reject math intrinsics.
      switch (II->getIntrinsicID()) {
      default:
        if (OperatesOnFloat)
          return reject("f32/f64 intrinsic operation is not yet implemented; "
                        "soft-float runtime is not connected");
        return reject("i64 intrinsic operation is not yet implemented; "
                      "wide-integer runtime is not connected");
      case Intrinsic::lifetime_start:
      case Intrinsic::lifetime_end:
      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
      case Intrinsic::dbg_label:
      case Intrinsic::invariant_start:
      case Intrinsic::invariant_end:
      case Intrinsic::assume:
        break;
      }
    }
  }

  // Float comparisons.
  if (auto *Cmp = dyn_cast<CmpInst>(&I)) {
    if (Cmp->getPredicate() >= CmpInst::FCMP_FALSE &&
        Cmp->getPredicate() <= CmpInst::FCMP_TRUE)
      return reject("f32/f64 comparison is not yet implemented; "
                    "soft-float runtime is not connected");
    if (IsWideOrFloat(Cmp->getOperand(0)->getType()) ||
        ElementTypeIsWideOrFloat(Cmp->getOperand(0)->getType()))
      return reject("i64 comparison is not yet implemented; "
                    "wide-integer runtime is not connected");
  }

  // Conversions involving float or i64 results.
  if (auto *CI = dyn_cast<CastInst>(&I)) {
    Type *SrcTy = CI->getSrcTy();
    Type *DstTy = CI->getDestTy();
    bool SrcFloat = SrcTy->isFloatTy() || SrcTy->isDoubleTy();
    bool DstFloat = DstTy->isFloatTy() || DstTy->isDoubleTy();
    // FPToInt / IntToFP / FPExt / FPTrunc are all unsupported.
    if ((SrcFloat || DstFloat) && CI->getOpcode() != Instruction::BitCast)
      return reject("float conversion is not yet implemented; "
                    "soft-float runtime is not connected");
    // i64 results from zext/sext are fine if they only feed trunc/ptrtoint
    // round-trips; the arithmetic check above catches real i64 use.
  }

  return Error::success();
}
} // namespace

Error llvm::MCS251::verifyModuleContract(const Module &M,
                                         const TargetOptions *Options,
                                         bool CheckArithmetic) {
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::mcs251)
    return Error::success();

  const DataLayout &DL = M.getDataLayout();
  const unsigned ProgramAS = DL.getProgramAddressSpace();
  unsigned AS0Bits = DL.getPointerSizeInBits(0);
  if (AS0Bits != 16 && AS0Bits != 32)
    return reject("AS0 pointer width must be 16 or 32 bits");

  if (Options) {
    const MemoryContract &Contract = Options->MCS251Memory;
    StringRef ExpectedLayout = getCompatibilityDataLayout();
    if (Contract.isSpecified()) {
      if (!isValidMemoryContract(Contract))
        return reject("invalid numeric memory contract");
      auto Desc = getLayoutDesc(
          static_cast<ASLayoutVersion>(Contract.ASLayoutVersion),
          static_cast<AS0PointerBits>(Contract.AS0PointerBits));
      if (!Desc)
        return reject("numeric memory contract has no data layout");
      ExpectedLayout = Desc->DataLayout;
    }
    if (DL != DataLayout(ExpectedLayout))
      return reject("module data layout conflicts with the selected memory "
                    "contract");
  }

  SmallPtrSet<const Type *, 32> Seen;
  SmallPtrSet<const Constant *, 32> ConstSeen;
  // Cover declarations, definitions, aliases and ifuncs. Looking only at
  // globals() misses an unsupported pointer hidden in a declaration or an
  // alias operand, while looking only at instruction result types misses a
  // pointer payload loaded from otherwise ordinary AS0 storage.
  for (const GlobalValue &GV : M.global_values()) {
    if (Error Err = checkValue(GV, Seen))
      return Err;
    if (const auto *GVar = dyn_cast<GlobalVariable>(&GV)) {
      if (Error Err = checkType(GVar->getValueType(), Seen))
        return Err;
      if (GVar->hasInitializer())
        if (Error Err = checkConstantTree(GVar->getInitializer(), Seen,
                                          ConstSeen))
          return Err;
    }
    if (const auto *F = dyn_cast<Function>(&GV))
      if (Error Err = checkType(F->getFunctionType(), Seen))
        return Err;
    if (const auto *U = dyn_cast<User>(&GV))
      for (const Use &Operand : U->operands()) {
        if (Error Err = checkValue(*Operand, Seen))
          return Err;
        if (const auto *C = dyn_cast<Constant>(Operand.get()))
          if (Error Err = checkConstantTree(C, Seen, ConstSeen))
            return Err;
      }
  }

  for (const Function &F : M) {
    for (const Argument &A : F.args())
      if (Error Err = checkValue(A, Seen))
        return Err;
    // RC-7: dominance information for the shared alloca constant-propagation
    // safety query. DominatorTree never mutates the function; the const_cast
    // only satisfies the legacy non-const analysis constructor API.
    std::optional<DominatorTree> MaybeDT;
    if (CheckArithmetic && !F.isDeclaration())
      MaybeDT.emplace(const_cast<Function &>(F));
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        // Direct function calls must use the module's executable address
        // space.  This catches a stale AS0 declaration in v2 before the DAG
        // has an opportunity to coerce its scalar representation.
        if (const auto *CB = dyn_cast<CallBase>(&I))
          if (Error Err = checkDirectCallTarget(*CB, ProgramAS))
            return Err;
        // RC-6: arithmetic checks run post-optimization only, so that
        // foldable or dead i64/f32/f64 operations are not falsely rejected.
        // Structural checks (types, address spaces) always run.
        if (CheckArithmetic)
          if (Error Err = checkUnsupportedArithmetic(
                  I, MaybeDT ? &*MaybeDT : nullptr))
            return Err;
        if (Error Err = checkValue(I, Seen))
          return Err;
        // RC-3: alloca allocated type and GEP source element type carry
        // pointer payloads that are not visible through the instruction result
        // type. An alloca of ptr addrspace(5) or a GEP into ptr addrspace(5)
        // would otherwise pass the verifier while embedding an unsupported
        // address space in the allocated/element type.
        if (const auto *AI = dyn_cast<AllocaInst>(&I))
          if (Error Err = checkType(AI->getAllocatedType(), Seen))
            return Err;
        if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I))
          if (Error Err = checkType(GEP->getSourceElementType(), Seen))
            return Err;
        for (const Use &U : I.operands()) {
          if (Error Err = checkValue(*U, Seen))
            return Err;
          // RC-3: recurse into constant-expression operand trees so that
          // unsupported address spaces hidden behind ptrtoint/inttoptr
          // round-trips are caught.
          if (const auto *C = dyn_cast<Constant>(U.get()))
            if (Error Err = checkConstantTree(C, Seen, ConstSeen))
              return Err;
        }
      }
  }
  return Error::success();
}

ModulePass *llvm::MCS251::createMCS251ContractVerifierPass(
    const TargetOptions *Options, bool CheckArithmetic) {
  return new MCS251ContractVerifierLegacy(Options, CheckArithmetic);
}

PreservedAnalyses llvm::MCS251::MCS251ContractVerifierPass::run(
    Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  if (CheckArithmetic)
    Changed = constantPropAndFold(M);
  if (Error Err = verifyModuleContract(M, Options, CheckArithmetic))
    report_fatal_error(Twine(toString(std::move(Err))));
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
