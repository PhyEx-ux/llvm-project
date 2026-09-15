//===--- CGMCS251Bit.cpp - MCS-251 controlled bit lvalue lowering ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CodeGen for the MCS-251 controlled `bit`/`sbit` capability (BIT task
// breakdown BT04/BT05, rulings P01/P02/P09). This is the M2 lowering half of
// the bit/sbit campaign: the frontend (M1) established the `bit` scalar type,
// old-style `sbit` declarations, and __builtin_mcs251_bit_lvalue; here those
// controlled bit lvalues are lowered to the frozen target intrinsics
// (llvm.mcs251.bit.read/set/clear/toggle) instead of degrading to an ordinary
// i8 byte global/store/alloca.
//
// A controlled bit lvalue has no ordinary byte address. It is created by
// EmitMCS251ControlledBitLValue and recognized by LValue::isMCS251Bit(); the
// generic load/store paths route it here. The fixed-address forms (old-style
// `sbit` and __builtin_mcs251_bit_lvalue) carry a constant bit address 0..255.
//
// Contract: DIALECT-FRONTEND-DESIGN.md §7.5 (forced lowering table) and §7.6
// (effect model); BIT-TASK-BREAKDOWN.md §2.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsMCS251.h"
#include <optional>

using namespace clang;
using namespace CodeGen;

namespace {

/// The fixed bit address denoted by \p E, if \p E is a controlled MCS-251 fixed
/// bit reference (an old-style `sbit` declaration or a
/// __builtin_mcs251_bit_lvalue(ICE) call). Mirrors the Sema identity rule
/// (SemaMCS251.cpp getMCS251FixedBitAddress): parentheses, implicit casts and a
/// resolved _Generic/__builtin_choose_expr forward identity.
std::optional<llvm::APSInt> getMCS251FixedBitAddress(const Expr *E,
                                                     ASTContext &Ctx) {
  while (E) {
    if (isa<ParenExpr>(E) || isa<ImplicitCastExpr>(E)) {
      E = cast<Expr>(*E->child_begin());
      continue;
    }
    if (auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
      if (GSE->isResultDependent())
        return std::nullopt;
      E = GSE->getResultExpr();
      continue;
    }
    if (auto *CE = dyn_cast<ChooseExpr>(E)) {
      E = CE->getChosenSubExpr();
      continue;
    }
    break;
  }
  if (!E)
    return std::nullopt;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD)
      return std::nullopt;
    const auto *Attr = VD->getAttr<MCS251BitAddressAttr>();
    if (!Attr)
      return std::nullopt;
    return Attr->getAddress()->getIntegerConstantExpr(Ctx);
  }
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    const FunctionDecl *FD = CE->getDirectCallee();
    // Double-guarded by the target triple: target builtin enums all start at
    // Builtin::FirstTSBuiltin, so the bare ID comparison would also match a
    // foreign builtin on another target (X86's _AddressOfReturnAddress
    // numerically collides with __builtin_mcs251_bit_lvalue).
    if (!FD || !CodeGenFunction::isMCS251Target(Ctx) ||
        FD->getBuiltinID() != MCS251::BI__builtin_mcs251_bit_lvalue)
      return std::nullopt;
    if (CE->getNumArgs() != 1)
      return std::nullopt;
    return CE->getArg(0)->getIntegerConstantExpr(Ctx);
  }
  return std::nullopt;
}

} // namespace

LValue CodeGenFunction::EmitMCS251ControlledBitLValue(const Expr *E) {
  ASTContext &Ctx = getContext();
  std::optional<llvm::APSInt> Addr = getMCS251FixedBitAddress(E, Ctx);
  if (!Addr)
    return LValue();
  llvm::Value *AddrV = llvm::ConstantInt::get(Int32Ty, Addr->getZExtValue());
  return LValue::MakeMCS251Bit(AddrV, /*Symbolic=*/false,
                               Ctx.getVolatileType(Ctx.MCS251BitTy));
}

