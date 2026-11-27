//===--- MCS251.cpp - MCS-251 bare-metal toolchain ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Tool.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

namespace {

class MCS251Assembler final : public Tool {
public:
  MCS251Assembler(const ToolChain &TC)
      : Tool("MCS251::Assembler", "llvm-mc", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const ArgList &Args,
                    const char *LinkingOutput) const override {
    ArgStringList CmdArgs;
    CmdArgs.push_back(Args.MakeArgString(
        "-triple=" + getToolChain().getEffectiveTriple().str()));
    CmdArgs.push_back("-filetype=obj");
    Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

    assert(Inputs.size() == 1 && "Unexpected number of inputs.");
    const InputInfo &Input = Inputs[0];
    assert(Input.isFilename() && "Invalid input.");
    CmdArgs.push_back(Input.getFilename());

    assert(Output.isFilename() && "Invalid output.");
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());

    const char *Exec =
        Args.MakeArgString(getToolChain().GetProgramPath("llvm-mc"));
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::AtFileCurCP(),
                                           Exec, CmdArgs, Inputs, Output));
  }
};

class MCS251StaticLibTool final : public Tool {
public:
  MCS251StaticLibTool(const ToolChain &TC)
      : Tool("MCS251::StaticLibTool", "llvm-ar", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const ArgList &Args,
                    const char *LinkingOutput) const override {
    const Driver &D = getToolChain().getDriver();
    Args.ClaimAllArgs(options::OPT_g_Group);
    Args.ClaimAllArgs(options::OPT_emit_llvm);
    Args.ClaimAllArgs(options::OPT_w);

    ArgStringList CmdArgs;
    CmdArgs.push_back("rcsD");
    Args.AddAllArgValues(CmdArgs, options::OPT_Xstatic_lib_tool);
    CmdArgs.push_back(Output.getFilename());

    for (const auto &II : Inputs)
      if (II.isFilename())
        CmdArgs.push_back(II.getFilename());

    const char *OutputFileName = Output.getFilename();
    if (Output.isFilename() && llvm::sys::fs::exists(OutputFileName)) {
      if (std::error_code EC = llvm::sys::fs::remove(OutputFileName)) {
        D.Diag(diag::err_drv_unable_to_remove_file) << EC.message();
        return;
      }
    }

    const char *Exec =
        Args.MakeArgString(getToolChain().GetStaticLibToolPath());
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::AtFileCurCP(),
                                           Exec, CmdArgs, Inputs, Output));
  }
};

class MCS251Linker final : public Tool {
public:
  MCS251Linker(const ToolChain &TC)
      : Tool("MCS251::Linker", "mcs251-lld", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const ArgList &Args,
                    const char *LinkingOutput) const override {
    const ToolChain &TC = getToolChain();
    Args.ClaimAllArgs(options::OPT_g_Group);
    Args.ClaimAllArgs(options::OPT_w);
    Args.ClaimAllArgs(options::OPT_nostdlib);
    Args.ClaimAllArgs(options::OPT_nostartfiles);
    Args.ClaimAllArgs(options::OPT_nodefaultlibs);

    std::string LinkerPath = TC.GetLinkerPath();
    StringRef Stem = llvm::sys::path::stem(LinkerPath);

    ArgStringList CmdArgs;
    // Generic `lld` requires `-flavor mcs251`; a dedicated `mcs251-lld` binary
    // selects the flavor via argv[0].
    if (!Stem.equals_insensitive("mcs251-lld")) {
      CmdArgs.push_back("-flavor");
      CmdArgs.push_back("mcs251");
    }

    Args.addAllArgs(CmdArgs,
                    {options::OPT_L, options::OPT_T, options::OPT_u,
                     options::OPT_e, options::OPT_s});
    if (TC.isUsingLTO(Args)) {
      if (const Arg *A = Args.getLastArg(options::OPT_O_Group)) {
        StringRef Opt = A->getSpelling();
        if (A->getOption().matches(options::OPT_O0))
          CmdArgs.push_back("--lto-O0");
        else if (A->getOption().matches(options::OPT_O)) {
          StringRef Val = A->getValue();
          if (Val == "0")
            CmdArgs.push_back("--lto-O0");
          else if (Val == "1")
            CmdArgs.push_back("--lto-O1");
          else if (Val == "3")
            CmdArgs.push_back("--lto-O3");
          else
            CmdArgs.push_back("--lto-O2");
        } else if (Opt == "-Ofast") {
          CmdArgs.push_back("--lto-O3");
        } else {
          CmdArgs.push_back("--lto-O2");
        }
      }
    }
    tools::AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

    if (Output.isFilename()) {
      CmdArgs.push_back("-o");
      CmdArgs.push_back(Output.getFilename());
    }

    const char *Exec = Args.MakeArgString(LinkerPath);
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::AtFileCurCP(),
                                           Exec, CmdArgs, Inputs, Output));
  }
};

} // namespace

MCS251ToolChain::MCS251ToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const llvm::opt::ArgList &Args)
    : ToolChain(D, Triple, Args) {
  // Search sibling tools (lld, mcs251-lld, llvm-ar, llvm-mc) alongside clang
  // so relocatable Linux/Windows toolchains work without PATH setup.
  getProgramPaths().push_back(D.Dir);
}

void MCS251ToolChain::AddClangSystemIncludeArgs(
    const llvm::opt::ArgList &Args, llvm::opt::ArgStringList &CC1Args) const {
  if (Args.hasArg(options::OPT_nostdinc, options::OPT_nobuiltininc))
    return;
  // Supply Clang's freestanding headers, but no host headers or target libc.
  llvm::SmallString<128> Dir(getDriver().ResourceDir);
  llvm::sys::path::append(Dir, "include");
  addSystemInclude(Args, CC1Args, Dir);
}

Tool *MCS251ToolChain::buildLinker() const { return new MCS251Linker(*this); }

Tool *MCS251ToolChain::buildStaticLibTool() const {
  return new MCS251StaticLibTool(*this);
}

Tool *MCS251ToolChain::buildAssembler() const {
  return new MCS251Assembler(*this);
}
