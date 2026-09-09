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
#include "llvm/BinaryFormat/ELF.h"
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

bool writeExecutable(const LinkerResult &Result, bool KeepSymbols,
                     StringRef Path, raw_ostream &Err) {
  std::vector<Segment> Segments = makeSegments(Result);
  if (Segments.empty())
    return fail(Err, "no loadable bytes");
  if (Segments.size() > 0xfffd)
    return fail(Err, "too many sparse PT_LOAD segments");

  // E5: the optional final symbol table.  Everything it adds (two names in
  // .shstrtab, a .strtab, a .symtab, two section rows) is appended strictly
  // after the layout that exists without it, so a default link keeps the
  // exact bytes the frozen release artifacts are hashed against.
  struct OutSym {
    uint32_t Name, Value, Size;
    uint8_t Info;
    uint16_t Shndx;
  };
  std::vector<OutSym> Syms; // [0] = null symbol, then locals, then globals.
  std::vector<uint8_t> Strtab = {0};
  uint32_t FirstGlobal = 0; // .symtab sh_info: index of the first global.
  uint32_t StrtabName = 0, SymtabName = 0;
  const bool EmitSyms = KeepSymbols && !Result.Symbols.empty();
  if (EmitSyms) {
    auto AddName = [&](StringRef N) {
      uint32_t Off = Strtab.size();
      Strtab.insert(Strtab.end(), N.begin(), N.end());
      Strtab.push_back(0);
      return Off;
    };
    auto FindShndx = [&](uint32_t Addr) -> uint16_t {
      for (size_t I = 0; I != Segments.size(); ++I) {
        const Segment &S = Segments[I];
        if (S.Address <= Addr && Addr < S.Address + S.Bytes.size())
          return static_cast<uint16_t>(I + 1);
      }
      // NOBITS (DSEG/ISEG/XSEG...) reservations and synthesised boundary
      // symbols belong to no loadable output section: report them ABS.
      return ELF::SHN_ABS;
    };
    // ELF requires entry 0 of every .symtab to be the all-zero null symbol
    // (STN_UNDEF): st_name/st_value/st_size/st_info/st_other/st_shndx all
    // zero.  It is itself STB_LOCAL, so every real symbol sits at index >= 1.
    Syms.push_back(OutSym{});
    for (const OutputSymbol &S : Result.Symbols) {
      OutSym O;
      O.Name = AddName(S.Name);
      O.Value = S.Address;
      O.Size = S.Size;
      O.Info = static_cast<uint8_t>((S.Bind << 4) | (S.Type & 0xf));
      O.Shndx = S.Synth ? static_cast<uint16_t>(ELF::SHN_ABS)
                        : FindShndx(S.Address);
      Syms.push_back(O);
    }
    // collectSymbols() orders locals before globals; sh_info must be the
    // index of the first global, i.e. the null symbol plus every local.
    // Derive it from the serialized rows so the section header, the entry
    // order and the actual indices can never drift apart.  An all-local
    // table uses the entry count (one past the last local).
    FirstGlobal = static_cast<uint32_t>(Syms.size());
    for (size_t I = 0; I != Syms.size(); ++I)
      if ((Syms[I].Info >> 4) != ELF::STB_LOCAL) {
        FirstGlobal = static_cast<uint32_t>(I);
        break;
      }
  }

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
  if (EmitSyms) {
    StrtabName = Names.size();
    StringRef S1 = ".strtab";
    Names.insert(Names.end(), S1.bytes_begin(), S1.bytes_end());
    Names.push_back(0);
    SymtabName = Names.size();
    StringRef S2 = ".symtab";
    Names.insert(Names.end(), S2.bytes_begin(), S2.bytes_end());
    Names.push_back(0);
  }
  if (DataOffset + Names.size() > 0xffffffff)
    return fail(Err, "ET_EXEC output is too large");
  uint32_t ShstrOffset = static_cast<uint32_t>(DataOffset);
  DataOffset += Names.size();
  DataOffset = (DataOffset + 3) & ~uint64_t(3);
  uint32_t StrtabOffset = static_cast<uint32_t>(DataOffset);
  uint32_t SymtabOffset = 0;
  if (EmitSyms) {
    DataOffset += Strtab.size();
    DataOffset = (DataOffset + 3) & ~uint64_t(3);
    SymtabOffset = static_cast<uint32_t>(DataOffset);
    DataOffset += uint64_t(Syms.size()) * 16;
    DataOffset = (DataOffset + 3) & ~uint64_t(3);
  }
  uint64_t Shoff = DataOffset;
  // null + sparse load sections + names (+ .strtab + .symtab)
  uint64_t Shnum = Segments.size() + 2 + (EmitSyms ? 2u : 0);
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
  if (EmitSyms) {
    ELF.resize(StrtabOffset, 0);
    ELF.insert(ELF.end(), Strtab.begin(), Strtab.end());
    ELF.resize(SymtabOffset, 0);
    for (const OutSym &S : Syms) {
      append32be(ELF, S.Name);
      append32be(ELF, S.Value);
      append32be(ELF, S.Size);
      ELF.push_back(S.Info);                    // st_info
      ELF.push_back(0);                         // st_other
      append16be(ELF, S.Shndx);
    }
  }
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
  if (EmitSyms) {
    // .strtab: section index Segments.size()+2.
    append32be(ELF, StrtabName);
    append32be(ELF, 3);                       // SHT_STRTAB
    append32be(ELF, 0); append32be(ELF, 0);   // sh_flags, sh_addr
    append32be(ELF, StrtabOffset);
    append32be(ELF, Strtab.size());
    append32be(ELF, 0);                       // sh_link
    append32be(ELF, 0);                       // sh_info
    append32be(ELF, 1);                       // sh_addralign
    append32be(ELF, 0);                       // sh_entsize
    // .symtab: section index Segments.size()+3; links to .strtab.
    append32be(ELF, SymtabName);
    append32be(ELF, 2);                       // SHT_SYMTAB
    append32be(ELF, 0); append32be(ELF, 0);
    append32be(ELF, SymtabOffset);
    append32be(ELF, Syms.size() * 16);        // sh_size: null symbol included
    append32be(ELF, Segments.size() + 2);     // sh_link -> .strtab
    append32be(ELF, FirstGlobal);             // sh_info -> first global
    append32be(ELF, 4);                       // sh_addralign
    append32be(ELF, 16);                      // sh_entsize
  }

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
    } else if (A == "--isr-reentrancy") {
      // E2: static parameter-slot reentrancy diagnosis; on by default.
      O.Core.IsrReentrancyDiag = true;
    } else if (A == "--no-isr-reentrancy") {
      // Opt out for builds whose ABI-safe call relationships are guaranteed
      // by other means; the diagnosis is a warning, never an error.
      O.Core.IsrReentrancyDiag = false;
    } else if (A == "--keep-symbols") {
      // E5: keep a final symbol table in the ELF and function-level rows in
      // the map.  Off by default: the frozen release artifacts (manifest.json
      // SHA256 of the linked ELF and the map) pin the exact output bytes, so
      // traceability output must be an explicit opt-in.
      O.Core.KeepSymbols = true;
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
      Out << "mcs251-lld [--stack-gate] [--isr-reentrancy] [--keep-symbols] "
             "[options] file...\n";
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
  // E5: Options.Core is moved into the core below; capture the flag first.
  const bool KeepSymbols = Options.Core.KeepSymbols;
  LinkerResult Result;
  if (!linkCore(std::move(Options.Core), Result, Err))
    return false;
  // E2: reentrancy warnings never fail the link; they go to stderr ahead of
  // any output writing.
  if (!Result.Diagnostics.empty())
    Err << Result.Diagnostics;
  if (DisableOutput)
    return true;
  if (PrintInput) {
    Out << Result.InputReport;
    Out.flush();
    return true;
  }
  if (!Options.Map.empty() && !writeText(Options.Map, Result.Map, "map", Err))
    return false;
  return writeExecutable(Result, KeepSymbols, Options.Output, Err);
}

} // namespace lld::mcs251