LValue CodeGenFunction::EmitMCS251PersistentBitLValue(const VarDecl *VD,
                                                      QualType Ty) {
  // P09 §2.6.2: a persistent/static `bit` object reference lowers to its
  // unique handle global as a Symbolic controlled-bit l-value. The declared
  // QualType is preserved as-is (the source keeps its own const/volatile
  // qualification); the fixed-reference implicit volatile does not apply.
  // The handle always exists: GetOrCreateMCS251BitGlobalVar diagnoses any
  // unsupported storage form and still returns a safely-shaped handle, so
  // codegen continues after the (build-failing) diagnostic.
  llvm::GlobalVariable *GV = CGM.GetOrCreateMCS251BitGlobalVar(VD);
  return LValue::MakeMCS251Bit(GV, /*Symbolic=*/true, Ty);
}

/// The DeclRefExpr denoted by \p E if \p E strips (parentheses, implicit
/// casts, resolved _Generic/__builtin_choose_expr -- the same transparent set
/// as getMCS251FixedBitAddress and the Sema identity rule) down to a
/// DeclRefExpr to a static-storage-duration bit object. ParmVarDecl/automatic
/// objects are the P-2 value slice and are not toggle identities here.
/// Returns the reference itself (not just the VarDecl) so callers can keep
/// its declared QualType without re-stripping with a narrower helper.
static const DeclRefExpr *getMCS251PersistentBitObjectRef(const Expr *E) {
  while (E) {
    if (isa<ParenExpr>(E) || isa<ImplicitCastExpr>(E)) {
      E = cast<Expr>(*E->child_begin());
      continue;
    }
    if (auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
      if (GSE->isResultDependent())
        return nullptr;
      E = GSE->getResultExpr();
      continue;
    }
    if (auto *CE = dyn_cast<ChooseExpr>(E)) {
      E = CE->getChosenSubExpr();
      continue;
    }
    break;
  }
  if (!E)
    return nullptr;
  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE)
    return nullptr;
  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD || !VD->getType().getUnqualifiedType()->isMCS251BitType())
    return nullptr;
  if (VD->getStorageDuration() != SD_Static ||
      VD->getTLSKind() != VarDecl::TLS_None)
    return nullptr;
  return DRE;
}

LValue CodeGenFunction::EmitMCS251ToggleBitLValue(const Expr *E) {
  // Fixed identity first (§2.3: an sbit also has bit type, and Fixed takes
  // priority over Object), then the persistent-object handle. The reference
  // may have been forwarded through parens/implicit casts/_Generic/choose;
  // the stripped DeclRefExpr's own QualType is the declared type to keep.
  if (LValue Fixed = EmitMCS251ControlledBitLValue(E); Fixed.isMCS251Bit())
    return Fixed;
  if (const DeclRefExpr *DRE = getMCS251PersistentBitObjectRef(E))
    return EmitMCS251PersistentBitLValue(cast<VarDecl>(DRE->getDecl()),
                                         DRE->getType());
  return LValue();
}

llvm::Value *CodeGenFunction::EmitMCS251BitAddressOperand(LValue LV) {
  assert(LV.isMCS251Bit() && "not an MCS-251 bit lvalue");
  llvm::Value *Addr = LV.getMCS251BitAddress();
  if (LV.isMCS251BitSymbolic()) {
    // P09 §2.6.4: the symbolic operand is the handle global itself (object
    // identity, always an AS0 i8 bit-object global). It is never narrowed to
    // an integer address and never ptrtoint'ed; the bit number is resolved
    // by the linker from the BITADDR8 relocation.
    assert(isa_and_nonnull<llvm::GlobalVariable>(Addr) &&
           "symbolic bit handle must be the bit-object global");
    return Addr;
  }
  if (Addr->getType() != Int32Ty)
    Addr =
        Builder.CreateIntCast(Addr, Int32Ty, /*isSigned=*/false, "bit.addr");
  return Addr;
}

