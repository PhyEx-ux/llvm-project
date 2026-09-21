//===-- MCS251GlobalInit.cpp - shared global-initializer decision ---------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WP4 shared component: the bodies moved verbatim from the AsmPrinter's
// private static members (see MCS251GlobalInit.h). The only new logic is the
// PointerLeafKind classification wrapper, which distinguishes the rejection
// reasons without changing any accept/reject verdict: every case that used to
// return false keeps returning false, every supported form keeps its exact
// base symbol and addend.
//
//===----------------------------------------------------------------------===//

#include "MCS251GlobalInit.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"

using namespace llvm;
using namespace llvm::MCS251::GlobalInit;

// The supported pointer leaf: exactly &global (a GlobalVariable or Function
// in one of the placed storage address spaces 0/3/4) with one folded
// constant addend -- either the legacy ConstantExpr Add form or, since
// legal IR expresses pointer-plus-constant as a GEP, a getelementptr whose
// base is a GlobalVariable and whose indices all fold to constants under
// the DataLayout -- or a null pointer (a fully defined zero image that
// serializes no capability). GEP null, non-constant indices, inttoptr,
// ptrtoint, addrspacecast and all other expression algebra stay rejected,
// in the style of the existing conservative support checks.
PointerLeafKind llvm::MCS251::GlobalInit::classifyPointerLeaf(
    const Constant *C, const DataLayout &DL, const GlobalValue *&Base,
    int64_t &Addend) {
  Base = nullptr;
  Addend = 0;
  if (isa<ConstantPointerNull>(C))
    return DL.getTypeStoreSize(C->getType()) == 4 ? PointerLeafKind::OK
                                                  : PointerLeafKind::Width;
  const Value *BaseV = nullptr;
  if (isa<GlobalValue>(C)) {
    BaseV = C;
  } else if (const auto *CE = dyn_cast<ConstantExpr>(C);
             CE && CE->getOpcode() == Instruction::Add &&
             CE->getNumOperands() == 2) {
    const Value *L = CE->getOperand(0);
    const Value *R = CE->getOperand(1);
    if (isa<GlobalValue>(L) && isa<ConstantInt>(R)) {
      BaseV = L;
      Addend = cast<ConstantInt>(R)->getSExtValue();
    } else if (isa<ConstantInt>(L) && isa<GlobalValue>(R)) {
      BaseV = R;
      Addend = cast<ConstantInt>(L)->getSExtValue();
    } else {
      return PointerLeafKind::Other;
    }
  } else if (const auto *GEP = dyn_cast<GEPOperator>(C)) {
    // X3-R4: the sanctioned form of "&global + constant". Only a GEP
    // directly over a GlobalVariable with fully constant indices folds; a
    // GEP over null, over any cast (which could launder an address space),
    // or with a non-constant index is not an object identity and keeps
    // the rejection. The folded offset must stay inside the 24-bit
    // effective-address discipline (any base plus such an addend that
    // would resolve into [0,0xffffff] needs an addend in
    // [-0xffffff,+0xffffff]; anything wider can never link legally, and
    // the linker's pointer-interval gate re-validates the final value).
    BaseV = GEP->getPointerOperand();
    if (!isa<GlobalVariable>(BaseV))
      return PointerLeafKind::GEPOverNull;
    const unsigned AS = GEP->getPointerAddressSpace();
    APInt Offset(DL.getIndexSizeInBits(AS), 0);
    if (!GEP->accumulateConstantOffset(DL, Offset))
      return PointerLeafKind::NonConstantGEP; // a runtime pointer, not a leaf
    if (!Offset.isSignedIntN(32))
      return PointerLeafKind::OffCurve;
    if (Offset.sgt(0xffffff) || Offset.slt(int64_t(-0xffffff)))
      return PointerLeafKind::OffCurve; // outside the 24-bit discipline
    Addend = Offset.getSExtValue();
  } else {
    // inttoptr is the A2 boundary: an absolute address constant. ptrtoint,
    // addrspacecast, bitcast, select, GEP-null and everything else are the
    // generic "Other" algebra.
    if (const auto *CE = dyn_cast<ConstantExpr>(C)) {
      if (CE->getOpcode() == Instruction::IntToPtr)
        return PointerLeafKind::AbsoluteAddress;
    }
    return PointerLeafKind::Other;
  }
  if (!BaseV)
    return PointerLeafKind::Other;
  if (auto *F = dyn_cast<Function>(BaseV)) {
    Base = F;
  } else if (auto *GV = dyn_cast<GlobalVariable>(BaseV)) {
    unsigned AS = GV->getAddressSpace();
    if (AS != 0 && AS != 3 && AS != 4)
      return PointerLeafKind::UnsupportedBase; // no placed storage class
    Base = GV;
  } else {
    return PointerLeafKind::Other;
  }
  // The 24-bit relocation channel needs the full 32/8 pointer container.
  if (DL.getTypeStoreSize(C->getType()) != 4)
    return PointerLeafKind::Width;
  return PointerLeafKind::OK;
}

