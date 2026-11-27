//===- MCS251.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MCS-251 target-specific CodeGen ABI classification (P09-BIT-CODEGEN-DESIGN
// §4.2, ruling P01). The only target-specific signature rule is the `bit`
// scalar: every bit parameter and a bit return value crosses the call
// boundary as an *explicit direct i8* carrying a normalized 0/1. This is the
// value ABI the backend already implements (first LLVM argument in the full
// DPL register, later arguments in the callee's original-position
// `_callee_PARM_n` static slots; i8 return in DPL).
//
// Everything that is not `bit` is classified by DefaultABIInfo unchanged:
// this TargetCodeGenInfo must not alter any other type's classification (in
// particular `_Bool` stays i1+extend and ordinary i8/i16/i32 scalars keep the
// existing direct registers/slots assignment).
//
// The ABIArgInfo only fixes the LLVM signature. The actual value conversions
// (normalize with `icmp ne` before any store, `zext` into the i8 boundary,
// entry decode, return normalization) are emitted by the dedicated bit
// marshal/unmarshal points in CGCall/CGDecl/CGStmt and never rely on the
// generic coercion-by-memory path.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "CodeGenModule.h"
#include "TargetInfo.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicsMCS251.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::CodeGen;
using llvm::report_fatal_error;

namespace {

class MCS251ABIInfo : public DefaultABIInfo {
public:
  MCS251ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  static bool isBitType(QualType Ty) {
    return Ty.getCanonicalType()->isMCS251BitType();
  }

  // Direct i8 coercion for the normalized 0/1 bit carrier: not i1, not
  // zeroext/signext on an i1, not an indirect/byval object.
  ABIArgInfo classifyBit(QualType Ty) const {
    return ABIArgInfo::getDirect(
        llvm::Type::getInt8Ty(getVMContext()));
  }

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isVoidType())
      return ABIArgInfo::getIgnore();
    if (isBitType(RetTy))
      return classifyBit(RetTy);
    if (isAggregateTypeForABI(RetTy)) {
      if (isEmptyRecord(getContext(), RetTy, true) ||
          getContext().getTypeSizeInChars(RetTy).isZero())
        return ABIArgInfo::getIgnore();
      if (const auto *RT = RetTy->getAs<RecordType>()) {
        if (RT->getDecl()->hasFlexibleArrayMember())
          return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);
      }
      uint64_t SizeBits = getContext().getTypeSize(RetTy);
      // Small trivially copyable structs/unions of 1, 2, or 4 bytes return
      // directly in DPL / DPTR / DPL:DPH:B:A; 3-byte and >4-byte aggregates
      // return via sret hidden pointer in Arg 0.
      if (SizeBits == 8 || SizeBits == 16 || SizeBits == 32)
        return ABIArgInfo::getDirect(
            llvm::IntegerType::get(getVMContext(), SizeBits));
      return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);
    }
    return DefaultABIInfo::classifyReturnType(RetTy);
  }

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);
    if (isBitType(Ty))
      return classifyBit(Ty);
    if (isAggregateTypeForABI(Ty)) {
      if (isEmptyRecord(getContext(), Ty, true) ||
          getContext().getTypeSizeInChars(Ty).isZero())
        return ABIArgInfo::getIgnore();
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
        return getNaturalAlignIndirect(
            Ty, RAA == CGCXXABI::RAA_DirectInMemory);
      // Pass all C/trivially-copyable aggregates by value on the stack with
      // byte alignment (matches fixed and variadic downward stack layout).
      return ABIArgInfo::getIndirect(CharUnits::One(), /*ByVal=*/true);
    }
    return DefaultABIInfo::classifyArgumentType(Ty);
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