/// The intrinsic family for a controlled-bit access: the fixed i32-ImmArg
/// family for a fixed bit address, the obj (handle) family for a symbolic
/// persistent bit object (P09 §1.1 / §2.6.5).
static llvm::Intrinsic::ID mcs251BitIntrinsic(llvm::Intrinsic::ID FixedID,
                                              LValue LV) {
  if (!LV.isMCS251BitSymbolic())
    return FixedID;
  switch (FixedID) {
  case llvm::Intrinsic::mcs251_bit_read:
    return llvm::Intrinsic::mcs251_bit_obj_read;
  case llvm::Intrinsic::mcs251_bit_set:
    return llvm::Intrinsic::mcs251_bit_obj_set;
  case llvm::Intrinsic::mcs251_bit_clear:
    return llvm::Intrinsic::mcs251_bit_obj_clear;
  case llvm::Intrinsic::mcs251_bit_toggle:
    return llvm::Intrinsic::mcs251_bit_obj_toggle;
  default:
    llvm_unreachable("not an MCS-251 bit intrinsic");
  }
}

RValue CodeGenFunction::EmitLoadOfMCS251BitLValue(LValue LV,
                                                  SourceLocation Loc) {
  llvm::Value *Addr = EmitMCS251BitAddressOperand(LV);
  llvm::Function *F = CGM.getIntrinsic(
      mcs251BitIntrinsic(llvm::Intrinsic::mcs251_bit_read, LV));
  llvm::Value *Bit = Builder.CreateCall(F, Addr);
  // The intrinsic returns i1; the source value is the MCS-251 `bit` scalar,
  // which is an i1 in expressions (the normalized i8 is only the ABI/object
  // representation, P01).
  llvm::Type *ValTy = ConvertType(LV.getType());
  if (Bit->getType() != ValTy)
    Bit = Builder.CreateZExt(Bit, ValTy, "bit.zext");
  return RValue::get(Bit);
}

void CodeGenFunction::EmitStoreThroughMCS251BitLValue(RValue Src, LValue Dst) {
  llvm::Value *Addr = EmitMCS251BitAddressOperand(Dst);
  llvm::Value *Val = Src.getScalarVal();
  // A source-language `bit` value is exactly 0 or 1 (Sema normalizes every
  // integer conversion through "nonzero -> 1"). A constant folds to a single
  // set/clear; any runtime value is evaluated once and written through a
  // branch (one target bit write), per §7.5. The value form deliberately does
  // not use MOV bit,C: it is not yet probe-verified, and the branch form has
  // no carry round-trip.
  if (auto *CI = dyn_cast<llvm::ConstantInt>(Val)) {
    bool One = !CI->isZero();
    llvm::Function *F = CGM.getIntrinsic(mcs251BitIntrinsic(
        One ? llvm::Intrinsic::mcs251_bit_set
            : llvm::Intrinsic::mcs251_bit_clear,
        Dst));
    Builder.CreateCall(F, Addr);
    return;
  }
  // Normalize a runtime value to i1 (nonzero -> 1) before branching, so a wide
  // or odd byte still writes exactly 0 or 1 (never bit0 only).
  llvm::Value *Bit =
      Val->getType()->isIntegerTy(1)
          ? Val
          : Builder.CreateICmpNE(
                Val, llvm::Constant::getNullValue(Val->getType()), "bit.nz");
  // The three blocks must be attached to the current function (a bare
  // createBasicBlock() leaves them parentless and the module verifier rejects
  // the dangling CFG). The condition above already sampled the RHS once; each
  // arm then performs exactly one bit write, per §7.5.
  llvm::Function *Fn = Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *SetBB = createBasicBlock("mcs251.bit.set", Fn);
  llvm::BasicBlock *ClrBB = createBasicBlock("mcs251.bit.clear", Fn);
  llvm::BasicBlock *ContBB = createBasicBlock("mcs251.bit.cont", Fn);
  Builder.CreateCondBr(Bit, SetBB, ClrBB);
  Builder.SetInsertPoint(SetBB);
  Builder.CreateCall(CGM.getIntrinsic(mcs251BitIntrinsic(
                         llvm::Intrinsic::mcs251_bit_set, Dst)),
                     Addr);
  Builder.CreateBr(ContBB);
  Builder.SetInsertPoint(ClrBB);
  Builder.CreateCall(CGM.getIntrinsic(mcs251BitIntrinsic(
                         llvm::Intrinsic::mcs251_bit_clear, Dst)),
                     Addr);
  Builder.CreateBr(ContBB);
  Builder.SetInsertPoint(ContBB);
}

