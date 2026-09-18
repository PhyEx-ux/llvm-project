//===- PlacementVerifyMain.cpp - mcs251-placement-verify entry ------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// G11-D (contract §1, R2): the standalone verifier's CLI and process entry.
// Exit status is 0 on success and 1 for every other outcome (argument, I/O,
// malformed input, verification failure, or a failed child start) -- there is
// deliberately no "partial success" code.  Success is silent.
//===----------------------------------------------------------------------===//

#include "PlacementVerify.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <string>

using namespace llvm;

namespace lld::mcs251::placementverify {

static std::optional<uint32_t> number(StringRef S) {
  uint32_t V = 0;
  if (S.getAsInteger(0, V))
    return std::nullopt;
  return V;
}

// A single-value option that accepts both `--name value` and `--name=value`.
static std::optional<StringRef> take(ArrayRef<const char *> Args, size_t &I,
                                     StringRef Name, StringRef A) {
  if (A == Name) {
    if (++I == Args.size())
      return std::nullopt;
    return StringRef(Args[I]);
  }
  std::string WithEquals = (Name + "=").str();
  if (A.starts_with(WithEquals))
    return A.substr(Name.size() + 1);
  return std::nullopt;
}

int runPlacementVerify(ArrayRef<const char *> Args, raw_ostream &Out,
                       raw_ostream &Err) {
  Options O;
  for (size_t I = 1; I < Args.size(); ++I) {
    StringRef A(Args[I]);
    if (A == "--help" || A == "-h") {
      Out << "mcs251-placement-verify --elf=<path> --report=<path> "
             "[--object=<path> ...] [--placement-manifest=<path> ...] "
             "[--link-map=<path>] [window options]\n";
      Out.flush();
      return 0;
    }
    if (A == "--version") {
      Out << "mcs251-placement-verify (LLVM MCS251)\n";
      Out.flush();
      return 0;
    }
    if (auto V = take(Args, I, "--elf", A)) {
      O.Elf = V->str();
      continue;
    }
    if (auto V = take(Args, I, "--report", A)) {
      O.Report = V->str();
      continue;
    }
    if (auto V = take(Args, I, "--object", A)) {
      O.Objects.push_back(V->str());
      continue;
    }
    if (auto V = take(Args, I, "--placement-manifest", A)) {
      O.Manifests.push_back(V->str());
      continue;
    }
    if (auto V = take(Args, I, "--link-map", A)) {
      O.LinkMap = V->str();
      continue;
    }
    if (auto V = take(Args, I, "--flash-base", A)) {
      auto N = number(*V);
      if (!N || *N > 0xffffff) {
        Err << "mcs251-placement-verify: error: invalid --flash-base\n";
        return 1;
      }
      O.FlashBase = *N;
      O.HasFlashBase = true;
      continue;
    }
    if (auto V = take(Args, I, "--flash-size", A)) {
      auto N = number(*V);
      if (!N || *N == 0 || uint64_t(*N) > 0x1000000) {
        Err << "mcs251-placement-verify: error: invalid --flash-size\n";
        return 1;
      }
      O.FlashSize = *N;
      O.HasFlashSize = true;
      continue;
    }
    if (auto V = take(Args, I, "--xdata-size", A)) {
      auto N = number(*V);
      if (!N || *N == 0 || uint64_t(*N) > 0x1000000) {
        Err << "mcs251-placement-verify: error: invalid --xdata-size\n";
        return 1;
      }
      O.XdataSize = *N;
      O.HasXdataSize = true;
      continue;
    }
    if (auto V = take(Args, I, "--edata-end", A)) {
      auto N = number(*V);
      if (!N || *N > 0xffff) {
        Err << "mcs251-placement-verify: error: invalid --edata-end\n";
        return 1;
      }
      O.EdataEnd = *N;
      O.HasEdataEnd = true;
      continue;
    }
    if (auto V = take(Args, I, "--iram-size", A)) {
      auto N = number(*V);
      if (!N) {
        Err << "mcs251-placement-verify: error: invalid --iram-size\n";
        return 1;
      }
      O.IramSize = *N;
      O.HasIramSize = true;
      continue;
    }
    if (auto V = take(Args, I, "--area-start", A)) {
      size_t Eq = V->find('=');
      if (Eq == StringRef::npos) {
        Err << "mcs251-placement-verify: error: invalid --area-start\n";
        return 1;
      }
      auto Address = number(V->drop_front(Eq + 1));
      StringRef AreaName = V->take_front(Eq);
      if (!Address || AreaName.empty()) {
        Err << "mcs251-placement-verify: error: invalid --area-start\n";
        return 1;
      }
      O.AreaStarts.emplace_back(AreaName.str(), *Address);
      continue;
    }
    if (auto V = take(Args, I, "--reserve-data", A)) {
      size_t Comma = V->find(',');
      if (Comma == StringRef::npos) {
        Err << "mcs251-placement-verify: error: invalid --reserve-data\n";
        return 1;
      }
      auto Start = number(V->take_front(Comma));
      auto Size = number(V->drop_front(Comma + 1));
      if (!Start || !Size || uint64_t(*Start) + *Size > 0x10000) {
        Err << "mcs251-placement-verify: error: invalid --reserve-data\n";
        return 1;
      }
      O.ReservedData.push_back({*Start, *Start + *Size});
      continue;
    }
    Err << "mcs251-placement-verify: error: unknown option " << A << "\n";
    return 1;
  }
  if (O.Elf.empty() || O.Report.empty()) {
    Err << "mcs251-placement-verify: error: --elf and --report are required\n";
    return 1;
  }
  if (!verifyPlacement(O, Err))
    return 1;
  return 0;
}

} // namespace lld::mcs251::placementverify

int main(int Argc, const char **Argv) {
  llvm::InitLLVM X(Argc, Argv);
  llvm::raw_ostream &Out = llvm::outs();
  llvm::raw_ostream &Err = llvm::errs();
  return lld::mcs251::placementverify::runPlacementVerify(
      llvm::ArrayRef<const char *>(Argv, Argc), Out, Err);
}
