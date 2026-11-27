//===-- MCS251AtomicExpand.cpp - Single-core EA atomic lowering -----------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "MCS251AtomicExpand.h"
#include "MCS251.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsMCS251.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

static int toCABIOrdering(AtomicOrdering AO) {
  switch (AO) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
    return 0; // __ATOMIC_RELAXED
  case AtomicOrdering::Acquire:
    return 2; // __ATOMIC_ACQUIRE
  case AtomicOrdering::Release:
    return 3; // __ATOMIC_RELEASE
  case AtomicOrdering::AcquireRelease:
    return 4; // __ATOMIC_ACQ_REL
  case AtomicOrdering::SequentiallyConsistent:
    return 5; // __ATOMIC_SEQ_CST
  }
  return 5;
}

static Value *computeRMWValue(IRBuilder<> &Builder, AtomicRMWInst::BinOp Op,
                              Value *Old, Value *Val) {
  Type *Ty = Old->getType();
  switch (Op) {
  case AtomicRMWInst::Xchg:
    return Val;
  case AtomicRMWInst::Add:
    return Builder.CreateAdd(Old, Val);
  case AtomicRMWInst::Sub:
    return Builder.CreateSub(Old, Val);
  case AtomicRMWInst::And:
    return Builder.CreateAnd(Old, Val);
  case AtomicRMWInst::Nand:
    return Builder.CreateNot(Builder.CreateAnd(Old, Val));
  case AtomicRMWInst::Or:
    return Builder.CreateOr(Old, Val);
  case AtomicRMWInst::Xor:
    return Builder.CreateXor(Old, Val);
  case AtomicRMWInst::Max:
    return Builder.CreateSelect(Builder.CreateICmpSGT(Old, Val), Old, Val);
  case AtomicRMWInst::Min:
    return Builder.CreateSelect(Builder.CreateICmpSLT(Old, Val), Old, Val);
  case AtomicRMWInst::UMax:
    return Builder.CreateSelect(Builder.CreateICmpUGT(Old, Val), Old, Val);
  case AtomicRMWInst::UMin:
    return Builder.CreateSelect(Builder.CreateICmpULT(Old, Val), Old, Val);
  case AtomicRMWInst::UIncWrap: {
    Value *Zero = ConstantInt::get(Ty, 0);
    Value *One = ConstantInt::get(Ty, 1);
    return Builder.CreateSelect(Builder.CreateICmpUGE(Old, Val), Zero,
                                Builder.CreateAdd(Old, One));
  }
  case AtomicRMWInst::UDecWrap: {
    Value *Zero = ConstantInt::get(Ty, 0);
    Value *One = ConstantInt::get(Ty, 1);
    Value *Wrap = Builder.CreateOr(Builder.CreateICmpEQ(Old, Zero),
                                   Builder.CreateICmpUGT(Old, Val));
    return Builder.CreateSelect(Wrap, Val, Builder.CreateSub(Old, One));
  }
  case AtomicRMWInst::USubCond:
    return Builder.CreateSelect(Builder.CreateICmpUGE(Old, Val),
                                Builder.CreateSub(Old, Val), Old);
  case AtomicRMWInst::USubSat:
    return Builder.CreateSelect(Builder.CreateICmpUGE(Old, Val),
                                Builder.CreateSub(Old, Val),
                                ConstantInt::get(Ty, 0));
  case AtomicRMWInst::FAdd:
    return Builder.CreateFAdd(Old, Val);
  case AtomicRMWInst::FSub:
    return Builder.CreateFSub(Old, Val);
  default:
    reportFatalUsageError("MCS251: unsupported atomicrmw operation");
  }
}

