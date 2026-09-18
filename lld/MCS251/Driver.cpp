//===- Driver.cpp - MCS251 flavor shell -----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file deliberately contains flavor policy and the ET_EXEC writer only.
// ELF validation, layout, relocation, XINIT validation, and stack-gate
// arithmetic belong to LinkerCore.cpp, which has no flavor-driver dependency.
//===----------------------------------------------------------------------===//

#include "LinkerCore.h"
#include "PlacementReport.h"
#include "lld/Common/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace lld::mcs251 {
namespace {

static constexpr uint16_t EM_MCS251 = 0x9999;
static constexpr uint32_t EF_MCS251_ABI_V1 = 1;
static constexpr uint32_t MCS251_DEFAULT_EDATA_END = 0x0fff;
// G11-D2 (design §3.2/§3.3): the positioning carrier envelope and record
// fields.  Every u32 is unsigned big-endian; there is no string pool, path
// field, timestamp or trailing tail.
static constexpr const char PositionsNoteSectionName[] =
    ".mcs251.placement.positions";
static constexpr uint32_t PositionsNoteType = 3;
static constexpr uint32_t PositionsVersion = 1;

struct FlavorOptions {
  LinkerConfig Core;
  std::string Output;
  std::string Map;
  std::string MapJSON;
  bool HelpOrVersion = false;
  // G11-D audit surface (contract §1).  `AuditReport` is true only when
  // --placement-report carried a non-empty path; `AuditVerify` only when
  // --verify-placement was given.  Neither implies the other, but both imply
  // KeepSymbols (the only coupling rule).
  bool AuditReport = false;
  bool AuditVerify = false;
  std::string ReportPath;
  // Tracked so the verifier receives exactly the window numbers the link was
  // given, and never a default the user did not ask for.
  bool EdataEndGiven = false;
  bool IramSizeGiven = false;
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

// G11-D2 (design §3.6): the positioning NOTE serializer.  A PURE function of
// the collected result: it reads no environment variable, path or randomness,
// so the same ordered object bytes, link options and tool revision produce a
// byte-identical carrier.  All length arithmetic runs in 64 bits and every
// serialized length must remain representable as a u32.
bool serializePositionsNote(const LinkerResult &Result,
                            std::vector<uint8_t> &Out, raw_ostream &Err) {
  const uint64_t ObjectCount = Result.PositionObjects.size();
  uint64_t Descsz = 16 + 32 * ObjectCount;
  for (const LinkerResult::PositionSection &P : Result.PositionSections)
    Descsz += 24 + 12 * uint64_t(P.Slices.size());
  if (Descsz > 0xffffffffull)
    return fail(Err, "positioning NOTE descriptor exceeds the u32 size limit");
  Out.clear();
  Out.reserve(20 + size_t(Descsz));
  append32be(Out, 7);                      // namesz
  append32be(Out, static_cast<uint32_t>(Descsz));
  append32be(Out, PositionsNoteType);      // the positioning carrier type
  const uint8_t Magic[8] = {'M', 'C', 'S', '2', '5', '1', 0, 0};
  Out.insert(Out.end(), Magic, Magic + 8); // "MCS251\0" + name padding
  append32be(Out, PositionsVersion);       // position_version
  append32be(Out, static_cast<uint32_t>(ObjectCount));
  append32be(Out, static_cast<uint32_t>(Result.PositionSections.size()));
  append32be(Out, 0);                      // reserved
  for (const std::array<uint8_t, 32> &Digest : Result.PositionObjects)
    Out.insert(Out.end(), Digest.begin(), Digest.end());
  for (const LinkerResult::PositionSection &P : Result.PositionSections) {
    if (P.StorageSpace > 2)
      return fail(Err, "internal: positioning record with an unknown storage "
                       "space");
    if (P.Slices.empty() || P.Slices.size() > 0xffffffffull - 1)
      return fail(Err, "internal: positioning record with an illegal slice "
                       "count");
    append32be(Out, static_cast<uint32_t>(24 + 12 * P.Slices.size()));
    append32be(Out, P.ObjectId);
    append32be(Out, P.InputShndx);
    Out.push_back(P.StorageSpace);
    Out.push_back(0);                      // reserved8
    append16be(Out, 0);                    // reserved16
    append32be(Out, P.InputSize);
    append32be(Out, static_cast<uint32_t>(P.Slices.size()));
    for (const LinkerResult::PositionSlice &Sl : P.Slices) {
      append32be(Out, Sl.InputOffset);
      append32be(Out, Sl.FinalAddress);
      append32be(Out, Sl.Length);
    }
  }
  return true;
}

// The pure byte builder behind writeExecutable().  `Positions` is null (or
// empty) for a plain link, which keeps the output byte-identical to the
// pre-D2 writer; in audit mode it is the serialized positioning NOTE, placed
// after every existing non-load datum (4-byte zero padded, never reusing file
// bytes) with its section header appended as the LAST header (design §6.3).
// The existing load-section order and indices, the .shstrtab/.strtab/.symtab
// relative order, the program headers and every PT_LOAD file byte stay
// untouched.
bool buildExecutable(const LinkerResult &Result, bool KeepSymbols,
                     const std::vector<uint8_t> *Positions,
                     std::vector<uint8_t> &ELF, raw_ostream &Err) {
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
  // G11-D2: the positioning carrier's name is APPENDED to the .shstrtab name
  // tail (after .symtab when a symbol table exists), so a plain link adds
  // nothing at all -- not even the name string (design §6.3).
  uint32_t PositionsName = 0;
  const bool EmitPositions = Positions != nullptr && !Positions->empty();
  if (EmitPositions) {
    PositionsName = Names.size();
    StringRef PN = PositionsNoteSectionName;
    Names.insert(Names.end(), PN.bytes_begin(), PN.bytes_end());
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
  // G11-D2: the positioning NOTE data sits after every existing non-load
  // datum and before the section header table, zero padded to a 4-byte file
  // offset; it never shares file bytes with any other content (design §6.3).
  uint32_t PositionsOffset = 0;
  if (EmitPositions) {
    DataOffset = (DataOffset + 3) & ~uint64_t(3);
    if (DataOffset + Positions->size() > 0xffffffff)
      return fail(Err, "ET_EXEC output is too large");
    PositionsOffset = static_cast<uint32_t>(DataOffset);
    DataOffset += Positions->size();
  }
  uint64_t Shoff = DataOffset;
  // null + sparse load sections + names (+ .strtab + .symtab)
  // (+ .mcs251.placement.positions)
  uint64_t Shnum =
      Segments.size() + 2 + (EmitSyms ? 2u : 0) + (EmitPositions ? 1u : 0);
  // G11-D2 (design §3.6): re-verify the non-extended section-numbering
  // capability after the section count grew; never assume the old bound.
  if (Shnum > 0xffff)
    return fail(Err, "too many output sections for the non-extended section "
                     "numbering");
  if (Shoff + Shnum * ShdrSize > 0xffffffff)
    return fail(Err, "section header table is too large");

  ELF.clear();
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
  if (EmitPositions) {
    ELF.resize(PositionsOffset, 0);
    ELF.insert(ELF.end(), Positions->begin(), Positions->end());
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
  // G11-D2 (design §3.1): the positioning carrier's section header is the
  // LAST header.  Frozen shape: SHT_NOTE, flags 0, sh_addr 0, link/info 0,
  // raw sh_addralign 4, sh_entsize 0, sh_size = 20 + descsz with no tail
  // bytes inside the section.
  if (EmitPositions) {
    append32be(ELF, PositionsName);
    append32be(ELF, 7);                       // SHT_NOTE
    append32be(ELF, 0); append32be(ELF, 0);   // sh_flags, sh_addr
    append32be(ELF, PositionsOffset);
    append32be(ELF, Positions->size());
    append32be(ELF, 0);                       // sh_link
    append32be(ELF, 0);                       // sh_info
    append32be(ELF, 4);                       // sh_addralign (raw)
    append32be(ELF, 0);                       // sh_entsize
  }
  return true;
}

// The classic publication writer: build the bytes, write them to `<Path>.tmp`
// and rename over the target.  A plain link passes Positions=nullptr and gets
// the exact pre-D2 bytes and diagnostics.
bool writeExecutable(const LinkerResult &Result, bool KeepSymbols,
                     const std::vector<uint8_t> *Positions, StringRef Path,
                     raw_ostream &Err) {
  std::vector<uint8_t> ELF;
  if (!buildExecutable(Result, KeepSymbols, Positions, ELF, Err))
    return false;

  std::string Temp = (Path + ".tmp").str();
  std::error_code EC;
  {
    raw_fd_ostream OS(Temp, EC, sys::fs::OF_None);
    if (EC)
      return fail(Err, "cannot open temporary output " + Temp);
    OS.write(reinterpret_cast<const char *>(ELF.data()), ELF.size());
    OS.flush();
    // R5-7 (same class as the private writers): close explicitly and clear a
    // close error so the destructor cannot escalate it to a fatal LLVM ERROR.
    OS.close();
    if (OS.has_error()) {
      OS.clear_error();
      return fail(Err, "cannot write temporary output " + Temp);
    }
  }
  EC = sys::fs::rename(Temp, Path);
  if (EC) {
    sys::fs::remove(Temp);
    return fail(Err, "cannot replace output " + Path);
  }
  return true;
}

// G11-D review B8: pre-flight a publication destination.  The audit path
// writes its private artifacts, verifies, and only then publishes; if the
// report's directory does not exist, the failure must be found BEFORE the ELF
// is placed, otherwise a failed run leaves a brand-new ELF that looks like a
// successful product.
//
// G11-D review R2-7 (B8): the probe itself must be side-effect free on files
// THIS RUN DID NOT CREATE.
//   * A destination that already exists as a DIRECTORY can never be renamed
//     onto, so it is refused up front (the probe used to pass by writing
//     `<path>.tmp`, and the real ELF was replaced before the report's
//     rename failed).
//   * The writability probe uses a UNIQUE scratch name in the destination's
//     directory.  Probing through `<path>.tmp` truncated and deleted a
//     PRE-EXISTING file at that path even when the run then failed; the
//     publication temp path belongs to the real writers only.
bool probeDestination(StringRef Path, StringRef Kind, raw_ostream &Err) {
  if (sys::fs::is_directory(Path))
    return fail(Err, "cannot replace " + Kind + " " + Path);
  // G11-D review R3-4 (R2-7 regression): the writability probe below only
  // proves the parent directory can hold a NEW unique scratch name; it never
  // looked at the REAL staging path `<path>.tmp`.  A DIRECTORY sitting at
  // that path can never be opened for writing, and discovering that only
  // inside the publication write left whatever had already been published
  // (e.g. a replaced ELF) behind a failed run.  The staging path is checked
  // here, before the first byte of ANY artifact is published; the diagnostic
  // keeps the exact spelling the real writer would have produced.
  SmallString<256> Temp((Path + ".tmp").str());
  if (sys::fs::is_directory(Temp))
    return fail(Err, "cannot open temporary " + Kind + " " + Temp);
  SmallString<256> Model((Path + ".probe.%%%%%%%%").str());
  SmallString<256> ProbePath;
  int FD;
  if (std::error_code EC =
          sys::fs::createUniqueFile(Model, FD, ProbePath)) {
    // The user-visible diagnostic keeps the publication temp spelling (the
    // name the real writer would have used), so the probe's private scratch
    // name never leaks into the frozen diagnostic family.
    return fail(Err, "cannot open temporary " + Kind + " " + Path + ".tmp");
  }
  // R5-8: the probe file is owned by THIS run, so its close and removal are
  // checked.  A silent failure here used to leave a `.probe.*` file behind
  // while the link reported success; the diagnostic names the leftover path
  // instead of claiming there is no residue.
  if (std::error_code EC = sys::fs::closeFile(FD)) {
    std::error_code Rm = sys::fs::remove(ProbePath);
    std::string Msg =
        ("cannot close the " + Kind + " probe file " + ProbePath.str() +
         ": " + EC.message())
            .str();
    if (Rm)
      Msg += " (and it could not be removed either: " + Rm.message() + ")";
    return fail(Err, Msg);
  }
  if (std::error_code EC = sys::fs::remove(ProbePath))
    return fail(Err, "cannot remove the " + Kind + " probe file " +
                         ProbePath.str().str() + ": " + EC.message());
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
    // R5-7: the CLOSE is part of the write.  Without an explicit close the
    // stream destructor escalates a close failure to a fatal LLVM ERROR,
    // which skips the caller's clean-up (the private audit workspace would
    // survive).  Closing here, clearing the flag and returning a controlled
    // failure keeps the design §9 error path intact.
    OS.close();
    if (OS.has_error()) {
      // Handled here: clear the flag so the stream's destructor cannot
      // escalate to a fatal usage error that would skip the clean-up.
      OS.clear_error();
      return fail(Err, "cannot write temporary " + Kind + " " + Temp);
    }
  }
  EC = sys::fs::rename(Temp, Path);
  if (EC) {
    sys::fs::remove(Temp);
    return fail(Err, "cannot replace " + Kind + " " + Path);
  }
  return true;
}

// ==== G11-D2 (design §9): the audit publication transaction =============== //
//
// R4-3: the audit path must STAGE every user artifact, BACK UP every final
// destination that already exists, and only then PUBLISH (map -> report ->
// ELF).  A failure at any point restores every already-published target to
// its old content; a target that did not exist is removed again.  The
// staging path `<final>.tmp` is taken with EXCLUSIVE ownership: a path that
// already exists (any type) fails without being opened, so its content
// survives untouched.

struct Publication {
  std::string Final;      // The user-visible destination.
  std::string Temp;       // <final>.tmp, owned by this run.
  std::string Kind;       // "map" / "placement report" / "output".
  bool Existed = false;   // The final path existed before publishing.
  bool IsSymlink = false; // The old final path was a symlink.
  std::string LinkTarget; // The saved symlink target (restored as a link).
  std::string Backup;     // The same-directory hidden backup path.
  bool Staged = false;
  bool BackedUp = false;
  bool Published = false;
};

// Stage one artifact: write the already-determined bytes to `<final>.tmp`
// with O_EXCL semantics, checking open/write/flush/close.  Every failure
// removes the file this run created before reporting (design §9.4: a step-3
// failure leaves no owned file behind, and the diagnostic keeps naming it).
bool stagePublication(Publication &P, StringRef Bytes, raw_ostream &Err) {
  P.Temp = P.Final + ".tmp";
  int FD;
  if (std::error_code EC =
          sys::fs::openFileForWrite(P.Temp, FD, sys::fs::CD_CreateNew,
                                    sys::fs::OF_None)) {
    P.Temp.clear();
    return fail(Err, "cannot open temporary " + P.Kind + " " + P.Final +
                         ".tmp");
  }
  {
    raw_fd_ostream OS(FD, /*shouldClose=*/true);
    OS.write(Bytes.data(), Bytes.size());
    OS.flush();
    if (OS.has_error()) {
      std::string T = P.Temp;
      OS.close();
      // The error is handled here; clear it so ~raw_fd_ostream does not
      // escalate to a fatal usage error that would skip the clean-up below.
      OS.clear_error();
      sys::fs::remove(T);
      P.Temp.clear();
      return fail(Err, "cannot write temporary " + P.Kind + " " + T);
    }
    OS.close();
    if (OS.has_error()) {
      std::string T = P.Temp;
      OS.clear_error();
      sys::fs::remove(T);
      P.Temp.clear();
      return fail(Err, "cannot write temporary " + P.Kind + " " + T);
    }
  }
  // Byte-count check: a short write must never pass for a staged artifact.
  uint64_t Size = 0;
  if (std::error_code EC = sys::fs::file_size(P.Temp, Size)) {
    std::string T = P.Temp;
    sys::fs::remove(T);
    P.Temp.clear();
    return fail(Err, "cannot write temporary " + P.Kind + " " + T);
  }
  if (Size != Bytes.size()) {
    std::string T = P.Temp;
    sys::fs::remove(T);
    P.Temp.clear();
    return fail(Err, "cannot write temporary " + P.Kind + " " + T);
  }
  P.Staged = true;
  return true;
}

// Back up one final destination that exists.  The backup lives in the SAME
// directory under a hidden unique name; a plain file prefers a hard link
// (the old inode and content survive); when links are unsupported a full
// byte copy is written, flushed, closed and READ BACK for comparison before
// publication may start (design §9.2).  A symlink is saved as a link; a
// directory or other special file is refused before anything is replaced.
bool backupPublication(Publication &P, raw_ostream &Err) {
  sys::fs::file_status St;
  std::error_code EC = sys::fs::status(P.Final, St, /*Follow=*/false);
  if (EC == std::errc::no_such_file_or_directory) {
    P.Existed = false;
    return true; // The target is brand new; there is nothing to back up.
  }
  if (EC)
    return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                         EC.message());
  if (sys::fs::is_symlink_file(St)) {
    SmallString<256> Target;
    if (std::error_code EC = sys::fs::readlink(P.Final, Target))
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                           EC.message());
    P.IsSymlink = true;
    P.LinkTarget = Target.str().str();
    P.Existed = true;
    // R5-6: the in-memory target string alone is NOT a recoverable backup.
    // A persistent copy of the LINK ITSELF is created in the same directory
    // before anything is published, so a failed restore can keep and report
    // an artifact the user can recover from (design §9.5).
    SmallString<256> Dir = sys::path::parent_path(P.Final);
    if (Dir.empty())
      Dir = ".";
    SmallString<256> Model(Dir);
    sys::path::append(Model, "." + sys::path::filename(P.Final) +
                                  ".pubbk.%%%%%%%%");
    SmallString<256> LinkCopy;
    int LFD;
    if (std::error_code EC = sys::fs::createUniqueFile(Model, LFD, LinkCopy))
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                           EC.message());
    if (std::error_code EC = sys::fs::closeFile(LFD)) {
      sys::fs::remove(LinkCopy);
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                           EC.message());
    }
    sys::fs::remove(LinkCopy);
    if (std::error_code EC =
            sys::fs::create_symlink(P.LinkTarget, LinkCopy)) {
      sys::fs::remove(LinkCopy);
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                           EC.message());
    }
    P.Backup = LinkCopy.str().str();
    P.BackedUp = true;
    return true;
  }
  if (St.type() != sys::fs::file_type::regular_file)
    return fail(Err, "cannot replace " + P.Kind + " " + P.Final +
                         ": unsupported existing file type");
  P.Existed = true;
  SmallString<256> Dir = sys::path::parent_path(P.Final);
  if (Dir.empty())
    Dir = ".";
  SmallString<256> Model(Dir);
  sys::path::append(Model, "." + sys::path::filename(P.Final) +
                                ".pubbk.%%%%%%%%");
  SmallString<256> Probe;
  int FD;
  if (std::error_code EC = sys::fs::createUniqueFile(Model, FD, Probe))
    return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                         EC.message());
  sys::fs::closeFile(FD);
  sys::fs::remove(Probe);
  // Preferred: the same-directory hard link keeps the old inode.
  if (!sys::fs::create_hard_link(P.Final, Probe)) {
    P.Backup = Probe.str().str();
    P.BackedUp = true;
    return true;
  }
  // Fallback: the verified byte copy.
  SmallString<256> Copy;
  int CFD;
  if (std::error_code EC = sys::fs::createUniqueFile(Model, CFD, Copy))
    return fail(Err, "cannot back up " + P.Kind + " " + P.Final + ": " +
                         EC.message());
  {
    auto MB = MemoryBuffer::getFile(P.Final);
    if (!MB) {
      sys::fs::closeFile(CFD);
      sys::fs::remove(Copy);
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final);
    }
    raw_fd_ostream OS(CFD, /*shouldClose=*/true);
    OS.write((*MB)->getBufferStart(), (*MB)->getBufferSize());
    OS.flush();
    OS.close();
    if (OS.has_error()) {
      // Handled here: clear the flag so the destructor cannot escalate to a
      // fatal usage error that would skip the clean-up below.
      OS.clear_error();
      sys::fs::remove(Copy);
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final);
    }
    uint64_t Sz = 0;
    if (sys::fs::file_size(Copy, Sz) || Sz != (*MB)->getBufferSize()) {
      sys::fs::remove(Copy);
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final);
    }
    auto Back = MemoryBuffer::getFile(Copy);
    if (!Back || (*Back)->getBuffer() != (*MB)->getBuffer()) {
      sys::fs::remove(Copy);
      return fail(Err, "cannot back up " + P.Kind + " " + P.Final);
    }
  }
  P.Backup = Copy.str().str();
  P.BackedUp = true;
  return true;
}

