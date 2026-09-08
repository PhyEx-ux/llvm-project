//===- Driver.cpp - MCS251 flavor shell -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file deliberately contains flavor policy and the ET_EXEC writer only.
// ELF validation, layout, relocation, XINIT validation, and stack-gate
// arithmetic belong to LinkerCore.cpp, which has no flavor-driver dependency.
//===----------------------------------------------------------------------===//

#include "LinkerCore.h"
#include "lld/Common/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace lld::mcs251 {
namespace {

static constexpr uint16_t EM_MCS251 = 0x9999;
static constexpr uint32_t EF_MCS251_ABI_V1 = 1;
static constexpr uint32_t MCS251_DEFAULT_EDATA_END = 0x0fff;

struct FlavorOptions {
  LinkerConfig Core;
  std::string Output;
  std::string Map;
  std::string MapJSON;
  bool HelpOrVersion = false;
};

struct Segment {
  uint32_t Address;
  std::vector<uint8_t> Bytes;
};

bool fail(raw_ostream &Err, const Twine &Msg) {
  Err << "mcs251-lld: error: " << Msg << '\n';
  return false;
}

std::optional<uint32_t> number(StringRef S) {
  uint32_t V = 0;
  if (S.getAsInteger(0, V))
    return std::nullopt;
  return V;
}

void append16be(std::vector<uint8_t> &B, uint16_t V) {
  B.push_back(V >> 8);
  B.push_back(V);
}

void append32be(std::vector<uint8_t> &B, uint32_t V) {
  B.push_back(V >> 24);
  B.push_back(V >> 16);
  B.push_back(V >> 8);
  B.push_back(V);
}

std::vector<Segment> makeSegments(const LinkerResult &Result) {
  std::vector<Segment> Segments;
  for (auto It = Result.Image.begin(); It != Result.Image.end();) {
    Segment S{It->first, {}};
    uint32_t Next = It->first;
    do {
      S.Bytes.push_back(It->second);
      ++Next;
      ++It;
    } while (It != Result.Image.end() && It->first == Next);
    Segments.push_back(std::move(S));
  }
  return Segments;
}

bool writeExecutable(const LinkerResult &Result, StringRef Path,
                     raw_ostream &Err) {
  std::vector<Segment> Segments = makeSegments(Result);
  if (Segments.empty())
    return fail(Err, "no loadable bytes");
  if (Segments.size() > 0xfffd)
    return fail(Err, "too many sparse PT_LOAD segments");

  constexpr uint32_t EhdrSize = 52;
  constexpr uint32_t PhdrSize = 32;
  constexpr uint32_t ShdrSize = 40;
  uint64_t DataOffset = EhdrSize + uint64_t(PhdrSize) * Segments.size();
  if (DataOffset > 0xffffffff)
    return fail(Err, "program header table is too large");
  std::vector<uint32_t> FileOffsets;
  FileOffsets.reserve(Segments.size());
  for (const Segment &S : Segments) {
    if (DataOffset + S.Bytes.size() > 0xffffffff)
      return fail(Err, "ET_EXEC output is too large");
    FileOffsets.push_back(static_cast<uint32_t>(DataOffset));
    DataOffset += S.Bytes.size();
  }

  std::vector<uint8_t> Names = {0};
  std::vector<uint32_t> NameOffsets;
  for (size_t I = 0; I != Segments.size(); ++I) {
    NameOffsets.push_back(Names.size());
    std::string Name = ".mcs251.load." + std::to_string(I);
    Names.insert(Names.end(), Name.begin(), Name.end());
    Names.push_back(0);
  }
  uint32_t ShstrName = Names.size();
  StringRef Shstr = ".shstrtab";
  Names.insert(Names.end(), Shstr.bytes_begin(), Shstr.bytes_end());
  Names.push_back(0);
  if (DataOffset + Names.size() > 0xffffffff)
    return fail(Err, "ET_EXEC output is too large");
  uint32_t ShstrOffset = static_cast<uint32_t>(DataOffset);
  DataOffset += Names.size();
  DataOffset = (DataOffset + 3) & ~uint64_t(3);
  uint64_t Shoff = DataOffset;
  uint64_t Shnum = Segments.size() + 2; // null + sparse load sections + names
  if (Shoff + Shnum * ShdrSize > 0xffffffff)
    return fail(Err, "section header table is too large");

  std::vector<uint8_t> ELF;
  ELF.reserve(static_cast<size_t>(Shoff + Shnum * ShdrSize));
  ELF.insert(ELF.end(), {'\x7f', 'E', 'L', 'F', 1, 2, 1, 0,
                         0, 0, 0, 0, 0, 0, 0, 0});
  append16be(ELF, 2);                         // ET_EXEC
  append16be(ELF, EM_MCS251);
  append32be(ELF, 1);                         // EV_CURRENT
  append32be(ELF, Result.Entry);              // HOME area start per E3 policy.
  append32be(ELF, EhdrSize);                  // e_phoff
  append32be(ELF, static_cast<uint32_t>(Shoff));
  append32be(ELF, EF_MCS251_ABI_V1);
  append16be(ELF, EhdrSize);
  append16be(ELF, PhdrSize);
  append16be(ELF, Segments.size());
  append16be(ELF, ShdrSize);
  append16be(ELF, Shnum);
  append16be(ELF, Segments.size() + 1);       // .shstrtab index
  for (size_t I = 0; I != Segments.size(); ++I) {
    const Segment &S = Segments[I];
    append32be(ELF, 1);                       // PT_LOAD
    append32be(ELF, FileOffsets[I]);
    append32be(ELF, S.Address);               // p_vaddr
    append32be(ELF, S.Address);               // p_paddr
    append32be(ELF, S.Bytes.size());          // p_filesz
    append32be(ELF, S.Bytes.size());          // p_memsz
    append32be(ELF, 5);                       // PF_R | PF_X
    append32be(ELF, 1);                       // byte alignment preserves holes
  }
  for (const Segment &S : Segments)
    ELF.insert(ELF.end(), S.Bytes.begin(), S.Bytes.end());
  ELF.insert(ELF.end(), Names.begin(), Names.end());
  ELF.resize(Shoff, 0);
  append32be(ELF, 0); append32be(ELF, 0); append32be(ELF, 0);
  append32be(ELF, 0); append32be(ELF, 0); append32be(ELF, 0);
  append32be(ELF, 0); append32be(ELF, 0); append32be(ELF, 0); append32be(ELF, 0);
  for (size_t I = 0; I != Segments.size(); ++I) {
    const Segment &S = Segments[I];
    append32be(ELF, NameOffsets[I]);
    append32be(ELF, 1);                       // SHT_PROGBITS
    append32be(ELF, 6);                       // SHF_ALLOC | SHF_EXECINSTR
    append32be(ELF, S.Address);
    append32be(ELF, FileOffsets[I]);
    append32be(ELF, S.Bytes.size());
    append32be(ELF, 0); append32be(ELF, 0);
    append32be(ELF, 1); append32be(ELF, 0);
  }
  append32be(ELF, ShstrName);
  append32be(ELF, 3);                         // SHT_STRTAB
  append32be(ELF, 0); append32be(ELF, 0);
  append32be(ELF, ShstrOffset); append32be(ELF, Names.size());
  append32be(ELF, 0); append32be(ELF, 0);
  append32be(ELF, 1); append32be(ELF, 0);

  std::string Temp = (Path + ".tmp").str();
  std::error_code EC;
  {
    raw_fd_ostream OS(Temp, EC, sys::fs::OF_None);
    if (EC)
      return fail(Err, "cannot open temporary output " + Temp);
    OS.write(reinterpret_cast<const char *>(ELF.data()), ELF.size());
    OS.flush();
    if (OS.has_error())
      return fail(Err, "cannot write temporary output " + Temp);
  }
  EC = sys::fs::rename(Temp, Path);
  if (EC) {
    sys::fs::remove(Temp);
    return fail(Err, "cannot replace output " + Path);
  }
  return true;
}

bool writeText(StringRef Path, StringRef Contents, StringRef Kind,
               raw_ostream &Err) {
  std::string Temp = (Path + ".tmp").str();
  std::error_code EC;
  {
    raw_fd_ostream OS(Temp, EC, sys::fs::OF_None);
    if (EC)
      return fail(Err, "cannot open temporary " + Kind + " " + Temp);
    OS << Contents;
    OS.flush();
    if (OS.has_error())
      return fail(Err, "cannot write temporary " + Kind + " " + Temp);
  }
  EC = sys::fs::rename(Temp, Path);
  if (EC) {
    sys::fs::remove(Temp);
    return fail(Err, "cannot replace " + Kind + " " + Path);
  }
  return true;
}

bool parseArgs(ArrayRef<const char *> Args, FlavorOptions &O,
               raw_ostream &Out, raw_ostream &Err) {
  // SPEC defaults are flavor policy. The arch-shaped core has no defaults for
  // either output policy or the stack gate.
  O.Core.EdataEnd = MCS251_DEFAULT_EDATA_END;
  O.Core.EnableStackGate = true;
  // E3/M4 Code ROM gate: both flash parameters or neither. The default is no
  // gate at all; the build layer owns the decision to pass a flash window.
  uint32_t FlashBase = 0;
  uint32_t FlashSize = 0;
  bool FlashBaseGiven = false;
  bool FlashSizeGiven = false;
  for (size_t I = 1; I < Args.size(); ++I) {
    StringRef A(Args[I]);
    auto take = [&](StringRef Name) -> std::optional<StringRef> {
      if (A == Name) {
        if (++I == Args.size())
          return std::nullopt;
        return StringRef(Args[I]);
      }
      std::string WithEquals = (Name + "=").str();
      if (A.starts_with(WithEquals))
        return A.substr(Name.size() + 1);
      return std::nullopt;
    };
    if (A == "--print-input") {
      O.Core.PrintInput = true;
    } else if (A == "--stack-gate") {
      O.Core.EnableStackGate = true;
    } else if (A == "--no-stack-gate") {
      return fail(Err, "--no-stack-gate is not supported; SPEC mandates the stack gate");
    } else if (auto V = take("-o")) {
      O.Output = V->str();
    } else if (auto V = take("--map")) {
      O.Map = V->str();
    } else if (auto V = take("--map-json")) {
      (void)V;
      return fail(Err, "--map-json is not implemented");
    } else if (auto V = take("--edata-end")) {
      auto N = number(*V);
      if (!N || *N > 0xffff)
        return fail(Err, "invalid --edata-end");
      O.Core.EdataEnd = *N;
    } else if (auto V = take("--iram-size")) {
      auto N = number(*V);
      if (!N)
        return fail(Err, "invalid --iram-size");
      O.Core.IramSize = *N;
    } else if (auto V = take("--stack-size")) {
      auto N = number(*V);
      if (!N)
        return fail(Err, "invalid --stack-size");
      O.Core.StackSize = *N;
    } else if (auto V = take("--flash-base")) {
      // Flash window start, a plain 24-bit address. No board-model knowledge
      // lives here; the build layer supplies the number (E.5 red line).
      auto N = number(*V);
      if (!N || *N > 0xffffff)
        return fail(Err, "invalid --flash-base (expected a 24-bit address)");
      FlashBase = *N;
      FlashBaseGiven = true;
    } else if (auto V = take("--flash-size")) {
      // Flash capacity in bytes; 1..0x1000000 keeps base+size in 24-bit space.
      auto N = number(*V);
      if (!N || *N == 0 || uint64_t(*N) > 0x1000000)
        return fail(Err, "invalid --flash-size (expected 1..0x1000000)");
      FlashSize = *N;
      FlashSizeGiven = true;
    } else if (auto V = take("--area-start")) {
      size_t Eq = V->find('=');
      if (Eq == StringRef::npos)
        return fail(Err, "invalid --area-start");
      auto Address = number(V->drop_front(Eq + 1));
      StringRef AreaName = V->take_front(Eq);
      if (!Address || AreaName.empty())
        return fail(Err, "invalid --area-start");
      // SPEC §5.1: validate the area name and reject duplicate/conflicting
      // settings.  CODE/XDATA areas use 24-bit addresses, DATA areas 16-bit.
      static const char *CodeAreas[] = {"HOME", "VECS", "BOOT", "CSEG",
                                        "XINIT"};
      static const char *DataAreas[] = {"DSEG", "ISEG"};
      bool IsCode = false, IsData = false, IsDataAbs = false;
      for (const char *A2 : CodeAreas)
        if (AreaName == A2) {
          IsCode = true;
          break;
        }
      for (const char *A2 : DataAreas)
        if (AreaName == A2) {
          IsData = true;
          break;
        }
      if (AreaName == "XSEG")
        IsCode = true; // XDATA uses 24-bit address space
      if (AreaName.starts_with(".mcs251.DATA."))
        IsDataAbs = true;
      if (!IsCode && !IsData && !IsDataAbs)
        return fail(Err, "unknown area name in --area-start: " + AreaName);
      if (IsCode && *Address > 0xffffff)
        return fail(Err, "area start out of 24-bit CODE/XDATA range: " +
                         AreaName);
      if (IsData && *Address > 0xffff)
        return fail(Err, "area start out of 16-bit DATA range: " + AreaName);
      if (IsDataAbs && *Address > 0xffff)
        return fail(Err, "area start out of 16-bit DATA range: " + AreaName);
      for (const auto &P : O.Core.AreaStarts)
        if (P.first == AreaName.str())
          return fail(Err, "duplicate --area-start for " + AreaName);
      O.Core.AreaStarts.emplace_back(AreaName.str(), *Address);
    } else if (auto V = take("--reserve-data")) {
      size_t Comma = V->find(',');
      if (Comma == StringRef::npos)
        return fail(Err, "invalid --reserve-data");
      auto Start = number(V->take_front(Comma));
      auto Size = number(V->drop_front(Comma + 1));
      if (!Start || !Size || *Start + *Size > 0x10000)
        return fail(Err, "invalid --reserve-data");
      O.Core.ReservedData.push_back({*Start, *Start + *Size});
    } else if (A == "--oformat=ihex" || A == "--oformat") {
      return fail(Err, "Intel HEX is produced by llvm-objcopy -O ihex");
    } else if (A == "--help") {
      Out << "mcs251-lld [--stack-gate] [options] file...\n";
      O.HelpOrVersion = true;
      return true;
    } else if (A == "--version") {
      Out << "mcs251-lld (LLVM MCS251)\n";
      O.HelpOrVersion = true;
      return true;
    } else if (A == "-flavor" || A == "mcs251") {
      if (A == "-flavor" && ++I == Args.size())
        return fail(Err, "missing flavor name");
    } else if (A.starts_with('-')) {
      return fail(Err, "unknown option " + A);
    } else {
      O.Core.Inputs.push_back(A.str());
    }
  }
  if (FlashBaseGiven != FlashSizeGiven)
    return fail(Err, "--flash-base and --flash-size must be given together");
  if (FlashBaseGiven &&
      uint64_t(FlashBase) + uint64_t(FlashSize) > 0x1000000)
    return fail(Err,
                "--flash-base and --flash-size exceed the 24-bit address space");
  if (FlashBaseGiven) {
    O.Core.FlashGate = true;
    O.Core.FlashBase = FlashBase;
    O.Core.FlashSize = FlashSize;
  }
  if (O.Core.Inputs.empty())
    return fail(Err, "no input files");
  if (!O.Core.PrintInput && O.Output.empty())
    O.Output = O.Core.Inputs.front() + ".elf";
  return true;
}

} // namespace

bool link(ArrayRef<const char *> Args, raw_ostream &Out, raw_ostream &Err,
          bool ExitEarly, bool DisableOutput) {
  (void)ExitEarly;
  FlavorOptions Options;
  if (!parseArgs(Args, Options, Out, Err))
    return false;
  if (Options.HelpOrVersion) {
    // In normal CLI mode (no LLD_IN_TEST), the dispatcher calls exitLld()
    // which calls _exit() without flushing buffered stdout.  Ensure the
    // help/version text is visible.
    Out.flush();
    return true;
  }
  const bool PrintInput = Options.Core.PrintInput;
  LinkerResult Result;
  if (!linkCore(std::move(Options.Core), Result, Err))
    return false;
  if (DisableOutput)
    return true;
  if (PrintInput) {
    Out << Result.InputReport;
    Out.flush();
    return true;
  }
  if (!Options.Map.empty() && !writeText(Options.Map, Result.Map, "map", Err))
    return false;
  return writeExecutable(Result, Options.Output, Err);
}

} // namespace lld::mcs251