// Stack-based reentrant variadic ABI:
// `va_list` is `{ptr __base, i32 __off}` (8 bytes with __off@4 on 32-bit AS0,
// or 6 bytes with __off@2 on 16-bit AS0 tiny/xtiny). `va_start` sets `__base`
// to one byte above the first variadic argument on the up-growing stack and
// `__off = 0`. Each `va_arg(ap, T)` advances `__off` by the exact stack slot
// size `Step` (promoted `int` size for narrow ints/`__bit`, `sizeof(T)` for
// aggregates/pointers/32-bit scalars) and loads from `__base - __off`.
RValue MCS251ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty, AggValueSlot Slot) const {
  ASTContext &Ctx = CGF.getContext();
  const llvm::DataLayout &DL = CGF.CGM.getDataLayout();

  auto *PairStruct = dyn_cast<llvm::StructType>(VAListAddr.getElementType());
  uint64_t OffOffset = 0;
  if (PairStruct && PairStruct->getNumElements() == 2 &&
      PairStruct->getElementType(0)->isPointerTy() &&
      PairStruct->getElementType(1)->isIntegerTy(32)) {
    OffOffset = DL.getStructLayout(PairStruct)->getElementOffset(1);
  }
  if (OffOffset != 2 && OffOffset != 4)
    report_fatal_error("MCS251: va_list must be a {ptr, i32} pair with __off "
                       "at byte 2 (16-bit ptr) or byte 4 (32-bit ptr)");

  const bool IsBit = isBitType(Ty);
  const bool IsAgg = !IsBit && isAggregateTypeForABI(Ty);
  llvm::Type *IRTy = CGF.ConvertTypeForMem(Ty);

  if (!IsBit && !IsAgg) {
    if (IRTy->isPointerTy()) {
      switch (cast<llvm::PointerType>(IRTy)->getAddressSpace()) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 8:
      case 9:
        break;
      default:
        report_fatal_error("MCS251: va_arg pointer address space has no "
                           "ordinary ABI");
      }
    } else if (IRTy->isIntegerTy()) {
      if (Ctx.getTypeSize(Ty) > 32)
        report_fatal_error("MCS251: va_arg integer wider than 32 bits is not "
                           "supported");
    } else if (!IRTy->isFloatTy()) {
      report_fatal_error("MCS251: unsupported va_arg type");
    }
  }

  CGBuilderTy &Builder = CGF.Builder;
  llvm::Value *AP = VAListAddr.emitRawPointer(CGF);
  llvm::Type *BasePtrTy = PairStruct->getElementType(0);

  // Load current byte offset (__off at byte 2 or byte 4) and __base (at 0).
  llvm::Value *OffP = Builder.CreateConstInBoundsGEP1_32(
      CGF.Int8Ty, AP, static_cast<unsigned>(OffOffset), "va.offp");
  llvm::Value *Off = Builder.CreateAlignedLoad(CGF.Int32Ty, OffP,
                                               CharUnits::One(), "va.off");
  llvm::Value *Base = Builder.CreateAlignedLoad(BasePtrTy, AP,
                                                CharUnits::One(), "va.base");

  // Handle empty aggregates (size 0) without advancing __off.
  if (IsAgg && (isEmptyRecord(Ctx, Ty, true) ||
                Ctx.getTypeSizeInChars(Ty).isZero())) {
    if (Slot.isIgnored())
      return RValue::getAggregate(Address::invalid());
    return RValue::getAggregate(Slot.getAddress());
  }

  const uint64_t IntSize = Ctx.getTargetInfo().getIntWidth() / 8;
  llvm::Type *IntTy = llvm::IntegerType::get(getVMContext(), IntSize * 8);

  uint64_t Step = 0;
  bool ReadPromotedInt = false;
  if (IsBit) {
    Step = IntSize;
    ReadPromotedInt = true;
  } else if (IsAgg) {
    Step = DL.getTypeAllocSize(IRTy);
  } else if (IRTy->isIntegerTy() && DL.getTypeAllocSize(IRTy) < IntSize) {
    Step = IntSize;
    ReadPromotedInt = true;
  } else {
    Step = DL.getTypeAllocSize(IRTy);
  }

  llvm::Value *OffN =
      Builder.CreateAdd(Off, llvm::ConstantInt::get(CGF.Int32Ty, Step),
                        "va.off.next");
  Builder.CreateAlignedStore(OffN, OffP, CharUnits::One());

  // Stack arguments sit downward from __base: address = __base - OffN.
  llvm::Value *NegOff =
      Builder.CreateSub(llvm::ConstantInt::get(CGF.Int32Ty, 0), OffN,
                        "va.negoff");
  llvm::Value *Idx =
      OffOffset == 2 ? Builder.CreateTrunc(NegOff, CGF.Int16Ty) : NegOff;
  llvm::Value *Addr = Builder.CreateGEP(CGF.Int8Ty, Base, Idx, "va.addr");

  if (IsAgg)
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(Address(Addr, IRTy, CharUnits::One()), Ty), Slot);

  llvm::Value *V;
  if (IsBit) {
    llvm::Value *Wide =
        Builder.CreateAlignedLoad(IntTy, Addr, CharUnits::One());
    llvm::Value *NZ = Builder.CreateICmpNE(
        Wide, llvm::ConstantInt::get(IntTy, 0), "va.bit.nz");
    V = Builder.CreateZExt(NZ, CGF.Int8Ty, "va.bit");
  } else if (ReadPromotedInt) {
    llvm::Value *Wide =
        Builder.CreateAlignedLoad(IntTy, Addr, CharUnits::One());
    V = Builder.CreateTrunc(Wide, IRTy);
  } else {
    V = Builder.CreateAlignedLoad(IRTy, Addr, CharUnits::One());
  }

  Address Temp = CGF.CreateMemTempWithoutCast(Ty, "va.arg.tmp");
  Builder.CreateStore(V, Temp);
  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(Temp, Ty), Slot);
}