bool llvm::MCS251::GlobalInit::isSupportedPointerLeaf(const Constant *C,
                                                      const DataLayout &DL,
                                                      const GlobalValue *&Base,
                                                      int64_t &Addend) {
  return classifyPointerLeaf(C, DL, Base, Addend) == PointerLeafKind::OK;
}

bool llvm::MCS251::GlobalInit::isSupportedMutableType(Type *Ty,
                                                      const DataLayout &DL) {
  if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32))
    return true;
  if (auto *PT = dyn_cast<PointerType>(Ty))
    // X3: pointer leaves are supported in 4-byte containers only -- the
    // initializer channel writes a big-endian 32-bit container whose low 24
    // bits are the canonical address (zero most-significant byte at
    // container offset 0, 3-byte R_MCS251_24 field at offsets 1..3; the
    // 32/8 pointer ABI; the 16-bit contracts keep rejecting).
    return DL.getTypeStoreSize(PT) == 4;
  if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    return AT->getNumElements() &&
           isSupportedMutableType(AT->getElementType(), DL);
  }
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->isOpaque() || ST->getNumElements() == 0)
      return false;
    return llvm::all_of(ST->elements(), [&](Type *E) {
      return isSupportedMutableType(E, DL);
    });
  }
  return false;
}

bool llvm::MCS251::GlobalInit::isSupportedMutableInitializer(const Constant *C,
                                                            const DataLayout
                                                                &DL) {
  Type *Ty = C->getType();
  if (!isSupportedMutableType(Ty, DL))
    return false;
  if (isa<ConstantAggregateZero>(C))
    return true;
  if (isa<ConstantInt>(C))
    return true;
  if (isa<PointerType>(Ty)) {
    const GlobalValue *Base;
    int64_t Addend;
    return isSupportedPointerLeaf(C, DL, Base, Addend);
  }
  if (!isa<ArrayType>(Ty) && !isa<StructType>(Ty))
    return false;
  unsigned Elements = Ty->isArrayTy()
                          ? cast<ArrayType>(Ty)->getNumElements()
                          : cast<StructType>(Ty)->getNumElements();
  for (unsigned I = 0; I != Elements; ++I) {
    const Constant *Element = C->getAggregateElement(I);
    if (!Element || !isSupportedMutableInitializer(Element, DL))
      return false;
  }
  return true;
}

// Read-only CSEG data type whitelist: i8/i16/i32 scalars and (possibly
// nested) arrays of them. X3 extends the accepted leaf set with pointer
// initializers (&global leaves through the 24-bit relocation channel) and,
// for CODE-space objects, the ROM zero image. The AS4-AGGREGATE slice
// additionally accepts struct aggregates on the AllowStructs (AS4) call site
// only: the gate keeps the isSupportedMutableInitializer shape -- recursive
// type qualification first, then the value-shape dispatch.
bool llvm::MCS251::GlobalInit::isSupportedROType(Type *Ty, const DataLayout &DL,
                                                 bool AllowStructs) {
  if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32))
    return true;
  if (auto *PT = dyn_cast<PointerType>(Ty))
    return DL.getTypeStoreSize(PT) == 4;
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return AT->getNumElements() &&
           isSupportedROType(AT->getElementType(), DL, AllowStructs);
  if (AllowStructs)
    if (auto *ST = dyn_cast<StructType>(Ty)) {
      if (ST->isOpaque() || ST->getNumElements() == 0)
        return false;
      return llvm::all_of(ST->elements(), [&](Type *E) {
        return isSupportedROType(E, DL, AllowStructs);
      });
    }
  return false;
}

