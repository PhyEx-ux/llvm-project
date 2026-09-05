//===--- MCS251.h - MCS-251 target information ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MCS251_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MCS251_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"

namespace clang::targets {

class LLVM_LIBRARY_VISIBILITY MCS251TargetInfo final : public TargetInfo {
public:
  MCS251TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // The default C model is ILP32. +int16 selects the traditional C model;
    // it is a translation-unit language option, not an ISA feature.
    ShortWidth = 16;
    IntWidth = LongWidth = PointerWidth = 32;
    LongLongWidth = 64;
    ShortAlign = IntAlign = LongAlign = LongLongAlign = PointerAlign = 8;
    FloatAlign = DoubleAlign = LongDoubleAlign = 8;
    SuitableAlign = DefaultAlignForAttributeAligned = 8;
    SizeType = UnsignedLong;
    PtrDiffType = IntPtrType = SignedLong;
    Int16Type = SignedShort;
    Char32Type = UnsignedLong;
    WCharType = WIntType = SignedShort;
    SigAtomicType = SignedChar;
    TLSSupported = false;
    VLASupported = false;
    HasMustTail = false;
    UserLabelPrefix = "_";
    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }
  bool initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                      StringRef CPU,
                      const std::vector<std::string> &FeatureVec) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;
  bool hasFeature(StringRef Feature) const override {
    return Feature == "mcs251" || (Feature == "int16" && IntWidth == 16);
  }
  // Function target attributes cannot change a translation unit's C model.
  bool isValidFeatureName(StringRef Feature) const override { return false; }
  bool allowsLargerPreferedTypeAlignment() const override { return false; }
  ArrayRef<const char *> getGCCRegNames() const override { return {}; }
  ArrayRef<GCCRegAlias> getGCCRegAliases() const override { return {}; }
  bool validateAsmConstraint(const char *&Name,
                             ConstraintInfo &Info) const override {
    // Target-specific inline assembly constraints are not yet a C interface.
    return false;
  }
  std::string_view getClobbers() const override { return ""; }
  BuiltinVaListKind getBuiltinVaListKind() const override {
    // This supplies the language type; variadic calls remain unsupported by
    // the backend and are not part of the initial single-argument C ABI.
    return CharPtrBuiltinVaList;
  }
};

} // namespace clang::targets

#endif