static bool expandFunctionAtomics(Function &F) {
  if (F.isDeclaration())
    return false;

  Module &M = *F.getParent();
  LLVMContext &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  SmallVector<Instruction *, 16> Worklist;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (isa<FenceInst>(&I) || isa<AtomicRMWInst>(&I) ||
          isa<AtomicCmpXchgInst>(&I)) {
        Worklist.push_back(&I);
      } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
        if (LI->isAtomic())
          Worklist.push_back(&I);
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        if (SI->isAtomic())
          Worklist.push_back(&I);
      }
    }
  }
  if (Worklist.empty())
    return false;

  Function *FenceFn =
      Intrinsic::getOrInsertDeclaration(&M, Intrinsic::mcs251_atomic_fence);
  Function *SaveDisableFn =
      Intrinsic::getOrInsertDeclaration(&M, Intrinsic::mcs251_ea_save_disable);
  Function *RestoreFn =
      Intrinsic::getOrInsertDeclaration(&M, Intrinsic::mcs251_ea_restore);

  Type *I1Ty = Type::getInt1Ty(Ctx);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *VoidTy = Type::getVoidTy(Ctx);
  PointerType *AS0PtrTy = PointerType::get(Ctx, 0);

  auto MakeEntryAlloca = [&](Type *Ty, const Twine &Name) -> AllocaInst * {
    IRBuilder<> EntryB(&F.getEntryBlock(), F.getEntryBlock().getFirstInsertionPt());
    return EntryB.CreateAlloca(Ty, nullptr, Name);
  };

  auto CheckAS0ForLibcall = [&](Value *Ptr, StringRef OpName) {
    if (Ptr->getType()->getPointerAddressSpace() != 0)
      reportFatalUsageError(
          "MCS251: " + Twine(OpName) +
          " wider than 4 bytes requires an ordinary AS0 pointer");
  };

  bool Changed = false;
  for (Instruction *I : Worklist) {
    // A store/load may already have been folded into a generic libcall.
    if (!I->getParent())
      continue;

    if (auto *FI = dyn_cast<FenceInst>(I)) {
      IRBuilder<> Builder(FI);
      Builder.CreateCall(FenceFn);
      FI->eraseFromParent();
      Changed = true;
      continue;
    }

    if (auto *LI = dyn_cast<LoadInst>(I)) {
      Type *Ty = LI->getType();
      uint64_t Size = DL.getTypeStoreSize(Ty);
      IRBuilder<> Builder(LI);
      if (Size == 1) {
        Builder.CreateCall(FenceFn);
        auto *NL = Builder.CreateAlignedLoad(Ty, LI->getPointerOperand(),
                                             LI->getAlign(), /*isVolatile=*/true);
        Builder.CreateCall(FenceFn);
        LI->replaceAllUsesWith(NL);
        LI->eraseFromParent();
      } else if (Size == 2 || Size == 4) {
        Value *EA = Builder.CreateCall(SaveDisableFn);
        auto *NL = Builder.CreateAlignedLoad(Ty, LI->getPointerOperand(),
                                             LI->getAlign(), /*isVolatile=*/true);
        Builder.CreateCall(RestoreFn, {EA});
        LI->replaceAllUsesWith(NL);
        LI->eraseFromParent();
      } else {
        CheckAS0ForLibcall(LI->getPointerOperand(), "atomic load");
        FunctionCallee Fn = M.getOrInsertFunction(
            "__atomic_load",
            FunctionType::get(VoidTy, {I32Ty, AS0PtrTy, AS0PtrTy, I32Ty},
                              false));
        Value *SizeV = ConstantInt::get(I32Ty, Size);
        Value *OrdV = ConstantInt::get(I32Ty, toCABIOrdering(LI->getOrdering()));
        // Common Clang pattern for >4B _Atomic struct load: load atomic iN
        // immediately followed by a sole store into a local AS0 destination.
        if (LI->hasOneUse()) {
          if (auto *NextSI = dyn_cast_or_null<StoreInst>(LI->getNextNode())) {
            if (NextSI->getValueOperand() == LI && !NextSI->isAtomic() &&
                !NextSI->isVolatile() &&
                NextSI->getPointerAddressSpace() == 0) {
              Builder.CreateCall(
                  Fn, {SizeV, LI->getPointerOperand(),
                       NextSI->getPointerOperand(), OrdV});
              NextSI->eraseFromParent();
              LI->eraseFromParent();
              Changed = true;
              continue;
            }
          }
        }
        AllocaInst *Tmp = MakeEntryAlloca(Ty, "atomic.load.tmp");
        Builder.CreateCall(Fn, {SizeV, LI->getPointerOperand(), Tmp, OrdV});
        Value *Res = Builder.CreateAlignedLoad(Ty, Tmp, DL.getABITypeAlign(Ty));
        LI->replaceAllUsesWith(Res);
        LI->eraseFromParent();
      }
      Changed = true;
      continue;
    }

    if (auto *SI = dyn_cast<StoreInst>(I)) {
      Value *Val = SI->getValueOperand();
      Type *Ty = Val->getType();
      uint64_t Size = DL.getTypeStoreSize(Ty);
      IRBuilder<> Builder(SI);
      if (Size == 1) {
        Builder.CreateCall(FenceFn);
        Builder.CreateAlignedStore(Val, SI->getPointerOperand(),
                                   SI->getAlign(), /*isVolatile=*/true);
        Builder.CreateCall(FenceFn);
        SI->eraseFromParent();
      } else if (Size == 2 || Size == 4) {
        Value *EA = Builder.CreateCall(SaveDisableFn);
        Builder.CreateAlignedStore(Val, SI->getPointerOperand(),
                                   SI->getAlign(), /*isVolatile=*/true);
        Builder.CreateCall(RestoreFn, {EA});
        SI->eraseFromParent();
      } else {
        CheckAS0ForLibcall(SI->getPointerOperand(), "atomic store");
        FunctionCallee Fn = M.getOrInsertFunction(
            "__atomic_store",
            FunctionType::get(VoidTy, {I32Ty, AS0PtrTy, AS0PtrTy, I32Ty},
                              false));
        Value *SizeV = ConstantInt::get(I32Ty, Size);
        Value *OrdV = ConstantInt::get(I32Ty, toCABIOrdering(SI->getOrdering()));
        if (auto *PrevLI = dyn_cast<LoadInst>(Val)) {
          if (PrevLI->hasOneUse() && PrevLI->getNextNode() == SI &&
              !PrevLI->isAtomic() && !PrevLI->isVolatile() &&
              PrevLI->getPointerAddressSpace() == 0) {
            Builder.CreateCall(
                Fn, {SizeV, SI->getPointerOperand(),
                     PrevLI->getPointerOperand(), OrdV});
            SI->eraseFromParent();
            PrevLI->eraseFromParent();
            Changed = true;
            continue;
          }
        }
        AllocaInst *Tmp = MakeEntryAlloca(Ty, "atomic.store.tmp");
        Builder.CreateAlignedStore(Val, Tmp, DL.getABITypeAlign(Ty));
        Builder.CreateCall(Fn, {SizeV, SI->getPointerOperand(), Tmp, OrdV});
        SI->eraseFromParent();
      }
      Changed = true;
      continue;
    }

    if (auto *RMW = dyn_cast<AtomicRMWInst>(I)) {
      Value *Ptr = RMW->getPointerOperand();
      Value *Val = RMW->getValOperand();
      Type *Ty = Val->getType();
      uint64_t Size = DL.getTypeStoreSize(Ty);
      IRBuilder<> Builder(RMW);
      if (Size == 1 || Size == 2 || Size == 4) {
        Value *EA = Builder.CreateCall(SaveDisableFn);
        Value *Old =
            Builder.CreateAlignedLoad(Ty, Ptr, RMW->getAlign(),
                                      /*isVolatile=*/true);
        Value *New = computeRMWValue(Builder, RMW->getOperation(), Old, Val);
        Builder.CreateAlignedStore(New, Ptr, RMW->getAlign(),
                                   /*isVolatile=*/true);
        Builder.CreateCall(RestoreFn, {EA});
        RMW->replaceAllUsesWith(Old);
        RMW->eraseFromParent();
        Changed = true;
      } else if (RMW->getOperation() == AtomicRMWInst::Xchg) {
        CheckAS0ForLibcall(Ptr, "atomic exchange");
        FunctionCallee Fn = M.getOrInsertFunction(
            "__atomic_exchange",
            FunctionType::get(VoidTy,
                              {I32Ty, AS0PtrTy, AS0PtrTy, AS0PtrTy, I32Ty},
                              false));
        AllocaInst *ValTmp = MakeEntryAlloca(Ty, "atomic.xchg.val");
        AllocaInst *RetTmp = MakeEntryAlloca(Ty, "atomic.xchg.ret");
        Builder.CreateAlignedStore(Val, ValTmp, DL.getABITypeAlign(Ty));
        Builder.CreateCall(
            Fn, {ConstantInt::get(I32Ty, Size), Ptr, ValTmp, RetTmp,
                 ConstantInt::get(I32Ty, toCABIOrdering(RMW->getOrdering()))});
        Value *Res =
            Builder.CreateAlignedLoad(Ty, RetTmp, DL.getABITypeAlign(Ty));
        RMW->replaceAllUsesWith(Res);
        RMW->eraseFromParent();
        Changed = true;
      } else {
        reportFatalUsageError(
            "MCS251: atomic read-modify-write wider than 4 bytes requires "
            "unsupported wide-integer arithmetic");
      }
      continue;
    }

    if (auto *CAS = dyn_cast<AtomicCmpXchgInst>(I)) {
      Value *Ptr = CAS->getPointerOperand();
      Value *CmpVal = CAS->getCompareOperand();
      Value *NewVal = CAS->getNewValOperand();
      Type *Ty = CmpVal->getType();
      uint64_t Size = DL.getTypeStoreSize(Ty);
      if (Size == 1 || Size == 2 || Size == 4) {
        BasicBlock *BB = CAS->getParent();
        BasicBlock *DoneBB =
            BB->splitBasicBlock(std::next(CAS->getIterator()), "cmpxchg.done");
        BB->getTerminator()->eraseFromParent();
        BasicBlock *StoreBB = BasicBlock::Create(Ctx, "cmpxchg.store", &F, DoneBB);

        IRBuilder<> Builder(CAS);
        Value *EA = Builder.CreateCall(SaveDisableFn);
        Value *Old =
            Builder.CreateAlignedLoad(Ty, Ptr, CAS->getAlign(),
                                      /*isVolatile=*/true);
        Value *Eq = Builder.CreateICmpEQ(Old, CmpVal, "cmpxchg.eq");
        Builder.CreateCondBr(Eq, StoreBB, DoneBB);

        IRBuilder<> StoreB(StoreBB);
        StoreB.CreateAlignedStore(NewVal, Ptr, CAS->getAlign(),
                                  /*isVolatile=*/true);
        StoreB.CreateBr(DoneBB);

        IRBuilder<> DoneB(DoneBB, DoneBB->begin());
        DoneB.CreateCall(RestoreFn, {EA});
        Value *Pair = PoisonValue::get(CAS->getType());
        Pair = DoneB.CreateInsertValue(Pair, Old, 0);
        Pair = DoneB.CreateInsertValue(Pair, Eq, 1);
        CAS->replaceAllUsesWith(Pair);
        CAS->eraseFromParent();
        Changed = true;
      } else {
        CheckAS0ForLibcall(Ptr, "atomic compare-exchange");
        IRBuilder<> Builder(CAS);
        FunctionCallee Fn = M.getOrInsertFunction(
            "__atomic_compare_exchange",
            FunctionType::get(
                I1Ty,
                {I32Ty, AS0PtrTy, AS0PtrTy, AS0PtrTy, I32Ty, I32Ty}, false));
        AllocaInst *ExpTmp = MakeEntryAlloca(Ty, "atomic.cas.exp");
        AllocaInst *DesTmp = MakeEntryAlloca(Ty, "atomic.cas.des");
        Builder.CreateAlignedStore(CmpVal, ExpTmp, DL.getABITypeAlign(Ty));
        Builder.CreateAlignedStore(NewVal, DesTmp, DL.getABITypeAlign(Ty));
        Value *Ok = Builder.CreateCall(
            Fn,
            {ConstantInt::get(I32Ty, Size), Ptr, ExpTmp, DesTmp,
             ConstantInt::get(I32Ty, toCABIOrdering(CAS->getSuccessOrdering())),
             ConstantInt::get(I32Ty,
                              toCABIOrdering(CAS->getFailureOrdering()))});
        Value *Loaded =
            Builder.CreateAlignedLoad(Ty, ExpTmp, DL.getABITypeAlign(Ty));
        Value *Pair = PoisonValue::get(CAS->getType());
        Pair = Builder.CreateInsertValue(Pair, Loaded, 0);
        Pair = Builder.CreateInsertValue(Pair, Ok, 1);
        CAS->replaceAllUsesWith(Pair);
        CAS->eraseFromParent();
        Changed = true;
      }
      continue;
    }
  }

  return Changed;
}

static bool expandModuleAtomics(Module &M) {
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::mcs251)
    return false;
  bool Changed = false;
  for (Function &F : M)
    Changed |= expandFunctionAtomics(F);
  return Changed;
}

class MCS251AtomicExpandLegacy final : public ModulePass {
public:
  static char ID;
  MCS251AtomicExpandLegacy() : ModulePass(ID) {}

  StringRef getPassName() const override {
    return "MCS-251 single-core EA atomic expansion";
  }

  bool runOnModule(Module &M) override { return expandModuleAtomics(M); }
};

} // namespace

char MCS251AtomicExpandLegacy::ID = 0;

INITIALIZE_PASS(MCS251AtomicExpandLegacy, "mcs251-atomic-expand",
                "MCS-251 single-core EA atomic expansion", false, false)

ModulePass *llvm::MCS251::createMCS251AtomicExpandPass() {
  return new MCS251AtomicExpandLegacy();
}

PreservedAnalyses
llvm::MCS251::MCS251AtomicExpandPass::run(Module &M, ModuleAnalysisManager &) {
  return expandModuleAtomics(M) ? PreservedAnalyses::none()
                                : PreservedAnalyses::all();
}