bool llvm::MCS251::GlobalInit::isSupportedROInitializer(
    const Constant *C, const DataLayout &DL, bool AllowZeroImage,
    bool AllowStructs) {
  Type *Ty = C->getType();
  // Two halves, in the isSupportedMutableInitializer order (design
  // AS4-AGGREGATE-INIT-DESIGN 6A.2): type qualification first, then the
  // value dispatch -- the zero image is never exempt from the type check.
  if (!isSupportedROType(Ty, DL, AllowStructs))
    return false;
  if (isa<ConstantInt>(C))
    return true;
  if (isa<ConstantAggregateZero>(C))
    return AllowZeroImage;
  if (isa<PointerType>(Ty)) {
    const GlobalValue *Base;
    int64_t Addend;
    return isSupportedPointerLeaf(C, DL, Base, Addend);
  }
  if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    if (!isa<ConstantDataArray>(C) && !isa<ConstantArray>(C))
      return false;
    for (unsigned I = 0; I != AT->getNumElements(); ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (!Element ||
          !isSupportedROInitializer(Element, DL, AllowZeroImage, AllowStructs))
        return false;
    }
    return true;
  }
  if (AllowStructs)
    if (auto *ST = dyn_cast<StructType>(Ty)) {
      for (unsigned I = 0; I != ST->getNumElements(); ++I) {
        const Constant *Element = C->getAggregateElement(I);
        if (!Element ||
            !isSupportedROInitializer(Element, DL, AllowZeroImage, AllowStructs))
          return false;
      }
      return true;
    }
  return false;
}

// v1 representability of a global INITIALIZER (X3). Admits aggregates of the
// emittable leaves: integers, zero images, and pointer leaves of the exact
// form above. The operand trees of arbitrary ConstantExprs are deliberately
// NOT walked for leaf admission: a cast or arithmetic expression that merely
// ends in a plain pointer type must not launder an AS3/AS4 capability into a
// v1 object (the isr-object ESCAPE cases pin exactly this). The callers use
// this walk only as a widening superset of hasV1ObjectCompatibleConstant,
// never as a replacement; it keeps its own seen-set so a constant rejected by
// the first walk can never look "already verified" here.
bool llvm::MCS251::GlobalInit::hasV1PlacementInitializer(
    const Constant *C, const DataLayout &DL) {
  SmallPtrSet<const Constant *, 32> Seen;
  return hasV1PlacementInitializerImpl(C, DL, Seen);
}

bool llvm::MCS251::GlobalInit::hasV1PlacementInitializerImpl(
    const Constant *C, const DataLayout &DL,
    SmallPtrSetImpl<const Constant *> &Seen) {
  if (!Seen.insert(C).second)
    return true;
  if (isa<ConstantAggregateZero>(C) || isa<ConstantInt>(C))
    return true;
  Type *Ty = C->getType();
  if (isa<PointerType>(Ty)) {
    const GlobalValue *Base;
    int64_t Addend;
    return isSupportedPointerLeaf(C, DL, Base, Addend);
  }
  unsigned Elements = 0;
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    Elements = AT->getNumElements();
  else if (auto *ST = dyn_cast<StructType>(Ty))
    Elements = ST->isOpaque() ? 0 : ST->getNumElements();
  else
    return false; // casts, ptrtoint, undef, ...: not an emittable leaf
  if (!Elements)
    return false;
  for (unsigned I = 0; I != Elements; ++I) {
    const Constant *Element = C->getAggregateElement(I);
    if (!Element || !hasV1PlacementInitializerImpl(Element, DL, Seen))
      return false;
  }
  return true;
}
