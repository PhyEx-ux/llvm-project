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
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::CodeGen;

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
};

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
      const unsigned ParamCount = NoProto ? 0 : FD->getNumParams();

      llvm::SmallVector<llvm::Metadata *, 8> Ops;
      Ops.push_back(llvm::MDString::get(Ctx, Symbol));
      uint8_t Role = IsDefinition
                         ? llvm::MCS251Signatures::Role_HasDefinition
                         : llvm::MCS251Signatures::Role_DeclaredNotDefined;
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
