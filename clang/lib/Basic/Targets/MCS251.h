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
#include "llvm/TargetParser/MCS251TargetParser.h"

namespace clang::targets {

class LLVM_LIBRARY_VISIBILITY MCS251TargetInfo final : public TargetInfo {
  llvm::MCS251::MemoryContract Contract;

public:
  MCS251TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : TargetInfo(Triple), Contract(Opts.MCS251Memory) {
    // The default C model is ILP32. +int16 selects the traditional C model;
    // it is a translation-unit language option, not an ISA feature.
    ShortWidth = 16;
    IntWidth = LongWidth = PointerWidth = 32;
    LongLongWidth = 64;
    ShortAlign = IntAlign = LongAlign = LongLongAlign = PointerAlign = 8;
    FloatWidth = 32;
    FloatAlign = 8;
    DoubleWidth = 32;
    DoubleAlign = 8;
    DoubleFormat = &llvm::APFloat::IEEEsingle();
    LongDoubleWidth = 32;
    LongDoubleAlign = 8;
    LongDoubleFormat = &llvm::APFloat::IEEEsingle();
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

    // The no-flag default is xsmall/v2. Compatibility is available only when
    // a caller explicitly supplies the legacy layout contract.
    if (!Contract.isSpecified())
      Contract = {1, 2, 32, 8, 1};
    if (auto Desc = llvm::MCS251::getLayoutDesc(
            static_cast<llvm::MCS251::ASLayoutVersion>(Contract.ASLayoutVersion),
            static_cast<llvm::MCS251::AS0PointerBits>(Contract.AS0PointerBits))) {
      for (const auto &P : Desc->Pointers)
        if (P.AddressSpace == 0) {
          PointerWidth = P.Size;
          PointerAlign = P.ABIAlignment;
          break;
        }
      resetDataLayout(Desc->DataLayout);
    } else {
      resetDataLayout();
    }
  }

  uint64_t getMaxPointerWidth() const override { return 32; }

  // _BitInt(N) maps onto the backend's integer registers: widths up to 32
  // bits legalize through the existing promotion paths (iN -> i8/i16/i32),
  // while the backend has no storage or arithmetic beyond the 32-bit DR
  // registers, so wider extents are rejected loudly. This override also
  // caps -fexperimental-max-bitint-width, which otherwise defaults to 128.
  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override { return 32; }

  uint64_t getPointerWidthV(LangAS AS) const override {
    unsigned TargetAS = getTargetAddressSpace(AS);
    if (auto Desc = llvm::MCS251::getLayoutDesc(
            static_cast<llvm::MCS251::ASLayoutVersion>(Contract.ASLayoutVersion),
            static_cast<llvm::MCS251::AS0PointerBits>(Contract.AS0PointerBits)))
      for (const auto &P : Desc->Pointers)
        if (P.AddressSpace == TargetAS)
          return P.Size;
    return PointerWidth;
  }
  uint64_t getPointerAlignV(LangAS AS) const override {
    unsigned TargetAS = getTargetAddressSpace(AS);
    if (auto Desc = llvm::MCS251::getLayoutDesc(
            static_cast<llvm::MCS251::ASLayoutVersion>(Contract.ASLayoutVersion),
            static_cast<llvm::MCS251::AS0PointerBits>(Contract.AS0PointerBits)))
      for (const auto &P : Desc->Pointers)
        if (P.AddressSpace == TargetAS)
          return P.ABIAlignment;
    return PointerAlign;
  }
  bool validateTarget(DiagnosticsEngine &Diags) const override;

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