// Restore one published target.  Returns false when the old state could NOT
// be re-established; the caller keeps going and reports at the end.
bool restorePublication(Publication &P) {
  if (!P.Published)
    return true;
  if (!P.Existed)
    return !sys::fs::remove(P.Final);
  // R5-6: both an ordinary file and a symlink now restore by renaming the
  // persistent same-directory backup over the published path.  For a symlink
  // that re-establishes the LINK itself; when the rename fails the backup is
  // deliberately KEPT (the caller reports its path, design §9.5).
  if (P.Backup.empty())
    return false;
  std::error_code EC = sys::fs::rename(P.Backup, P.Final);
  if (EC)
    return false;
  P.Backup.clear(); // Consumed by the successful restore.
  return true;
}

// G11-D: lexically absolutize under the link working directory, never through
// symlinks (contract §2 来源行粒度).  Byte-compatible with LinkerCore.cpp's
// lexicalAbsolutePath, so the report's `file` column and the verifier's
// expectation agree without either side trusting the other.
std::string lexicalAbsolute(StringRef P) {
  SmallString<256> Buf(P);
  if (sys::fs::make_absolute(Buf))
    return P.str();
  // make_absolute does NOT remove `.`/`..` components, so `./alias.elf` and
  // `alias.elf` came out as different strings and the report-vs-output
  // conflict was missed (G11-D review B8).  Normalize the lexical form first
  // (still no realpath: the report's `file` spelling stays lexical).
  sys::path::remove_dots(Buf, /*remove_dot_dot=*/true);
  return Buf.str().str();
}

