//===--- MCS251.h - MCS-251 bare-metal toolchain ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MCS251_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MCS251_H

#include "clang/Driver/ToolChain.h"

namespace clang::driver::toolchains {

// Bare-metal ELF toolchain for Intel/STC MCS-251: uses Clang's integrated
// assembler (MCS251AsmParser) by default, llvm-mc as external assembler,
// llvm-ar for static libraries, and lld (-flavor mcs251) / mcs251-lld for
// firmware linking.
class LLVM_LIBRARY_VISIBILITY MCS251ToolChain final : public ToolChain {
public:
  MCS251ToolChain(const Driver &D, const llvm::Triple &Triple,
                 const llvm::opt::ArgList &Args);

  bool isBareMetal() const override { return true; }
  bool isCrossCompiling() const override { return true; }
  bool IsIntegratedAssemblerDefault() const override { return true; }
  bool HasNativeLLVMSupport() const override { return true; }
  bool IsMathErrnoDefault() const override { return false; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &) const override { return false; }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }
  const char *getDefaultLinker() const override { return "lld"; }
  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &) const override {
    return UnwindTableLevel::None;
  }

  void AddClangSystemIncludeArgs(
      const llvm::opt::ArgList &Args,
      llvm::opt::ArgStringList &CC1Args) const override;

protected:
  Tool *buildLinker() const override;
  Tool *buildStaticLibTool() const override;
  Tool *buildAssembler() const override;
};

} // namespace clang::driver::toolchains

#endif