void CodeGenFunction::EmitToggleMCS251BitLValue(LValue Dst) {
  llvm::Value *Addr = EmitMCS251BitAddressOperand(Dst);
  Builder.CreateCall(CGM.getIntrinsic(mcs251BitIntrinsic(
                         llvm::Intrinsic::mcs251_bit_toggle, Dst)),
                     Addr);
}

//===----------------------------------------------------------------------===//
// G7 S3: TFPU math builtins (__builtin_mcs251_tfpu_*)
//===----------------------------------------------------------------------===//
//
// The builtin is f32-typed at the source level; the IR intrinsic carries the
// IEEE-754 bit pattern as i32 (see the SIGNATURE NOTE in
// IntrinsicsMCS251.td -- this target softens every f32 and the generic
// float legalizer has no softening for intrinsic nodes, so the bitcast
// boundary lives HERE, in the frontend, where the value is still IR). The
// pattern is never transformed: bitcast in, hardware sequence, bitcast out.
//
// P-4 (G7 design §2.3): the intrinsic has no linkage symbol, so nothing
// leaks into !mcs251.signatures (emitTargetMetadata skips intrinsics in
// both its loops); the type-safety chain is Sema's exact-f32 gate above,
// CGM.getIntrinsic's declaration (never getOrInsertFunction), and llc's
// ContractCheck ID whitelist + exact signature check.
llvm::Value *CodeGenFunction::EmitMCS251BuiltinExpr(unsigned BuiltinID,
                                                    const CallExpr *E) {
  llvm::Intrinsic::ID IID;
  switch (BuiltinID) {
  default:
    llvm_unreachable("not an MCS251 TFPU builtin");
  case MCS251::BI__builtin_mcs251_tfpu_sin:
    IID = llvm::Intrinsic::mcs251_tfpu_sin; break;
  case MCS251::BI__builtin_mcs251_tfpu_cos:
    IID = llvm::Intrinsic::mcs251_tfpu_cos; break;
  case MCS251::BI__builtin_mcs251_tfpu_tan:
    IID = llvm::Intrinsic::mcs251_tfpu_tan; break;
  case MCS251::BI__builtin_mcs251_tfpu_atan:
    IID = llvm::Intrinsic::mcs251_tfpu_atan; break;
  case MCS251::BI__builtin_mcs251_tfpu_sqrt:
    IID = llvm::Intrinsic::mcs251_tfpu_sqrt; break;
  case MCS251::BI__builtin_mcs251_tfpu_add:
    IID = llvm::Intrinsic::mcs251_tfpu_add; break;
  case MCS251::BI__builtin_mcs251_tfpu_sub:
    IID = llvm::Intrinsic::mcs251_tfpu_sub; break;
  case MCS251::BI__builtin_mcs251_tfpu_mul:
    IID = llvm::Intrinsic::mcs251_tfpu_mul; break;
  case MCS251::BI__builtin_mcs251_tfpu_div:
    IID = llvm::Intrinsic::mcs251_tfpu_div; break;
  }
  // Sema's exact-f32 gate guarantees each argument is an f32 scalar.
  SmallVector<llvm::Value *, 2> Args;
  for (const Expr *Arg : E->arguments())
    Args.push_back(
        Builder.CreateBitCast(EmitScalarExpr(Arg), Int32Ty, "tfpu.bits"));
  llvm::Function *F = CGM.getIntrinsic(IID);
  llvm::CallInst *Call = Builder.CreateCall(F, Args);
  return Builder.CreateBitCast(Call, ConvertType(E->getType()), "tfpu.val");
}