// G11-D review B8: decide whether two user-supplied paths denote the SAME
// file.  A lexical comparison alone is not enough (`./alias.elf` vs
// `alias.elf`, or a symlinked directory alias).  The rule, strongest first:
//   1. both paths exist -> ask the file system (device + inode);
//   2. otherwise resolve the parent directory through the file system and
//      compare (parent realpath + filename), so a symlinked directory alias
//      is still recognized;
//   3. otherwise compare the normalized lexical absolute paths.
bool sameFilePath(StringRef A, StringRef B) {
  if (A.empty() || B.empty())
    return false;
  if (A == B)
    return true;
  bool ExistsA = sys::fs::exists(A);
  bool ExistsB = sys::fs::exists(B);
  if (ExistsA && ExistsB) {
    bool Same = false;
    if (!sys::fs::equivalent(A, B, Same))
      return Same;
    return false;
  }
  auto Resolved = [](StringRef P) -> std::string {
    SmallString<256> Full(P);
    if (sys::fs::make_absolute(Full))
      return P.str();
    std::string Name = sys::path::filename(Full).str();
    SmallString<256> Dir(sys::path::parent_path(Full));
    SmallString<256> RealDir;
    if (!sys::fs::real_path(Dir, RealDir, /*expand_tilde=*/false)) {
      if (!RealDir.empty() && RealDir.back() != '/')
        RealDir += '/';
      RealDir += Name;
      sys::path::remove_dots(RealDir, /*remove_dot_dot=*/true);
      return RealDir.str().str();
    }
    sys::path::remove_dots(Full, /*remove_dot_dot=*/true);
    return Full.str().str();
  };
  return Resolved(A) == Resolved(B);
}

