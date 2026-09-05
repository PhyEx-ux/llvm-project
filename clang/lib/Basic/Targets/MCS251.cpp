//===--- MCS251.cpp - MCS-251 target information ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

bool MCS251TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeatureVec) const {
  for (StringRef Feature : FeatureVec) {
    if (Feature != "+int16" && Feature != "-int16") {
      Diags.Report(diag::err_invalid_feature_combination)
          << ("MCS251 supports only +int16/-int16; long and pointer widths "
              "are fixed at 32 bits (requested '" +
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

void MCS251TargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  Builder.defineMacro("__mcs251__");
  Builder.defineMacro("__MCS251__");
  if (IntWidth == 16)
    Builder.defineMacro("__MCS251_INT16__");
}
