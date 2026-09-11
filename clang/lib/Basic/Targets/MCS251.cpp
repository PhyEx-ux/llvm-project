//===--- MCS251.cpp - MCS-251 target information ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/TargetParser/MCS251TargetParser.h"
#include <iterator>

using namespace clang;
using namespace clang::targets;

static constexpr int NumBuiltins =
    clang::MCS251::LastTSBuiltin - Builtin::FirstTSBuiltin;

#define GET_BUILTIN_STR_TABLE
#include "clang/Basic/BuiltinsMCS251.inc"
#undef GET_BUILTIN_STR_TABLE

static constexpr Builtin::Info BuiltinInfos[] = {
#define GET_BUILTIN_INFOS
#include "clang/Basic/BuiltinsMCS251.inc"
#undef GET_BUILTIN_INFOS
};
static_assert(std::size(BuiltinInfos) == NumBuiltins);

llvm::SmallVector<Builtin::InfosShard>
MCS251TargetInfo::getTargetBuiltins() const {
  return {{&BuiltinStrings, BuiltinInfos}};
}

bool MCS251TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeatureVec) const {
  for (StringRef Feature : FeatureVec) {
    if (Feature != "+int16" && Feature != "-int16") {
      Diags.Report(diag::err_invalid_feature_combination)
          << ("MCS251 supports only +int16/-int16; long is fixed at 32 bits "
              "and pointer widths come from the numeric memory contract "
              "(requested '" +
              Feature.str() + "')");
      return false;
    }
  }
  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeatureVec);
}

bool MCS251TargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                          DiagnosticsEngine &Diags) {
  for (StringRef Feature : Features)
    if (Feature == "+int16")
      IntWidth = 16;

  // Do not advertise a C data model as an LLVM instruction-set feature.
  // IR types already encode it; keeping this list empty also avoids unknown
  // backend feature diagnostics and meaningless per-function ISA attributes.
  Features.clear();
  return true;
}

void MCS251TargetInfo::adjust(DiagnosticsEngine &Diags, LangOptions &Opts,
                              const TargetInfo *Aux) {
  // The core `__bit` spelling is a property of the MCS-251 target and is
  // available regardless of -fmcs251-keil. Keil's bare `bit`/`sbit` spellings
  // are handled separately by LangOptions::MCS251Keil.
  Opts.MCS251Bit = 1;
  TargetInfo::adjust(Diags, Opts, Aux);
}

bool MCS251TargetInfo::validateTarget(DiagnosticsEngine &Diags) const {
  if (!llvm::MCS251::isValidMemoryContract(Contract)) {
    Diags.Report(diag::err_invalid_feature_combination)
        << "invalid MCS-251 numeric memory contract";
    return false;
  }
  auto Desc = llvm::MCS251::getLayoutDesc(
      static_cast<llvm::MCS251::ASLayoutVersion>(Contract.ASLayoutVersion),
      static_cast<llvm::MCS251::AS0PointerBits>(Contract.AS0PointerBits));
  if (!Desc || getDataLayoutString() != Desc->DataLayout) {
    Diags.Report(diag::err_invalid_feature_combination)
        << "MCS-251 target data layout does not match its numeric contract";
    return false;
  }
  return true;
}

void MCS251TargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  Builder.defineMacro("__mcs251__");
  Builder.defineMacro("__MCS251__");
  if (IntWidth == 16)
    Builder.defineMacro("__MCS251_INT16__");
}