// G11-D: everything the independent verifier needs that only the Driver knows
// (contract §1/§4.3).  Captured BEFORE `Options.Core` is moved into linkCore;
// the verifier treats all of it as a description of what was linked, never as
// ground truth -- it re-reads every artifact.
struct AuditContext {
  std::vector<std::string> Objects;   // Complete link input set, in order.
  std::vector<std::string> Manifests; // Every manifest actually consumed.
  std::vector<std::string> WindowArgs;
};

// G11-D: locate the verifier binary.  It ships next to this lld (LLD installs
// both into the same bindir), so the search is "sibling of the running
// executable" plus the documented MCS251_PLACEMENT_VERIFY override; PATH is
// deliberately not consulted, so a link can never silently pick up an
// unrelated tool with that name.
std::optional<std::string> findPlacementVerifier() {
  std::vector<std::string> Candidates;
  if (const char *Override = std::getenv("MCS251_PLACEMENT_VERIFY"))
    if (*Override)
      Candidates.push_back(Override);
  std::string Self = sys::fs::getMainExecutable(nullptr, nullptr);
  if (!Self.empty()) {
    SmallString<256> Dir(Self);
    sys::path::remove_filename(Dir);
    SmallString<256> Sibling(Dir);
    sys::path::append(Sibling, "mcs251-placement-verify");
    Candidates.push_back(Sibling.str().str());
  }
  for (const std::string &C : Candidates)
    if (sys::fs::can_execute(C))
      return C;
  return std::nullopt;
}

