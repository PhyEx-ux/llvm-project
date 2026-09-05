//===--- MCS251.cpp - MCS-251 bare-metal toolchain ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;

namespace {
class UnsupportedTool final : public Tool {
  const char *Operation;
  bool Linking;

public:
  UnsupportedTool(const ToolChain &TC, const char *Operation, bool Linking)
      : Tool("MCS251::Unsupported", "MCS251", TC), Operation(Operation),
        Linking(Linking) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return Linking; }
  void ConstructJob(Compilation &, const JobAction &, const InputInfo &,
                    const InputInfoList &, const llvm::opt::ArgList &,
                    const char *) const override {
    getToolChain().getDriver().Diag(diag::err_drv_clang_unsupported) << Operation;
  }
};
} // namespace

void MCS251ToolChain::AddClangSystemIncludeArgs(
    const llvm::opt::ArgList &Args, llvm::opt::ArgStringList &CC1Args) const {
  if (Args.hasArg(options::OPT_nostdinc, options::OPT_nobuiltininc))
    return;
  // Supply Clang's freestanding headers, but no host headers or target libc.
  llvm::SmallString<128> Dir(getDriver().ResourceDir);
  llvm::sys::path::append(Dir, "include");
  addSystemInclude(Args, CC1Args, Dir);
}

Tool *MCS251ToolChain::buildLinker() const {
  return new UnsupportedTool(
      *this, "MCS251 driver linking; use mcs251_ld.py with an explicit .lk file",
      true);
}

Tool *MCS251ToolChain::buildStaticLibTool() const {
  return new UnsupportedTool(*this, "MCS251 driver archive creation", true);
}

Tool *MCS251ToolChain::buildAssembler() const {
  return new UnsupportedTool(
      *this, "MCS251 assembly input; use sdas251 on the output of clang -S",
      false);
}
