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

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI)) {
      if (isBitType(FI.getReturnType()))
        FI.getReturnInfo() = classifyBit(FI.getReturnType());
      else
        FI.getReturnInfo() = DefaultABIInfo::classifyReturnType(
            FI.getReturnType());
    }
    for (auto &I : FI.arguments()) {
      if (isBitType(I.type))
        I.info = classifyBit(I.type);
      else
        I.info = DefaultABIInfo::classifyArgumentType(I.type);
    }
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

// G2 B1 static-slot variadic ABI (G2-VARIADIC-DESIGN-draft.md R3 §4.3.3(c)
// / §4.3.5).  va_arg is lowered HERE, as ordinary IR, and never reaches the
// backend as an llvm.va_arg intrinsic (the backend fail-closes any residual
// one).  `va_list` is the 8-byte {__base, __off} pair (MCS251BuiltinVaList,
// __base@0, __off@4); VAListAddr points at that pair.  __base carries the
// owner function's first continuation-slot address (identity of the source
// slot area), so this code performs pure {base,off} pointer arithmetic and
// never names a `_PARM_n` symbol -- a va_list forwarded to any ordinary
// helper function consumes the OWNER's slots through the same code.
//
// IR shape per design §4.3.3(c) (R3 probe V: all of it selects on today's
// llc); the halt arm never flows back, so the result needs no PHI:
//
//   entry:  %off = load i32, ptr %offp            ; pair byte 4
//           %ok  = icmp ule i32 %off, 20          ; 6 slots x 4B, last = 20
//           br i1 %ok, label %load, label %halt
//   halt:   call void @llvm.mcs251.vararg.halt()  ; `sjmp .` in the backend
//           unreachable                           ; deterministic halt
//   load:   %base = load ptr, ptr %ap
//           %addr = getelementptr i8, ptr %base, i32 %off
//           %v    = load <slot type>, ptr %addr align 1   ; 4B slot
//           store i32 %off + 4, ptr %offp          ; advance exactly 1 slot
//
// Narrow reads (i8/i16, the inverse of the default argument promotion) load
// the 4B slot as i32 and truncate; f32 loads its bit pattern directly
// (double == f32 on this target); pointers load the already-canonicalized
// 4B slot (the caller's A-byte zeroing happened in the backend store path).
RValue MCS251ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty, AggValueSlot Slot) const {
  // Fail-closed classification.  Sema already hard-errors the frozen
  // rejection set on this exact path (N18/D2, SemaMCS251::
  // CheckMCS251VAArgType); these are the CodeGen backstops so a Sema bypass
  // can never reach the backend as a silently-wrong slot read.  `bit` stays
  // on the frozen M2 boundary (G2 §4.6): the continuation-slot ABI defines
  // no bit encoding, so instead of the historical silent i8-slot compile it
  // now stops loudly here.
  if (isBitType(Ty))
    report_fatal_error("MCS251: va_arg of __bit is not supported (frozen M2 "
                       "boundary, G2 §4.6)");
  const ASTContext &Ctx = CGF.getContext();
  // Pair-layout backstop (G2 B-S2 review fix, Alice blocker 1): the IR
  // below addresses __off with a hardwired 4-byte access at byte 4 and
  // advances by 4, so a narrower pair (a 6-byte {ptr, i16} object) would
  // silently run both past the object end.  The va_list construction path
  // (ASTContext::CreateMCS251BuiltinVaListDecl) pins the offset field to
  // the architecturally-32-bit `unsigned long`, so the i16-offset form
  // cannot be built in any C model; this re-checks the frozen layout at
  // the point of use so that a degenerate pair arriving by any other road
  // (e.g. the 16-bit-AS0 memory contracts, whose `void*` base makes the
  // pair 6 bytes with __off at byte 2) stops loudly instead of reading
  // past the object end.
  auto *PairStruct = dyn_cast<llvm::StructType>(VAListAddr.getElementType());
  if (!PairStruct || PairStruct->getNumElements() != 2 ||
      !PairStruct->getElementType(0)->isPointerTy() ||
      !PairStruct->getElementType(1)->isIntegerTy(32) ||
      CGF.CGM.getDataLayout()
              .getStructLayout(PairStruct)
              ->getElementOffset(1) != 4)
    report_fatal_error("MCS251: va_list must be the frozen 8-byte {4-byte "
                       "base, 4-byte offset} pair with __off at byte 4; a "
                       "narrower pair would put the 4-byte offset access "
                       "past the object end (the offset field is pinned to "
                       "i32 at construction; 16-bit data-pointer memory "
                       "contracts have no variadic support)");
  llvm::Type *IRTy = CGF.ConvertTypeForMem(Ty);
  if (IRTy->isPointerTy()) {
    switch (cast<llvm::PointerType>(IRTy)->getAddressSpace()) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 8:
    case 9:
      break; // hasOrdinaryPointerABI set, same boundary as the backend
    default:
      report_fatal_error("MCS251: va_arg pointer address space has no "
                         "ordinary static-slot encoding");
    }
  } else if (IRTy->isIntegerTy()) {
    // Integral and enumeration targets up to 32 bits (the promotion set).
    if (Ctx.getTypeSize(Ty) > 32)
      report_fatal_error("MCS251: va_arg integer wider than 32 bits has no "
                         "slot encoding (rejected by Sema N18; CodeGen "
                         "backstop)");
  } else if (!IRTy->isFloatTy()) {
    // float only: DoubleWidth is 32 on this target, so every real floating
    // va_arg type is f32-sized; anything else (aggregates, complex, ...) is
    // in the Sema rejection set.
    report_fatal_error("MCS251: va_arg type must be a promoted scalar "
                       "(i8/i16/i32/f32) or an ordinary data pointer "
                       "(rejected by Sema N18; CodeGen backstop)");
  }

  CGBuilderTy &Builder = CGF.Builder;
  llvm::Value *AP = VAListAddr.emitRawPointer(CGF);
  llvm::Type *APTy = AP->getType();

  // __off at byte 4 of the pair.
  llvm::Value *OffP = Builder.CreateConstInBoundsGEP1_32(CGF.Int8Ty, AP, 4,
                                                         "va.offp");
  llvm::Value *Off = Builder.CreateAlignedLoad(CGF.Int32Ty, OffP,
                                               CharUnits::One(), "va.off");
  // Six 4-byte continuation slots: offsets 0,4,...,20 stay legal, anything
  // else halts deterministically (the compiler cannot count a loop of
  // va_args; G2 §4.3.6 last row).
  llvm::Value *OK = Builder.CreateICmpULE(
      Off, llvm::ConstantInt::get(CGF.Int32Ty, 5 * 4), "va.ok");

  llvm::BasicBlock *HaltBB = CGF.createBasicBlock("vaarg.halt");
  llvm::BasicBlock *LoadBB = CGF.createBasicBlock("vaarg.load");
  Builder.CreateCondBr(OK, LoadBB, HaltBB);

  // Halt arm: llvm.mcs251.vararg.halt lowers to the VARARG_HALT machine
  // instruction (`sjmp .`, two bytes, no call frame, no runtime symbol).
  // Deliberately not llvm.trap -- the generic expansion of that is an
  // _abort libcall and this runtime has no abort provider (G2 §4.3.6, R3
  // probe V3).  unreachable keeps the arm from rejoining, so the result is
  // branch-free on this path.
  // NB: the new blocks must be linked into the function with EmitBlock
  // (createBasicBlock leaves them unparented in this tree); a bare
  // Builder.SetInsertPoint would emit into detached blocks and leave the
  // br dangling (<badref>), which crashes every later CFG consumer.
  CGF.EmitBlock(HaltBB);
  Builder.CreateCall(
      CGF.CGM.getIntrinsic(llvm::Intrinsic::mcs251_vararg_halt));
  Builder.CreateUnreachable();

  CGF.EmitBlock(LoadBB);
  llvm::Value *Base = Builder.CreateAlignedLoad(APTy, AP, CharUnits::One(),
                                                "va.base");
  llvm::Value *Addr =
      Builder.CreateInBoundsGEP(CGF.Int8Ty, Base, Off, "va.addr");

  llvm::Value *V;
  if (IRTy->isIntegerTy(8) || IRTy->isIntegerTy(16)) {
    // Narrow read: the slot always holds a promoted i32 (G2 §4.3.1 slot
    // table: 1B/2B continuation slots do not exist).
    llvm::Value *Wide =
        Builder.CreateAlignedLoad(CGF.Int32Ty, Addr, CharUnits::One());
    V = Builder.CreateTrunc(Wide, IRTy);
  } else {
    V = Builder.CreateAlignedLoad(IRTy, Addr, CharUnits::One());
  }

  // Advance exactly one 4-byte slot, independent of the read width.
  llvm::Value *OffN =
      Builder.CreateAdd(Off, llvm::ConstantInt::get(CGF.Int32Ty, 4));
  Builder.CreateAlignedStore(OffN, OffP, CharUnits::One());

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
      if (F.isIntrinsic() || F.isDeclaration() || !F.hasExternalLinkage())
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
      if (F.isIntrinsic() || !F.hasExternalLinkage() || F.getName().empty())
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