// G11-D: run the verifier as a CHILD PROCESS with an explicit argv (never
// through a shell).  Its own stderr is inherited, so the frozen
// `VERIFY FAIL...` body reaches the user byte-for-byte and the Driver adds no
// `mcs251-lld: error:` prefix to it (contract §1 失败/退出码/发布规则).
bool runPlacementVerifier(const AuditContext &Audit, StringRef Elf,
                          StringRef Report, StringRef Map, raw_ostream &Err) {
  auto Tool = findPlacementVerifier();
  if (!Tool)
    return fail(Err, "cannot execute mcs251-placement-verify: the verifier "
                     "binary is not available next to mcs251-lld");
  std::vector<std::string> Args;
  Args.push_back(*Tool);
  Args.push_back(("--elf=" + Elf).str());
  Args.push_back(("--report=" + Report).str());
  Args.push_back(("--link-map=" + Map).str());
  for (const std::string &O : Audit.Objects)
    Args.push_back("--object=" + O);
  for (const std::string &M : Audit.Manifests)
    Args.push_back("--placement-manifest=" + M);
  for (const std::string &W : Audit.WindowArgs)
    Args.push_back(W);
  std::vector<StringRef> Argv;
  Argv.reserve(Args.size());
  for (const std::string &A : Args)
    Argv.push_back(A);
  std::string ErrMsg;
  bool ExecutionFailed = false;
  int RC = sys::ExecuteAndWait(*Tool, Argv, std::nullopt, {}, 0, 0, &ErrMsg,
                               &ExecutionFailed);
  if (RC < 0 || ExecutionFailed)
    return fail(Err, "cannot execute mcs251-placement-verify: " +
                         (ErrMsg.empty() ? std::string("failed to start the "
                                                       "verifier process")
                                         : ErrMsg));
  // A non-zero verifier status is a verification failure whose entire body the
  // child already wrote; the Driver only propagates the failure.
  return RC == 0;
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
  bool ReportGiven = false;
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
    } else if (A == "--verify-placement") {
      // G11-D (contract §1): link, then verify the ACTUAL serialized ELF and
      // report with the independent child process.  Implies KeepSymbols (the
      // verifier needs the final symbol table) and does NOT imply a report
      // file: verify-only uses a private temporary report that is never left
      // behind.
      O.AuditVerify = true;
      O.Core.KeepSymbols = true;
    } else if (A == "--placement-report" ||
               A.starts_with("--placement-report=")) {
      // G11-D (contract §1): write the placement merge contract companion
      // text.  Implies KeepSymbols.  Requesting the report does NOT verify,
      // so corrupting only the source hash still links successfully.
      if (ReportGiven)
        return fail(Err, "duplicate --placement-report");
      ReportGiven = true;
      auto V = take("--placement-report");
      if (!V || V->empty())
        return fail(
            Err, "invalid --placement-report: expected a non-empty file path");
      O.ReportPath = V->str();
      O.AuditReport = true;
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
      O.EdataEndGiven = true;
    } else if (auto V = take("--iram-size")) {
      auto N = number(*V);
      if (!N)
        return fail(Err, "invalid --iram-size");
      O.Core.IramSize = *N;
      O.IramSizeGiven = true;
    } else if (auto V = take("--stack-size")) {
      auto N = number(*V);
      if (!N)
        return fail(Err, "invalid --stack-size");
      O.Core.StackSize = *N;
    } else if (auto V = take("--xdata-size")) {
      // X3: board-level XDATA capacity in bytes; 1..0x1000000 keeps
      // base+size inside the 24-bit space.  The default (absent) keeps the
      // always-on 24-bit range check only.
      auto N = number(*V);
      if (!N || *N == 0 || uint64_t(*N) > 0x1000000)
        return fail(Err, "invalid --xdata-size (expected 1..0x1000000)");
      O.Core.XdataSize = *N;
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
      // BT14: BITINIT is the CODE area holding the lld-synthesized bit-init
      // table consumed by the bit-aware CRT's __mcs251_bit_init walker.
      static const char *CodeAreas[] = {"HOME", "VECS", "BOOT", "CSEG",
                                        "XINIT", "XDATA_INIT", "BITINIT"};
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
      // X3: per-object XSEG sections may be pinned individually by their own
      // section name (the core allocator honors a per-section start).
      if (AreaName.starts_with(".mcs251.XSEG"))
        IsCode = true;
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
    } else if (auto V = take("--placement-manifest")) {
      // G11 (design §7): the manifest is a plain text file of
      // `place <stable> <A> [align=N] [size=N] [retain] [noinit] [bind]
      // [data|xdata|code]` rows (with `#` comments).  The shell only moves
      // bytes: the line grammar, the malformed-entry diagnostic and every
      // merge decision belong to mergePlacement() in the core.  A missing
      // or unreadable file is a hard error, never an empty manifest.
      auto MB = MemoryBuffer::getFileOrSTDIN(*V);
      if (!MB)
        return fail(Err, "cannot read --placement-manifest " + *V);
      // G11-D provenance (contract §4.2/§2): record the lexically absolute
      // manifest path and the 1-based physical line number with every row,
      // so the report can name a manifest constraint's origin.
      std::string AbsPath = lexicalAbsolute(*V);
      StringRef Contents = MB->get()->getBuffer();
      uint32_t LineNo = 0;
      while (!Contents.empty()) {
        auto [Line, Rest] = Contents.split('\n');
        ++LineNo;
        O.Core.PlacementManifest.push_back({AbsPath, LineNo, Line.str()});
        Contents = Rest;
      }
      if (O.Core.PlacementManifest.empty())
        return fail(Err, "--placement-manifest " + *V + " has no entries");
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
             "[--placement-report=<path>] [--verify-placement] "
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
  // G11-D audit validation (contract §1).  Audit output must be a real
  // published artifact, and the report file must be a distinct path: an audit
  // that overwrote an input or the ELF/map would destroy the very evidence it
  // claims to check.
  if (O.AuditReport || O.AuditVerify) {
    if (O.Core.PrintInput)
      return fail(Err,
                  "placement audit options cannot be combined with --print-input");
    if (O.AuditReport) {
      // G11-D review B8: same-file detection, not string equality after
      // make_absolute (`--placement-report=./alias.elf -o alias.elf` used to
      // pass and overwrite the ELF with report text).
      auto Same = [&](StringRef P) {
        return sameFilePath(P, O.ReportPath);
      };
      bool Conflict = Same(O.Output);
      if (!Conflict && !O.Map.empty())
        Conflict = Same(O.Map);
      for (const std::string &I : O.Core.Inputs)
        if (!Conflict)
          Conflict = Same(I);
      for (const LinkerConfig::ManifestEntry &ME : O.Core.PlacementManifest)
        if (!Conflict)
          Conflict = Same(ME.Path);
      if (Conflict)
        return fail(Err,
                    "placement report path conflicts with another input or "
                    "output");
    }
    // G11-D review R2-7 (B8): the final-path conflict matrix must cover the
    // TEMPORARY paths too.  Publication writes every artifact through
    // `<path>.tmp` and renames it over the target, so an artifact whose FINAL
    // path is another artifact's TEMP path is silently renamed away by that
    // artifact's publish: `-o product.tmp --placement-report=product` used to
    // return 0 and delete product.tmp (the report's temp WAS the ELF output).
    // Each published artifact contributes its final and its temp path; any
    // same-file pair across the matrix is a conflict.
    struct PubPath {
      std::string Path;
      bool IsReport;
      bool IsTemp;
    };
    SmallVector<PubPath, 6> Pubs;
    auto Add = [&](StringRef P, bool IsReport) {
      Pubs.push_back({P.str(), IsReport, false});
      Pubs.push_back({(P + ".tmp").str(), IsReport, true});
    };
    Add(O.Output, false);
    if (!O.Map.empty())
      Add(O.Map, false);
    if (O.AuditReport)
      Add(O.ReportPath, true);
    for (size_t I = 0; I != Pubs.size(); ++I)
      for (size_t J = I + 1; J != Pubs.size(); ++J)
        if (sameFilePath(Pubs[I].Path, Pubs[J].Path)) {
          if (Pubs[I].IsReport || Pubs[J].IsReport)
            return fail(Err,
                        "placement report path conflicts with another input "
                        "or output");
          return fail(Err,
                      "placement audit publication paths conflict: an output "
                      "path collides with another artifact's temporary path");
        }
    // G11-D review R3-5 (R2-7 residue): the matrix above only compared
    // artifacts with each other -- the INPUT side still compared the report's
    // FINAL path alone.  But publication writes through `<path>.tmp`, so an
    // input named `source.tmp` with `--placement-report=source` was consumed
    // by the report's staging write + rename and silently vanished.  Every
    // publication STAGING path is now compared against the complete input
    // set (objects and manifests) before a single byte is written.
    for (const PubPath &P : Pubs) {
      if (!P.IsTemp)
        continue; // Final paths vs inputs: the report final is covered above;
                  // the ELF/map finals keep their documented CLI semantics.
      auto Hits = [&](StringRef In) { return sameFilePath(P.Path, In); };
      bool Hit = false;
      for (const std::string &In : O.Core.Inputs)
        if (Hits(In)) {
          Hit = true;
          break;
        }
      if (!Hit)
        for (const LinkerConfig::ManifestEntry &ME : O.Core.PlacementManifest)
          if (Hits(ME.Path)) {
            Hit = true;
            break;
          }
      if (Hit) {
        if (P.IsReport)
          return fail(Err,
                      "placement report path conflicts with another input "
                      "or output");
        return fail(Err,
                    "placement audit publication paths conflict: an output "
                    "temporary path collides with an input file");
      }
    }
  }
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
  // G11-D (contract §1): capture every locating input BEFORE the core takes
  // ownership: the complete object list, the manifests actually consumed and
  // the board-level window numbers as given.  The verifier re-reads all of
  // them; the Driver never hands over Result.Placement as evidence.
  const bool Audit = Options.AuditReport || Options.AuditVerify;
  // G11-D2 (design §1.3/§6.1): the positioning carrier is emitted exactly when
  // an audit option is enabled AND output is actually generated.  A plain
  // link (and a keep-symbols-only link) never collects or emits anything.
  if (Audit)
    Options.Core.CollectPositions = true;
  AuditContext AuditCtx;
  if (Audit) {
    for (const std::string &P : Options.Core.Inputs)
      AuditCtx.Objects.push_back(lexicalAbsolute(P));
    for (const LinkerConfig::ManifestEntry &ME : Options.Core.PlacementManifest)
      if (std::find(AuditCtx.Manifests.begin(), AuditCtx.Manifests.end(),
                    ME.Path) == AuditCtx.Manifests.end())
        AuditCtx.Manifests.push_back(ME.Path);
    if (Options.Core.FlashGate) {
      AuditCtx.WindowArgs.push_back("--flash-base=" +
                                    std::to_string(Options.Core.FlashBase));
      AuditCtx.WindowArgs.push_back("--flash-size=" +
                                    std::to_string(Options.Core.FlashSize));
    }
    if (Options.Core.XdataSize)
      AuditCtx.WindowArgs.push_back("--xdata-size=" +
                                    std::to_string(Options.Core.XdataSize));
    if (Options.EdataEndGiven)
      AuditCtx.WindowArgs.push_back("--edata-end=" +
                                    std::to_string(Options.Core.EdataEnd));
    if (Options.IramSizeGiven)
      AuditCtx.WindowArgs.push_back("--iram-size=" +
                                    std::to_string(Options.Core.IramSize));
    for (const auto &P : Options.Core.AreaStarts)
      AuditCtx.WindowArgs.push_back("--area-start=" + P.first + "=" +
                                    std::to_string(P.second));
    for (const Range &P : Options.Core.ReservedData)
      AuditCtx.WindowArgs.push_back("--reserve-data=" +
                                    std::to_string(P.Start) + "," +
                                    std::to_string(P.End - P.Start));
  }
  LinkerResult Result;
  if (!linkCore(std::move(Options.Core), Result, Err))
    return false;
  // E2: reentrancy warnings never fail the link; they go to stderr ahead of
  // any output writing.
  if (!Result.Diagnostics.empty())
    Err << Result.Diagnostics;
  if (DisableOutput) {
    // Contract §1: a requested audit must never be reported as success just
    // because this invocation produces no output.
    if (Audit)
      return fail(Err, "placement audit requires output generation");
    return true;
  }
  if (PrintInput) {
    Out << Result.InputReport;
    Out.flush();
    return true;
  }
  if (!Audit) {
    if (!Options.Map.empty() && !writeText(Options.Map, Result.Map, "map", Err))
      return false;
    return writeExecutable(Result, KeepSymbols, /*Positions=*/nullptr,
                           Options.Output, Err);
  }

  // G11-D2: the positioning NOTE is serialized ONCE from the collected
  // result, so the private verification ELF and every later published copy
  // are the same bytes by construction (design §9.3).
  std::vector<uint8_t> PositionsNote;
  if (!serializePositionsNote(Result, PositionsNote, Err))
    return false;

  // G11-D audit (contract §1).  The report text is serialized ONCE from the
  // delivered interface: when both options are given, the verifier checks the
  // very bytes that are later published to the user path -- never a second,
  // differently-sourced contract.
  std::string ReportText;
  raw_string_ostream ReportOS(ReportText);
  serializePlacementReport(Result, ReportOS);
  ReportOS.flush();

  // Private scratch space, never a sibling of the user's output: an audit that
  // fails must not leave a brand-new path that looks like a successful
  // artifact, and an OLD file at the target path is neither deleted nor
  // replaced by this run's product.
  SmallString<256> TmpDir;
  if (std::error_code EC =
          sys::fs::createUniqueDirectory("mcs251-placement-audit", TmpDir))
    return fail(Err, "cannot create the placement audit workspace");
  struct Scratch {
    SmallString<256> Dir;
    ~Scratch() { sys::fs::remove_directories(Dir); }
  } Cleanup{TmpDir};
  SmallString<256> TmpReportBuf(TmpDir), TmpMapBuf(TmpDir);
  sys::path::append(TmpReportBuf, "out.placement");
  sys::path::append(TmpMapBuf, "out.map");

  // G11-D2 (design §9.3/§9.4 step 1): the published bytes are DETERMINED
  // ONCE.  The ELF is serialized a single time; the private verification
  // copy and every later staging write reuse those exact bytes -- never a
  // second serializer run.
  std::vector<uint8_t> ElfBytes;
  if (!buildExecutable(Result, KeepSymbols, &PositionsNote, ElfBytes, Err))
    return false;
  SmallString<256> TmpElfBuf(TmpDir);
  sys::path::append(TmpElfBuf, "out.elf");
  {
    int FD;
    if (std::error_code Open =
            sys::fs::openFileForWrite(TmpElfBuf, FD, sys::fs::CD_CreateNew,
                                      sys::fs::OF_None))
      return fail(Err, "cannot open temporary output " + TmpElfBuf + ": " +
                           Open.message());
    // R5-7: this private copy is written through a raw_fd_ostream with an
    // EXPLICIT close, so a close failure is a controlled error on the §9
    // clean-up path instead of a fatal LLVM ERROR that skips the workspace
    // removal.
    raw_fd_ostream OS(FD, /*shouldClose=*/true);
    OS.write(reinterpret_cast<const char *>(ElfBytes.data()), ElfBytes.size());
    OS.flush();
    OS.close();
    if (OS.has_error()) {
      OS.clear_error();
      return fail(Err, "cannot write temporary output " + TmpElfBuf);
    }
  }
  if (!writeText(TmpReportBuf, ReportText, "placement report", Err))
    return false;
  // The map is always produced for the verifier, even when the user asked for
  // none: it is the locating evidence for V13/V14/V16/V18 (contract §4.3).
  if (!writeText(TmpMapBuf, Result.Map, "map", Err))
    return false;
  if (Options.AuditVerify &&
      !runPlacementVerifier(AuditCtx, TmpElfBuf, TmpReportBuf, TmpMapBuf, Err))
    return false;

  // (a) Pre-flight EVERY destination before the first byte is published
  //     (review B8): the report path pointing into a missing directory used
  //     to be discovered after a fresh ELF had already been placed.
  //     R3-4 adds the real staging path `<path>.tmp` to the pre-flight, so a
  //     directory parked there fails before any artifact is touched.
  if (!probeDestination(Options.Output, "output", Err))
    return false;
  if (!Options.Map.empty() &&
      !probeDestination(Options.Map, "map", Err))
    return false;
  if (Options.AuditReport &&
      !probeDestination(Options.ReportPath, "placement report", Err))
    return false;

  // (b) G11-D2 (design §9.4): the publication transaction.  Stage every
  //     artifact FIRST (exclusive ownership of `<final>.tmp`), then back up
  //     every existing final path, then publish map -> report -> ELF.  A
  //     failure anywhere before the first rename leaves every final path
  //     untouched; a rename failure restores the already-published targets
  //     in reverse order.
  std::vector<Publication> Pubs;
  auto AddPub = [&](StringRef Final, StringRef Kind, StringRef Bytes) {
    Pubs.push_back({});
    Pubs.back().Final = Final.str();
    Pubs.back().Kind = Kind.str();
    if (!stagePublication(Pubs.back(), Bytes, Err))
      return false;
    return true;
  };
  // Failure clean-up (design §9.4): every file THIS run owns is removed --
  // the staging temps that were never published AND the backups of targets
  // that were never published (their final paths stayed untouched).  A backup
  // whose restore failed is deliberately kept: it is the only copy of the old
  // state (design §9.5).
  auto CleanOwned = [&]() {
    for (const Publication &P : Pubs) {
      if (P.Staged && !P.Published)
        sys::fs::remove(P.Temp);
      if (!P.Published && !P.Backup.empty())
        sys::fs::remove(P.Backup);
    }
  };
  if (!Options.Map.empty() &&
      !AddPub(Options.Map, "map", Result.Map)) {
    CleanOwned();
    return false;
  }
  if (Options.AuditReport &&
      !AddPub(Options.ReportPath, "placement report", ReportText)) {
    CleanOwned();
    return false;
  }
  {
    StringRef ElfView(reinterpret_cast<const char *>(ElfBytes.data()),
                      ElfBytes.size());
    if (!AddPub(Options.Output, "output", ElfView)) {
      CleanOwned();
      return false;
    }
  }
  // Backups: still no final path has been touched.
  for (Publication &P : Pubs)
    if (!backupPublication(P, Err)) {
      CleanOwned();
      return false;
    }
  // Publish: map -> report -> ELF.
  for (Publication &P : Pubs) {
    std::error_code EC = sys::fs::rename(P.Temp, P.Final);
    if (EC) {
      sys::fs::remove(P.Temp);
      P.Staged = false;
      // Reverse-order recovery of every already-published target.
      bool RestoredAll = true;
      SmallVector<std::string, 3> Unrestored;
      for (auto It = Pubs.rbegin(); It != Pubs.rend(); ++It)
        if (It->Published && !restorePublication(*It)) {
          RestoredAll = false;
          Unrestored.push_back(It->Final);
        }
      CleanOwned();
      if (!RestoredAll) {
        Err << "mcs251-lld: error: cannot replace " << P.Kind << " "
            << P.Final << " and the previous state could not be fully "
               "restored; unrestored targets (retained backups):\n";
        for (const std::string &U : Unrestored) {
          Err << "  " << U;
          for (const Publication &Q : Pubs)
            if (Q.Final == U && !Q.Backup.empty() &&
                sys::fs::exists(Q.Backup))
              Err << " (backup: " << Q.Backup << ")";
          Err << "\n";
        }
        Err.flush();
      } else {
        (void)fail(Err, "cannot replace " + P.Kind + " " + P.Final);
      }
      return false;
    }
    P.Published = true;
  }
  // (c) Committed: the last rename succeeded, so cleanup failures are
  //     reported but never turn the publication into a failure (design
  //     §9.5); the retained paths are named.
  for (const Publication &P : Pubs)
    if (!P.Backup.empty() && sys::fs::exists(P.Backup) &&
        sys::fs::remove(P.Backup))
      Err << "mcs251-lld: warning: cannot remove the publication backup "
          << P.Backup << "\n";
  if (std::error_code EC = sys::fs::remove_directories(TmpDir))
    Err << "mcs251-lld: warning: cannot remove the placement audit "
           "workspace "
        << TmpDir << "\n";
  Err.flush();
  return true;
}

} // namespace lld::mcs251
