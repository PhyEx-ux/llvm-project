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

// Compilation uses LLVM's ASxxxx object writer, not a host ELF assembler or
// linker. Firmware linking remains an explicit mcs251_ld.py invocation.
class LLVM_LIBRARY_VISIBILITY MCS251ToolChain final : public ToolChain {
public:
  MCS251ToolChain(const Driver &D, const llvm::Triple &Triple,
                 const llvm::opt::ArgList &Args)
      : ToolChain(D, Triple, Args) {}

  bool isBareMetal() const override { return true; }
  bool isCrossCompiling() const override { return true; }
  bool HasNativeLLVMSupport() const override { return true; }
  bool IsMathErrnoDefault() const override { return false; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &) const override { return false; }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }
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