class MCS251TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  MCS251TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<MCS251ABIInfo>(CGT)) {}

  // P-4 (freeze 2026-09-14, "签名的 IR 层保留"): publish the source-typed
  // function signatures as `!mcs251.signatures` named metadata.  This is the
  // single producer of Tag 28 records: the AST is the only place that still
  // knows `bit` from `unsigned char`, and the freeze forbids re-deriving a
  // signature from the i8 boundary.  llc reads the nodes back and converts
  // them; lld consumes the resulting object tag.
  //
  // One node per external-linkage function that reaches the final module
  // (definition or referenced declaration).  A compiler-generated external
  // declaration with no FunctionDecl (for example a libcall helper) is
  // recorded as an all-non-bit signature: every parameter of such a helper is
  // originally a pointer or an integer, so "non-bit" is exact and is not the
  // forbidden i8 re-derivation.
  // One node per external-linkage function the TU DECLARES or DEFINES, taken
  // from the SOURCE declarations rather than the emitted IR: Clang materialises
  // a plain forward declaration only on first use, so a declaration that this
  // TU never calls would otherwise be missing and a cross-TU bit/non-bit
  // conflict on it could not be detected.  The signature is read from the most
  // complete declaration of each name (a definition, else the prototyped
  // declaration with the most parameters), never from the canonical/earliest
  // declaration -- `int f(); int f(int);` must record `int f(int)`.
  void emitTargetMetadata(
      CodeGen::CodeGenModule &CGM,
      const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames)
      const override {
    if (CGM.getTarget().getTriple().getArch() != llvm::Triple::mcs251)
      return;

    llvm::LLVMContext &Ctx = CGM.getModule().getContext();
    llvm::DataLayout DL = CGM.getModule().getDataLayout();
    llvm::Module &M = CGM.getModule();

    auto finalSymbol = [&](StringRef IRName) {
      SmallString<128> Buf;
      llvm::Mangler::getNameWithPrefix(Buf, IRName, DL);
      return std::string(Buf);
    };

    // The set of external symbols the backend will really define in this
    // object.  A source-level body is NOT enough: a C11 `inline` definition
    // with external linkage emits no object symbol unless it is used, so the
    // role must follow the emitted IR, not the AST body.  Built once from the
    // final module before the source walk.
    llvm::StringSet<> EmittedDefinitions;
    for (const llvm::Function &F : M.functions()) {
      if (F.isIntrinsic() || F.isDeclaration() || F.hasLocalLinkage() ||
          F.hasAvailableExternallyLinkage())
        continue;
      EmittedDefinitions.insert(finalSymbol(F.getName()));
    }

    // --- Collect the source-typed declaration set ----------------------
    //
    // Keyed by the FINAL ELF symbol name so re-declarations with one linker
    // name merge into one record.  The stored FunctionDecl is the most
    // complete one seen (prefer a definition, then a prototype, then the most
    // parameters).
    struct Candidate {
      const FunctionDecl *Best = nullptr;
      bool HasBody = false;
    };
    llvm::StringMap<Candidate> Candidates;

    auto isBetterDecl = [](const FunctionDecl *New, const FunctionDecl *Old) {
      if (!Old)
        return true;
      const bool NewDef = New->doesThisDeclarationHaveABody();
      const bool OldDef = Old->doesThisDeclarationHaveABody();
      if (NewDef != OldDef)
        return NewDef; // a definition beats a declaration
      // Prefer a prototype over a K&R no-prototype declaration.
      if (New->hasPrototype() != Old->hasPrototype())
        return New->hasPrototype();
      // Prefer the declaration with the most source parameters, so a later,
      // more specific prototype wins over `int f()`.
      return New->getNumParams() > Old->getNumParams();
    };

    auto addDecl = [&](const FunctionDecl *FD) {
      if (!FD || FD->isTemplated())
        return;
      if (!FD->hasExternalFormalLinkage())
        return; // local/internal functions are outside the signature domain
      if (FD->getIdentifier() == nullptr && !FD->hasAttr<AsmLabelAttr>())
        return; // unnamed and unlabelled: no linker name
      std::string Symbol =
          finalSymbol(CGM.getMangledName(GlobalDecl(FD)).str());
      Candidate &C = Candidates[Symbol];
      if (isBetterDecl(FD, C.Best))
        C.Best = FD;
      if (FD->doesThisDeclarationHaveABody())
        C.HasBody = true;
    };

    // Walk every FunctionDecl reachable from the translation unit, INCLUDING
    // declarations nested inside function bodies (`extern __bit flag(__bit);`
    // in a block).  A top-level-only walk would miss them; the nested
    // declaration still owns the source `bit` type, and the freeze requires
    // it to be recorded exactly like a file-scope one.  A given declaration
    // may be reached through several redeclaration chains; addDecl merges
    // them by final symbol.
    class FuncDeclCollector
        : public RecursiveASTVisitor<FuncDeclCollector> {
    public:
      explicit FuncDeclCollector(
          llvm::function_ref<void(const FunctionDecl *)> Add)
          : Add(Add) {}
      bool VisitFunctionDecl(const FunctionDecl *FD) {
        Add(FD);
        return true; // keep descending into the body and its nested decls
      }

    private:
      llvm::function_ref<void(const FunctionDecl *)> Add;
    };
    FuncDeclCollector Collector(addDecl);
    Collector.TraverseDecl(CGM.getContext().getTranslationUnitDecl());

    // --- Emit the metadata nodes ---------------------------------------
    llvm::NamedMDNode *Named =
        M.getOrInsertNamedMetadata(llvm::MCS251Signatures::MetadataName);

    // A stable order keeps the IR (and thus the object bytes) reproducible.
    SmallVector<StringRef, 32> Symbols;
    for (const auto &KV : Candidates)
      Symbols.push_back(KV.getKey());
    llvm::sort(Symbols);

    for (StringRef Symbol : Symbols) {
      const Candidate &C = Candidates[Symbol];
      const FunctionDecl *FD = C.Best;
      // The role describes the OBJECT symbol, not the source body: a C11
      // inline definition that the backend did not emit is a declaration in
      // this object (there is no definition for a reader to associate), while
      // a body that did reach the object is a definition.
      const bool IsDefinition = C.HasBody && EmittedDefinitions.contains(Symbol);
      const bool NoProto = !FD->hasPrototype();
      const bool Variadic = FD->isVariadic();
      const unsigned ParamCount = FD->getNumParams();

      llvm::SmallVector<llvm::Metadata *, 8> Ops;
      Ops.push_back(llvm::MDString::get(Ctx, Symbol));
      uint8_t Role = IsDefinition
                         ? llvm::MCS251Signatures::Role_HasDefinition
                         : llvm::MCS251Signatures::Role_DeclaredNotDefined;
      // Zero-parameter protocol rule (PM ruling 2026-09-15, revised after
      // review): bit2 records the SOURCE prototype-ness of the recorded
      // declaration, for every no-prototype function -- including the
      // zero-parameter ones.  A K&R definition's parameter list is real
      // (getNumParams covers it), so ParamCount is always the exact source
      // count and a K&R `int f(x) int x;` is written (bit2=1, count=1),
      // which the reader rejects against a prototyped `int f(void)`
      // (bit2 differs and the counts are not both 0).  The reader-side
      // exception carries the compatibility instead: a bit2 difference
      // between two records that BOTH have param_count 0 is ignored, so a
      // `void main()` definition (bit2=1, count=0) still links against the
      // CRT's `void main(void)` declaration (bit2=0, count=0) -- the
      // runtime ABI is identical and old objects written either way remain
      // linkable in both directions.
      if (NoProto)
        Role |= llvm::MCS251Signatures::Role_NoPrototype;
      if (Variadic)
        Role |= llvm::MCS251Signatures::Role_Variadic;
      Ops.push_back(llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(Ctx), Role)));
      bool RetBit = FD->getReturnType()
                        .getCanonicalType()
                        ->isMCS251BitType();
      Ops.push_back(llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(Ctx), RetBit ? 1 : 0)));
      for (unsigned I = 0; I != ParamCount; ++I) {
        bool Bit = FD->getParamDecl(I)
                       ->getType()
                       .getCanonicalType()
                       ->isMCS251BitType();
        Ops.push_back(llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(Ctx), Bit ? 1 : 0)));
      }
      Named->addOperand(llvm::MDNode::get(Ctx, Ops));
    }

    // A compiler-generated external declaration with no FunctionDecl (for
    // example a libcall helper that reached the IR) is recorded as an
    // all-non-bit signature: every such helper is a pointer/integer function,
    // so "non-bit" is exact and is not the forbidden i8 re-derivation.  The
    // source walk above cannot see it because it owns no Decl.
    llvm::StringSet<> Covered;
    for (const auto &KV : Candidates)
      Covered.insert(KV.getKey());
    for (const llvm::Function &F : M.functions()) {
      if (F.isIntrinsic() || F.hasLocalLinkage() ||
          F.hasAvailableExternallyLinkage() || F.getName().empty())
        continue;
      std::string Symbol = finalSymbol(F.getName());
      if (!Covered.insert(Symbol).second)
        continue;
      llvm::SmallVector<llvm::Metadata *, 8> Ops;
      Ops.push_back(llvm::MDString::get(Ctx, Symbol));
      Ops.push_back(llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(Ctx),
          F.isDeclaration() ? llvm::MCS251Signatures::Role_DeclaredNotDefined
                            : llvm::MCS251Signatures::Role_HasDefinition)));
      Ops.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0)));
      for (unsigned I = 0, E = F.arg_size(); I != E; ++I)
        Ops.push_back(llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(Ctx), 0)));
      Named->addOperand(llvm::MDNode::get(Ctx, Ops));
    }
  }

};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createMCS251TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<MCS251TargetCodeGenInfo>(CGM.getTypes());
}
