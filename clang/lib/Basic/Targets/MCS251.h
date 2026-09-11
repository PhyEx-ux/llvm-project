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
  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override;
  // MCS-251 bit/sbit status (M1, frontend-only):
  //   * Implemented: the independent `bit`/`__bit` type, its Sema boundaries,
  //     old-style `sbit` parsing, and __builtin_mcs251_bit_lvalue with a
  //     controlled volatile lvalue result.
  //   * P08 revision (Alice ruling, plan B; enforced by the MCS251 OpenMP/
  //     OpenACC construct restriction context in SemaMCS251 + the parser
  //     entry points): 首期不支持在启用并被前端处理的 OpenMP/OpenACC 构造
  //     中使用 MCS251 bit 能力，包括 bit 类型/对象、sbit 和 L1 固定位引用。
  //     构造的参数、clause、关联语句或声明及相关捕获均受限制，不区分读值、
  //     写入、存储绑定或是否实际求值。构造外的普通 C bit 语义不变；不含
  //     该能力的 OMP/ACC 构造维持既有行为。
  //     (English summary: the first slice rejects every MCS251 bit capability
  //     -- bit types/objects, sbit, L1 fixed-bit references -- inside enabled
  //     OpenMP/OpenACC constructs the frontend processes, across directive
  //     arguments, clauses, associated statements/declarations, and captures,
  //     regardless of read/write, storage binding, or actual evaluation.
  //     Ordinary C bit semantics outside constructs, and bit-free OMP/ACC
  //     constructs, keep their existing behavior. Diagnostics:
  //     err_mcs251_bit_omp_construct / err_mcs251_bit_acc_construct, a true
  //     error family, not a CodeGen backstop. Final rework: the capability is
  //     identified recursively -- bit carried inside a function-pointer or
  //     block signature (return type or parameter, typedef'd or not), a block
  //     literal's own signature, a __builtin_va_arg bit type argument, and
  //     function definitions owned by declare simd / declare variant marks
  //     are all construct input; the restriction stack is drained
  //     unconditionally at end of translation unit.)
  //   * P08 items deferred to M2, with mixed enforcement today (do not treat as
  //     supported):
  //       - COMMON bit objects (`-fcommon`) and weak bit objects are accepted by
  //         Sema and then rejected by CodeGen ("MCS251 bit global yet"); they do
  //         not silently become ordinary values.
  //       - A bit value passed through varargs (`g(0, (__bit)1)`) or to an
  //         unprototyped function (`g((__bit)1)`) is accepted today and is a
  //         real gap: no bit ABI is emitted for it.
  //       - `va_arg(ap, __bit)` used directly as a value (e.g. `return
  //         __builtin_va_arg(ap, __bit);`) compiles as an i8 slot and is a real
  //         gap; binding it to a local `__bit` object instead hits the ordinary
  //         bit-object fail-closed gate above. Note: `va_arg` is fully usable on
  //         this target for ordinary types; the only `va_arg` failure is passing
  //         the *address* of a `va_list` (`va_arg(&ap, T)`), which is a general
  //         va_list lvalue requirement unrelated to bit.
  //   * CodeGen fail-closed: every bit object that would need storage (global,
  //     local, static, extern read, compound literal including in a constant
  //     initializer, volatile/register/thread-local, block capture, parameter)
  //     and every controlled fixed reference (old-style `sbit`, the builtin
  //     lvalue, and a bit alias) reports "cannot compile this ... yet" until M2
  //     lowers it. The only bit-related CodeGen that succeeds today is a bit
  //     *value* already materialized as an i1 scalar (e.g. `return 2;` from a
  //     bit function or a bit-typed expression that performs no bit-object
  //     storage), plus the one registered exception above: `va_arg(ap, __bit)`
  //     returned directly as a value, which compiles as an i8 slot and is a
  //     known M2 gap rather than a fail-closed diagnostic.
  //   * Constant-expression boundary not covered by the fail-closed guard:
  //     a bit compound literal folded to a plain integer *value* in an enum
  //     initializer, `case` label, `_Static_assert`, or bit-field width creates
  //     no bit storage and stays accepted. The guard covers object/storage
  //     initializers (global/local/static/aggregate) and the builtin fold.
  // The MCS251 core bit type uses the reserved spelling "__bit"; it is a
  // property of the target, not of -fmcs251-keil. The target options are
  // adjusted after the language options have been established (see
  // CompilerInstance::createTarget), so this is where the flag is set.
  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
              const TargetInfo *Aux) override;
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
