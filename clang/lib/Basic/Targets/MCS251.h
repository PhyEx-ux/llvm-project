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
    // Provisional C data model: keep int compatible with the SDCC/C251 world.
    // Changing this requires an ABI decision, not just a backend legalization.
    IntWidth = ShortWidth = 16;
    LongWidth = PointerWidth = 32;
    LongLongWidth = 64;
    ShortAlign = IntAlign = LongAlign = PointerAlign = 8;
    // Wider integers and floating types retain the LLVM DataLayout defaults.
    // Their runtime operations are outside the initial i8/i16/i32 C subset.
    LongLongAlign = FloatAlign = 32;
    DoubleAlign = LongDoubleAlign = 64;
    SuitableAlign = DefaultAlignForAttributeAligned = 64;
    SizeType = UnsignedLong;
    PtrDiffType = IntPtrType = SignedLong;
    Int16Type = SignedInt;
    Char32Type = UnsignedLong;
    WCharType = WIntType = SignedInt;
    SigAtomicType = SignedChar;
    TLSSupported = false;
    VLASupported = false;
    HasMustTail = false;
    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }
  bool hasFeature(StringRef Feature) const override { return Feature == "mcs251"; }
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
