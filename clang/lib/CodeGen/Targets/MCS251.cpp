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
#include "TargetInfo.h"

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
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createMCS251TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<MCS251TargetCodeGenInfo>(CGM.getTypes());
}
