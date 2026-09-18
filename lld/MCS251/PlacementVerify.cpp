//===- PlacementVerify.cpp - independent placement verifier ----------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// G11-D (design §3.4, contract §3): the numbered independent checks V1-V18.
//
// Independence rules honoured here (contract §2.4/§4):
//   * the ELF/NOTE/manifest/report readers are written from the frozen byte
//     layouts; no producer code is linked and no producer table is consulted;
//   * the semantic merge is re-implemented from the design's merge table, not
//     imported (no shared merge oracle);
//   * the mark data (`H_source` / `H_report`) is only ever recomputed here;
//     the verifier never treats a stored hash as evidence;
//   * final positions come from the link map as CANDIDATES and are cross
//     checked against the final symtab, its loadable ranges and the input
//     spans.  A position that cannot be uniquely located is a FAIL, never a
//     skipped check.
//
// Capability is present in this tree, so a compile/link/parse/locate/verify
// failure is a FAIL.  Only spec-defined non-applicabilities are exempt:
// NOBITS entities have no CODE bytes, and a bind function's size is
// deliberately unchecked.  Those are not SKIPs.
//===----------------------------------------------------------------------===//

#include "PlacementVerify.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;

namespace lld::mcs251::placementverify {
namespace {

// ---- frozen constants (mirrored from the design, never from the producer) --
constexpr uint16_t EM_MCS251 = 0x9999;
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t SHT_PROGBITS = 1;
constexpr uint16_t SHT_SYMTAB = 2;
constexpr uint16_t SHT_STRTAB = 3;
constexpr uint16_t SHT_RELA = 4;
constexpr uint16_t SHT_NOTE = 7;
constexpr uint16_t SHT_NOBITS = 8;
constexpr uint64_t SHF_WRITE = 0x1;
constexpr uint64_t SHF_ALLOC = 0x2;
constexpr uint64_t SHF_EXECINSTR = 0x4;
constexpr uint64_t SHF_GNU_RETAIN = 0x200000;
constexpr uint16_t SHN_ABS = 0xfff1;
// ELF32 reserved section-index floor (SHN_LORESERVE).  Every index at or above
// it names a reserved pseudo-section (SHN_ABS, SHN_COMMON, SHN_XINDEX, ...),
// never an ordinary section table entry.
constexpr uint16_t SHN_LORESERVE = 0xff00;
constexpr uint8_t STB_LOCAL = 0, STB_GLOBAL = 1;
constexpr uint8_t STT_NOTYPE = 0, STT_OBJECT = 1, STT_FUNC = 2,
                  STT_SECTION = 3;

constexpr uint8_t PSC_AS0_DATA = 0, PSC_XDATA = 1, PSC_CODE = 2;
constexpr uint8_t PE_OBJECT = 0, PE_FUNCTION = 1;
constexpr uint8_t PO_OWNED = 0, PO_BIND = 1;
constexpr uint32_t FlagRetain = 1, FlagNoInit = 2;

constexpr uint32_t PlacementNoteNameSize = 7;
constexpr uint32_t PlacementNoteType = 1;
constexpr uint32_t PlacementNamesType = 2;
constexpr uint32_t PlacementNamesVersion = 1;
constexpr uint8_t PlacementSchemaVersion = 1;

constexpr StringRef PlacementNoteSectionName = ".mcs251.placement";
constexpr StringRef NamesNoteSectionName = ".mcs251.placement.names";
constexpr StringRef FixedSectionPrefix = ".mcu.fixed.";

// G11-D2 (design §3): the versioned positioning carrier in the FINAL ELF.
// Envelope: namesz=7, name "MCS251\0" + one zero pad byte, NOTE type 3.
// Descriptor: position_version=1, object/record counts, reserved 0, a
// 32-byte-per-object SHA-256 fingerprint table, then the strictly
// (object_id, input_shndx)-sorted main records with their canonical slices.
constexpr StringRef PositionsNoteSectionName = ".mcs251.placement.positions";
constexpr uint32_t PositionsNoteType = 3;
constexpr uint32_t PositionsVersion = 1;
// The processor flag range bits classifySection accepts for ALLOC sections
// (mirrored here from the producer's vocabulary, never included from it).
constexpr uint64_t SHF_MCS251_OVERLAY = 0x10000000ull;
constexpr uint64_t SHF_MCS251_EDATA_MOVABLE = 0x20000000ull;
constexpr uint64_t SHF_MCS251_XSEG_SPLIT = 0x40000000ull;

// MCS251 relocation numbers (ELFRelocs/MCS251.def).
constexpr uint32_t R_NONE = 0, R_16 = 1, R_24 = 2, R_LO8 = 3, R_MID8 = 4,
                   R_HI8 = 5, R_PC8 = 6, R_J16 = 7, R_J11 = 8;

static std::string hex0x(uint32_t V) { return "0x" + Twine::utohexstr(V).str(); }
// Design 10.4: the range diagnostics must print the WIDENED true end,
// never the wrapped-around address a u32 truncation would show.
static std::string hex0x(uint64_t V) { return "0x" + Twine::utohexstr(V).str(); }

// Lexical absolutization under the link working directory (no realpath),
// matching the report's `file` spelling rule.
// G11-D review R2-6 (B8): `.` components are removed lexically on BOTH sides
// of every path comparison the verifier makes (map row file columns vs
// `--object` args).  The Driver normalizes the paths it hands over, and the
// producer normalizes the report's provenance the same way, so `./r.o` and
// `r.o` are one object here too -- without this, a map row spelled `./r.o`
// could never be tied to the normalized `--object=/cwd/r.o`.
static std::string absPath(StringRef P) {
  SmallString<256> Buf(P);
  if (sys::fs::make_absolute(Buf))
    return P.str();
  sys::path::remove_dots(Buf, /*remove_dot_dot=*/true);
  return Buf.str().str();
}

// Split on an exact multi-byte delimiter.  `StringRef::split` with a
// StringRef separator matches the literal SEQUENCE (it is not a character
// set in this tree -- see splitWhitespace below), so " | " would have to be
// matched literally anyway; this helper makes the intent explicit and is used
// by the report parser, whose separator is exactly the three frozen bytes.
static void splitExactDelim(StringRef Line, StringRef Delim,
                            SmallVectorImpl<StringRef> &Out) {
  Out.clear();
  size_t Pos = 0;
  while (true) {
    size_t Next = Line.find(Delim, Pos);
    if (Next == StringRef::npos) {
      Out.push_back(Line.substr(Pos));
      return;
    }
    Out.push_back(Line.substr(Pos, Next - Pos));
    Pos = Next + Delim.size();
  }
}

// ---- a minimal, self-contained ELF32 big-endian reader -------------------- //

static bool rd16(const std::vector<uint8_t> &B, size_t Off, uint16_t &Out) {
  if (Off + 2 > B.size())
    return false;
  Out = uint16_t(B[Off] << 8 | B[Off + 1]);
  return true;
}
static bool rd32(const std::vector<uint8_t> &B, size_t Off, uint32_t &Out) {
  if (Off + 4 > B.size())
    return false;
  Out = uint32_t(B[Off]) << 24 | uint32_t(B[Off + 1]) << 16 |
        uint32_t(B[Off + 2]) << 8 | uint32_t(B[Off + 3]);
  return true;
}
static bool rdBytes(const std::vector<uint8_t> &B, size_t Off, size_t N,
                    std::vector<uint8_t> &Out) {
  if (Off + N > B.size())
    return false;
  Out.assign(B.begin() + Off, B.begin() + Off + N);
  return true;
}

struct Section {
  uint32_t NameOff = 0;
  std::string Name;
  uint16_t Type = 0;
  // G11-D2 (design §3.1): the RAW 32-bit sh_type / sh_addralign words.  The
  // positioning carrier's shape must be judged on the raw values, never on
  // the truncated 16-bit type or the zero-normalized alignment.
  uint32_t RawType = 0;
  uint32_t RawAlign = 0;
  uint64_t Flags = 0;
  uint32_t Addr = 0, Offset = 0, Size = 0, Link = 0, Info = 0, Align = 1,
           EntSize = 0;
};

struct Symbol {
  std::string Name;
  uint32_t Value = 0, Size = 0;
  uint8_t Info = 0, Other = 0;
  uint16_t Shndx = 0;
  uint8_t Bind() const { return uint8_t(Info >> 4); }
  uint8_t Type() const { return uint8_t(Info & 0xf); }
};

struct Reloc {
  uint32_t Offset = 0, Type = 0, SymIdx = 0;
  int32_t Addend = 0;
};

struct LoadSeg {
  uint32_t VAddr = 0, FileOff = 0, FileSz = 0, MemSz = 0;
};

struct ElfFile {
  std::string Path;
  std::vector<uint8_t> Buf;
  bool Ok = false;
  std::string Reason;
  uint16_t Type = 0, Machine = 0;
  // G11-D2: the table locations, kept for the carrier's file-range
  // non-reuse check (design §3.1).
  uint32_t Phoff = 0, Shoff = 0;
  uint16_t Phnum = 0;
  std::vector<Section> Secs;
  std::vector<Symbol> Syms;
  std::string Strtab;
  int SymtabIndex = -1;
  std::vector<LoadSeg> Loads;

  const Section *findByName(StringRef N) const {
    for (const Section &S : Secs)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
  bool sectionData(const Section &S, std::vector<uint8_t> &Out) const {
    return rdBytes(Buf, S.Offset, S.Size, Out);
  }
};

static bool parseElf(ElfFile &F) {
  const std::vector<uint8_t> &B = F.Buf;
  if (B.size() < 52 || B[0] != 0x7f || B[1] != 'E' || B[2] != 'L' ||
      B[3] != 'F') {
    F.Reason = "not an ELF file";
    return false;
  }
  if (B[4] != 1) {
    F.Reason = "not ELF32";
    return false;
  }
  if (B[5] != 2) {
    F.Reason = "not big-endian";
    return false;
  }
  uint16_t EType = 0, EMach = 0, Shentsize = 0, Shnum = 0, Shstrndx = 0;
  uint16_t Phentsize = 0, Phnum = 0;
  uint32_t Shoff = 0, Phoff = 0;
  if (!rd16(B, 16, EType) || !rd16(B, 18, EMach) || !rd32(B, 32, Shoff) ||
      !rd32(B, 28, Phoff) || !rd16(B, 46, Shentsize) || !rd16(B, 48, Shnum) ||
      !rd16(B, 50, Shstrndx) || !rd16(B, 42, Phentsize) || !rd16(B, 44, Phnum)) {
    F.Reason = "truncated ELF header";
    return false;
  }
  F.Type = EType;
  F.Machine = EMach;
  if (Shoff == 0 || Shnum == 0 || Shentsize < 40) {
    F.Reason = "no section header table";
    return false;
  }
  if (!Shstrndx || Shstrndx >= Shnum) {
    F.Reason = "invalid section header string table index";
    return false;
  }
  uint64_t TableEnd = uint64_t(Shoff) + uint64_t(Shentsize) * Shnum;
  if (TableEnd > B.size()) {
    F.Reason = "section header table out of bounds";
    return false;
  }
  auto sectAt = [&](unsigned I, Section &S) -> bool {
    size_t Off = Shoff + size_t(I) * Shentsize;
    uint32_t V[8];
    for (unsigned K = 0; K != 8; ++K)
      if (!rd32(B, Off + K * 4, V[K]))
        return false;
    S.NameOff = V[0];
    S.Type = uint16_t(V[1]);
    S.RawType = V[1];
    S.Flags = V[2];
    S.Addr = V[3];
    S.Offset = V[4];
    S.Size = V[5];
    S.Link = V[6];
    S.Info = V[7];
    uint32_t A = 0, E = 0;
    if (!rd32(B, Off + 32, A) || !rd32(B, Off + 36, E))
      return false;
    S.Align = A ? A : 1;
    S.RawAlign = A;
    S.EntSize = E;
    return true;
  };
  F.Secs.resize(Shnum);
  for (unsigned I = 0; I != Shnum; ++I)
    if (!sectAt(I, F.Secs[I])) {
      F.Reason = "truncated section header table";
      return false;
    }
  F.Shoff = Shoff;
  F.Phoff = Phoff;
  F.Phnum = Phnum;
  const Section &Shstr = F.Secs[Shstrndx];
  if (uint64_t(Shstr.Offset) + Shstr.Size > B.size()) {
    F.Reason = "section header string table out of bounds";
    return false;
  }
  auto nameOf = [&](uint32_t Off) -> std::string {
    if (Off >= Shstr.Size)
      return std::string();
    size_t Base = Shstr.Offset + Off;
    size_t End = Base;
    while (End < size_t(Shstr.Offset) + Shstr.Size && B[End] != 0)
      ++End;
    return std::string(reinterpret_cast<const char *>(B.data() + Base),
                       End - Base);
  };
  for (Section &S : F.Secs)
    S.Name = nameOf(S.NameOff);
  // G11-D review B7: the program header table is structural evidence for V1
  // and for every load-image read (V18/V16).  A program header that cannot be
  // read is MALFORMED, never a row to skip: skipping it would drop a whole
  // loadable segment from the image map and turn a locating failure into a
  // green check (e.g. e_phnum = 65535 used to be accepted).
  if (Phnum != 0) {
    if (Phentsize != 32) {
      F.Reason = "invalid program header entry size";
      return false;
    }
    if (uint64_t(Phoff) + uint64_t(Phentsize) * Phnum > B.size()) {
      F.Reason = "program header table out of bounds";
      return false;
    }
  }
  for (unsigned I = 0; I != Phnum; ++I) {
    size_t Off = Phoff + size_t(I) * Phentsize;
    uint32_t PType = 0, POff = 0, VAddr = 0, FileSz = 0, FileSzMem = 0,
             Flg = 0;
    if (!rd32(B, Off, PType) || !rd32(B, Off + 4, POff) ||
        !rd32(B, Off + 8, VAddr) || !rd32(B, Off + 16, FileSz) ||
        !rd32(B, Off + 20, FileSzMem) || !rd32(B, Off + 24, Flg)) {
      F.Reason = "truncated program header table";
      return false;
    }
    if (PType == 1) {
      if (uint64_t(POff) + FileSz > B.size()) {
        F.Reason = "loadable segment out of bounds";
        return false;
      }
      F.Loads.push_back({VAddr, POff, FileSz, FileSzMem});
    } else if (PType == 4) {
      // R5-9 (design §3.1): the final ELF must not create a PT_NOTE.  Before
      // this check an ADDED PT_NOTE pointing at the positioning carrier (or
      // anything else) was silently ignored, so the frozen output shape was
      // not actually enforced.
      F.Reason = "unexpected PT_NOTE in the final ELF";
      return false;
    }
  }
  for (unsigned I = 0; I != F.Secs.size(); ++I)
    if (F.Secs[I].Type == SHT_SYMTAB) {
      F.SymtabIndex = int(I);
      break;
    }
  F.Ok = true;
  return true;
}

// Load the symbol table.  `requireSymtab` drives V1's frozen diagnostic; the
// input objects must also carry one (for association and relocations).
static bool loadSymtab(ElfFile &F, bool requireSymtab, std::string &Why) {
  if (F.SymtabIndex < 0) {
    Why = "no symbol table";
    return !requireSymtab;
  }
  const Section &Sym = F.Secs[F.SymtabIndex];
  if (Sym.Link >= F.Secs.size() || F.Secs[Sym.Link].Type != SHT_STRTAB) {
    Why = "invalid symtab link";
    return false;
  }
  const Section &Str = F.Secs[Sym.Link];
  if (uint64_t(Str.Offset) + Str.Size > F.Buf.size()) {
    Why = "symtab string table out of bounds";
    return false;
  }
  F.Strtab.assign(reinterpret_cast<const char *>(F.Buf.data() + Str.Offset),
                  Str.Size);
  if (Sym.EntSize != 16 || (Sym.Size % 16) != 0 ||
      uint64_t(Sym.Offset) + Sym.Size > F.Buf.size()) {
    Why = "malformed symtab";
    return false;
  }
  unsigned N = Sym.Size / 16;
  for (unsigned I = 0; I != N; ++I) {
    size_t Off = Sym.Offset + size_t(I) * 16;
    uint32_t NameOff = 0, Value = 0, Size = 0;
    if (!rd32(F.Buf, Off, NameOff) || !rd32(F.Buf, Off + 4, Value) ||
        !rd32(F.Buf, Off + 8, Size)) {
      Why = "truncated symtab";
      return false;
    }
    Symbol S;
    S.Value = Value;
    S.Size = Size;
    S.Info = F.Buf[Off + 12];
    S.Other = F.Buf[Off + 13];
    if (!rd16(F.Buf, Off + 14, S.Shndx)) {
      Why = "truncated symtab";
      return false;
    }
    if (NameOff < F.Strtab.size()) {
      size_t End = NameOff;
      while (End < F.Strtab.size() && F.Strtab[End] != 0)
        ++End;
      S.Name.assign(F.Strtab.data() + NameOff, End - NameOff);
    }
    F.Syms.push_back(std::move(S));
  }
  return true;
}

// ---- report text v1: the frozen byte protocol ----------------------------- //

static bool reportSafeByte(unsigned char C) {
  if ((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') ||
      (C >= 'a' && C <= 'z'))
    return true;
  switch (C) {
  case '_': case '.': case '/': case ':': case '+': case '-':
    return true;
  default:
    return false;
  }
}

// Strict decode of one report string column: `\xHH` with UPPERCASE hex, and
// only for bytes the encoder cannot write literally.  No lenient acceptance.
static bool decodeReportField(StringRef In, std::string &Out, std::string &Why) {
  Out.clear();
  for (size_t I = 0; I != In.size();) {
    unsigned char C = In[I];
    if (C != '\\') {
      if (!reportSafeByte(C)) {
        Why = "unsafe literal byte in a string column";
        return false;
      }
      Out.push_back(char(C));
      ++I;
      continue;
    }
    if (I + 3 >= In.size() || In[I + 1] != 'x') {
      Why = "bad escape";
      return false;
    }
    auto Hex = [](char H) -> int {
      if (H >= '0' && H <= '9')
        return H - '0';
      if (H >= 'A' && H <= 'F')
        return H - 'A' + 10;
      return -1;
    };
    int Hi = Hex(In[I + 2]), Lo = Hex(In[I + 3]);
    if (Hi < 0 || Lo < 0) {
      Why = "bad escape (two uppercase hex digits required)";
      return false;
    }
    unsigned char D = static_cast<unsigned char>(Hi * 16 + Lo);
    if (reportSafeByte(D)) {
      Why = "non-canonical escape for a safe byte";
      return false;
    }
    Out.push_back(char(D));
    I += 4;
  }
  return true;
}

struct ReportRow {
  std::string Stable, Sym, File, Section;
  uint8_t Class = 0, Entity = 0, Ownership = 0;
  uint32_t A = 0, Size = 0, Align = 1, Flags = 0, Hash = 0;
  bool BoundOnly = false, RetainedMarker = false;
  unsigned Line = 0;
};

static bool parseFixedHex8(StringRef S, uint32_t &Out) {
  if (S.size() != 10 || !S.starts_with("0x"))
    return false;
  Out = 0;
  for (char C : S.drop_front(2)) {
    if (C >= '0' && C <= '9')
      Out = Out * 16 + unsigned(C - '0');
    else if (C >= 'a' && C <= 'f')
      Out = Out * 16 + unsigned(C - 'a' + 10);
    else
      return false;
  }
  return true;
}

static bool parseDecNoLead(StringRef S, uint32_t &Out) {
  if (S.empty() || S.size() > 10)
    return false;
  if (S.size() > 1 && S[0] == '0')
    return false;
  // G11-D review B2: a ten-digit limit is a LENGTH limit, not a RANGE check.
  // `Out = Out * 10 + d` wraps modulo 2^32, so `4294967300` (10 digits) would
  // silently parse as 4.  Accumulate in 64 bits and require the u32 range.
  uint64_t V = 0;
  for (char C : S) {
    if (C < '0' || C > '9')
      return false;
    V = V * 10 + uint64_t(C - '0');
    if (V > 0xffffffffull)
      return false;
  }
  Out = uint32_t(V);
  return true;
}

// Split a manifest row into whitespace-separated tokens.  `StringRef::split`
// with a StringRef separator matches the literal multi-byte SEQUENCE in this
// tree, not a character set, so `split(Toks, " \t")` never matches a row that
// contains a space followed by anything else -- the manifest parser would then
// reject every legal row.  Tokenize on runs of spaces and tabs explicitly.
static void splitWhitespace(StringRef S, SmallVectorImpl<StringRef> &Out) {
  Out.clear();
  size_t I = 0;
  while (I != S.size()) {
    while (I != S.size() && (S[I] == ' ' || S[I] == '\t'))
      ++I;
    size_t Start = I;
    while (I != S.size() && S[I] != ' ' && S[I] != '\t')
      ++I;
    if (I != Start)
      Out.push_back(S.substr(Start, I - Start));
  }
}

// ---- the model --------------------------------------------------------- //

struct NoteRecord {
  uint32_t Index = 0, Offset = 0;
  std::string Stable;
  uint8_t Class = 0, Entity = 0, Ownership = 0;
  uint32_t Address = 0, Size = 0, Align = 1, Flags = 0, Hash = 0;
};

struct NameEntry {
  uint32_t RecordIndex = 0;
  std::string Name;
};

struct InputObj {
  std::string Arg; // As passed to --object (also the map's spelling).
  std::string Abs; // Lexically absolutized (the report's spelling).
  ElfFile E;
  std::vector<NoteRecord> Notes;
  bool HasNames = false;
  std::vector<NameEntry> Names;
  std::map<uint32_t, std::string> OwnedSymByRecord;
  std::map<uint32_t, std::string> OwnedSecByRecord;
  // G11-D2 (design §7.2): the owned fixed section's ORIGINAL index; V18
  // locates it through (object, shndx), never through its name.
  std::map<uint32_t, uint32_t> OwnedSecIdxByRecord;
};

struct ManifestRow {
  std::string Path;
  uint32_t Line = 0;
  std::string Stable;
  uint32_t Address = 0, Align = 1, Size = 0;
  bool HasAlign = false, HasSize = false;
  bool Retain = false, NoInit = false, Bind = false;
  bool HasClass = false;
  uint8_t Class = 0;
};

struct SourceRef {
  enum Kind { Note, Manifest } K = Note;
  std::string Path, ElfName, Section;
  uint32_t Index = 0;
};

struct MergedEntity {
  std::string Stable, Sym;
  uint8_t Class = 0, EntKind = PE_OBJECT, Ownership = PO_OWNED;
  uint32_t A = 0, Size = 0, Align = 1, Flags = 0, Hash = 0;
  bool BoundOnly = false;
  bool HasKnownSpan = true;
  // G11-D review B6: a bare ELF name is only globally unique for EXTERNAL
  // entities; an internal (STB_LOCAL) name is file-scoped.
  bool ExternalName = false;
  // G11-D review B7: the ONE output symbol V10 selected (address + shape
  // verified).  V11/V15 reuse it instead of re-searching "any name match".
  const Symbol *OutSym = nullptr;
  std::vector<SourceRef> Sources;
  const InputObj *OwnedObj = nullptr;
  const NoteRecord *OwnedRec = nullptr;
  std::string OwnedSection;
  // G11-D2: the owned fixed section's original section index in OwnedObj.
  uint32_t OwnedSecIdx = 0;
};

struct MapRow {
  std::string File, Section;
  uint32_t Addr = 0, Size = 0;
};

// ---- G11-D2: the positioning carrier model (design §3) -------------------- //

struct PosSlice {
  uint32_t InputOffset = 0, FinalAddress = 0, Length = 0;
};
struct PosRecord {
  uint32_t ObjectId = 0, InputShndx = 0, InputSize = 0;
  uint8_t Space = 0xff; // 0=AS0-DATA, 1=XDATA, 2=CODE
  std::vector<PosSlice> Slices;
};
struct Positions {
  std::vector<std::array<uint8_t, 32>> Digests;
  std::vector<PosRecord> Records; // strictly (ObjectId, InputShndx) sorted
};

// One input section's independently derived classification (design §5.2):
// the storage space decided from the section's own name/type/flags, or -1
// when the vocabulary does not admit it.  The rules mirror the producer's
// classifySection boundaries from the frozen design text, never an include.
struct ClassifiedSection {
  int Space = -1;
  bool IsSplit = false;    // SHF_MCS251_XSEG_SPLIT on an XSEG section.
  bool IsMovable = false;  // SHF_MCS251_EDATA_MOVABLE on a DSEG section.
  bool IsOverlay = false;  // SHF_MCS251_OVERLAY member of a group.
  bool EmptyDataExempt = false; // The DATA_EMPTY_PENDING shape.
  std::string Group;       // OSEG/SSEG/BIT_BANK/REG_BANK_n group name.
};

// ---- the verifier -------------------------------------------------------- //

class Verifier {
public:
  Verifier(const Options &O, raw_ostream &Err) : O(O), Err(Err) {}
  bool run();

private:
  const Options &O;
  raw_ostream &Err;
  bool Failed = false;

  ElfFile Final;
  std::vector<InputObj> Objs;
  std::vector<ManifestRow> Manifest;
  std::vector<ReportRow> Report;
  std::vector<MapRow> Map;
  std::vector<std::pair<std::string, uint32_t>> MapSynth;
  std::vector<MergedEntity> Entities;
  // G11-D2: the parsed positioning carrier of the final ELF.
  Positions Pos;

  bool fail(const Twine &Body) {
    if (!Failed)
      Err << Body << "\n";
    Err.flush();
    Failed = true;
    return false;
  }
  bool failVerify(const Twine &Body) { return fail("VERIFY FAIL: " + Body); }

  // G11-D review B9: V15's frozen retain diagnostic must remain reachable even
  // when an earlier check (V8's missing row, V10's missing entity) already has
  // a verdict for the same artifact.  Both bodies are emitted: the caller's
  // specific diagnostic AND the frozen retain one, so the retain rule is never
  // swallowed by a generic return path.
  void noteRetainedAbsent(const MergedEntity &E) {
    Err << "VERIFY FAIL: retained entity " + E.Stable +
               " absent from placement report/symtab\n";
    Err.flush();
  }

  bool checkOutputEntry();          // V1
  bool checkInputNoteStructure();   // V2
  bool checkNameAssociation();      // V3
  bool checkSourceHashes();         // V4
  bool loadManifests();             // V5
  bool mergeAndCompare();           // V6
  bool loadAndCheckReport();        // V7
  bool enumerateBothWays();         // V8
  bool checkReportHashes();         // V9
  bool loadMap();                   // locating evidence (R3)
  bool checkOutputEntities();       // V10
  bool checkSizesAndMainSymbols();  // V11
  bool checkAlignmentAndRanges();   // V12
  bool checkWindows();              // V13
  bool checkOverlaps();             // V14
  bool checkRetain();               // V15
  bool checkNoInitCoverage();       // V16
  bool checkBindRelocations();      // V17
  bool checkCodeImage();            // V18

  // ---- G11-D2: the positioning carrier phase (design §7) ----------------- //
  bool parsePositions();            // §3 structure, envelope, records
  bool checkPositionObjects();      // §3.4 ordered digest agreement
  bool checkPositionsCoverage();    // §5.3 bidirectional coverage + windows
  bool checkRegionRanges();         // §10 (R4-4) region range table
  bool checkPositionsMap();         // §7.3 map occurrence cross-check
  bool checkPositionsSymbols();     // §7.4 named-definition cross-check
  bool checkPositionsLoad();        // §7.5 PT_LOAD cross-check
  const PosRecord *findPosition(uint32_t ObjId, uint32_t Shndx) const;
  bool resolveInputRange(uint32_t ObjId, uint32_t Shndx, uint32_t InputOffset,
                         uint32_t Width, uint32_t &FinalAddr) const;
  static ClassifiedSection classifyAllocSection(const Section &S);
  static int spaceOfArea(StringRef Area);

  // G11-D review B4/R3: the initialization-table location comes from the FINAL
  // ELF's `s_<AREA>`/`l_<AREA>` symbols and MUST agree with the link map's
  // candidate boundary rows; a missing, zero-length-in-one-source or malformed
  // boundary is a FAIL, never a skipped check.  A boundary key that appears
  // more than once with CONFLICTING values is malformed evidence too (review
  // R2-5): returning the first match silently would let the tampered row
  // decide where the initialization tables live.
  std::optional<uint32_t> elfBoundary(StringRef Name);
  std::optional<uint32_t> mapBoundary(StringRef Name);
  // G11-D2 (design §7.7): the structured initialization-table location.
  // `EndExclusive` is the exclusive end (Start + Length, widened); callers
  // must never add the start to it again -- the old (Lo, Hi) form let one
  // caller treat Hi as a length and double-add the base.
  struct InitTableLoc {
    uint32_t Start = 0;
    uint32_t Length = 0;
    uint32_t EndExclusive = 0;
    bool Present = false;
  };
  bool locateInitTable(const char *Area, InitTableLoc &Out);
  // G11-D review R3-1 (R2-1 residue): the REGION bounds a dynamic family
  // allocates into -- the final ELF's synthesized `s_<AREA>`/`l_<AREA>`
  // boundary symbols, cross-checked against the map's own boundary rows with
  // the same agreement rule locateInitTable applies to the initialization
  // tables.  This is the locating-evidence vocabulary PM ruling
  // R-2026-09-17-3 R3 already froze ("须与最终 ELF/symtab/PT_LOAD 互核、
  // XINIT/XDATA_INIT 与最终 s_*/l_* 互核"); the final ELF keeps no input
  // section names (its load sections are the synthesized `.mcs251.load.N`
  // and NOBITS storage families have no section rows at all), so the region
  // boundary symbols are the final-ELF position evidence for a dynamic row.
  bool regionBounds(const std::string &Area, uint32_t &Lo, uint32_t &Len);
  bool checkInitTableOverlaps();
  bool checkDynamicOverlaps();
  // G11-D review R3-3: the storage space of a V18 relocation target,
  // resolved the way the producer's own F9 gate classifies
  // (placementTargetClass) but from the verifier's own readers: the symbol's
  // own input section first, then the placement entities (fixed and bind
  // targets carry their NOTE/manifest class), then the single global
  // definition.  -1 when undecidable, exactly like the producer's
  // classifier, so the verifier never REJECTS more than the linker would.
  int relocTargetSpace(StringRef Name, const InputObj &From) const;
  bool imageByte(uint32_t Addr, uint8_t &Out) const;
  bool imageRange(uint32_t Addr, uint32_t Len, std::vector<uint8_t> &Out) const;
  std::vector<const Symbol *> outputSymbolsNamed(StringRef N) const;
  std::optional<uint32_t> outputAddress(StringRef Name) const;
  static uint32_t layoutHash(uint8_t Class, uint8_t Entity, uint8_t Ownership,
                             uint32_t A, uint32_t Align, uint32_t Flags);
  static uint32_t relocWidth(uint32_t Type) {
    return (Type == R_16 || Type == R_J16 || Type == R_J11) ? 2
           : Type == R_24                                  ? 3
           : Type == R_NONE                                ? 0
                                                           : 1;
  }
};

uint32_t Verifier::layoutHash(uint8_t Class, uint8_t Entity, uint8_t Ownership,
                              uint32_t A, uint32_t Align, uint32_t Flags) {
  const uint8_t F[16] = {
      PlacementSchemaVersion, Class, Entity, Ownership,
      uint8_t(A >> 24), uint8_t(A >> 16), uint8_t(A >> 8), uint8_t(A),
      uint8_t(Align >> 24), uint8_t(Align >> 16), uint8_t(Align >> 8),
      uint8_t(Align),
      uint8_t(Flags >> 24), uint8_t(Flags >> 16), uint8_t(Flags >> 8),
      uint8_t(Flags)};
  SHA256 H;
  H.update(ArrayRef<uint8_t>(F, sizeof(F)));
  std::array<uint8_t, 32> D = H.final();
  return uint32_t(D[28]) << 24 | uint32_t(D[29]) << 16 | uint32_t(D[30]) << 8 |
         uint32_t(D[31]);
}

// ---------------------------------------------------------------------------
// V1: the output entry point -- read the ACTUAL ELF, require a real symtab.
// ---------------------------------------------------------------------------
bool Verifier::checkOutputEntry() {
  auto MB = MemoryBuffer::getFile(O.Elf);
  if (!MB)
    return failVerify("cannot read input ELF " + O.Elf);
  Final.Path = O.Elf;
  Final.Buf.assign((*MB)->getBuffer().begin(), (*MB)->getBuffer().end());
  if (!parseElf(Final))
    return failVerify("malformed input ELF " + O.Elf + ": " + Final.Reason);
  if (Final.Type != ET_EXEC || Final.Machine != EM_MCS251)
    return failVerify("malformed input ELF " + O.Elf +
                      ": not an MCS251 ET_EXEC image");
  if (Final.SymtabIndex < 0)
    return fail("VERIFY FAIL: input ELF has no symbol table (relink with "
                "--verify-placement or --placement-report)");
  std::string Why;
  if (!loadSymtab(Final, true, Why))
    return failVerify("malformed input ELF " + O.Elf + ": " + Why);
  return true;
}

// ---------------------------------------------------------------------------
// V2: original NOTE structure.  V3's carrier is parsed in the same pass so the
// two sections of one object are never read inconsistently.
// ---------------------------------------------------------------------------
bool Verifier::checkInputNoteStructure() {
  for (const std::string &P : O.Objects) {
    InputObj Obj;
    Obj.Arg = P;
    Obj.Abs = absPath(P);
    auto MB = MemoryBuffer::getFile(P);
    if (!MB)
      return failVerify("malformed placement NOTE in " + P +
                        ": cannot read input object");
    Obj.E.Path = P;
    Obj.E.Buf.assign((*MB)->getBuffer().begin(), (*MB)->getBuffer().end());
    if (!parseElf(Obj.E))
      return failVerify("malformed placement NOTE in " + P + ": " +
                        Obj.E.Reason);
    std::string Why;
    if (!loadSymtab(Obj.E, false, Why))
      return failVerify("malformed placement NOTE in " + P + ": " + Why);

    const Section *Note = Obj.E.findByName(PlacementNoteSectionName);
    if (Note) {
      // The frozen carrier shape (design §3.3, LinkerCore's own acceptance
      // rule) is SHT_NOTE, flags 0, ALIGN 4.  Alignment is part of the shape:
      // an align=1 carrier is malformed evidence, not a tolerable variant
      // (G11-D review B7).
      if (Note->Type != SHT_NOTE || Note->Flags != 0 || Note->Align != 4)
        return failVerify("malformed placement NOTE in " + P +
                          ": carrier is not a flags=0 align=4 SHT_NOTE");
      std::vector<uint8_t> B;
      if (!Obj.E.sectionData(*Note, B))
        return failVerify("malformed placement NOTE in " + P +
                          ": section body out of bounds");
      uint32_t Namesz = 0, Descsz = 0, NType = 0;
      if (B.size() < 20 || !rd32(B, 0, Namesz) || !rd32(B, 4, Descsz) ||
          !rd32(B, 8, NType))
        return failVerify("malformed placement NOTE in " + P +
                          ": envelope truncated");
      if (Namesz != PlacementNoteNameSize)
        return failVerify("malformed placement NOTE in " + P + ": namesz " +
                          Twine(Namesz) + " != 7");
      const uint8_t Magic[8] = {'M', 'C', 'S', '2', '5', '1', 0, 0};
      for (unsigned I = 0; I != 8; ++I)
        if (B[12 + I] != Magic[I])
          return failVerify("malformed placement NOTE in " + P +
                            ": note name is not MCS251");
      if (NType != PlacementNoteType)
        return failVerify("malformed placement NOTE in " + P + ": note type " +
                          Twine(NType) + " != 1");
      if (Descsz != B.size() - 20)
        return failVerify("malformed placement NOTE in " + P +
                          ": descsz does not cover the record table");
      size_t Off = 20;
      while (Off != B.size()) {
        if (B.size() - Off < 29)
          return failVerify("malformed placement NOTE in " + P +
                            ": truncated record at offset " + Twine(Off));
        NoteRecord R;
        R.Offset = uint32_t(Off);
        R.Index = uint32_t(Obj.Notes.size());
        uint32_t RecSize = 0;
        rd32(B, Off, RecSize);
        R.Class = B[Off + 5];
        R.Entity = B[Off + 6];
        R.Ownership = B[Off + 7];
        uint8_t Ver = B[Off + 4];
        if (!rd32(B, Off + 8, R.Address) || !rd32(B, Off + 12, R.Size) ||
            !rd32(B, Off + 16, R.Align) || !rd32(B, Off + 20, R.Flags) ||
            !rd32(B, Off + 24, R.Hash))
          return failVerify("malformed placement NOTE in " + P +
                            ": truncated record at offset " + Twine(Off));
        uint8_t StableLen = B[Off + 28];
        auto Bad = [&](const Twine &Why2) {
          return failVerify("malformed placement NOTE in " + P + ": record " +
                            Twine(R.Index) + " at offset " + Twine(Off) +
                            ": " + Why2);
        };
        if (B.size() - Off - 29 < StableLen)
          return Bad("truncated stable symbol");
        R.Stable.assign(reinterpret_cast<const char *>(B.data() + Off + 29),
                        StableLen);
        if (Ver != PlacementSchemaVersion)
          return Bad("unsupported schema version " + Twine(unsigned(Ver)));
        if (R.Class > PSC_CODE)
          return Bad("unknown storage_class " + Twine(unsigned(R.Class)));
        if (R.Entity > PE_FUNCTION)
          return Bad("unknown entity " + Twine(unsigned(R.Entity)));
        if (R.Ownership > PO_BIND)
          return Bad("unknown ownership " + Twine(unsigned(R.Ownership)));
        if (R.Address > 0xffffff)
          return Bad("address outside the 24-bit space");
        if (R.Flags & ~(FlagRetain | FlagNoInit))
          return Bad("unknown flags " + hex0x(R.Flags));
        if (R.Ownership == PO_BIND && R.Flags != 0)
          return Bad("bind record carries non-zero flags");
        if (R.Ownership == PO_BIND && R.Entity == PE_OBJECT && R.Size == 0)
          return Bad("bind object has size 0");
        if (R.Ownership == PO_BIND && R.Entity == PE_FUNCTION && R.Size != 0)
          return Bad("bind function has a non-zero size");
        if (R.Align == 0 || (R.Align & (R.Align - 1)) != 0 ||
            R.Align > 0x1000000)
          return Bad("align is not a non-zero power of two");
        if (StableLen == 0)
          return Bad("empty stable symbol");
        uint32_t Canon = (25u + StableLen + 3u) & ~uint32_t(3);
        if (RecSize != Canon)
          return Bad("record_size " + Twine(RecSize) +
                     " is not the canonical alignTo(25+stable_len,4) value " +
                     Twine(Canon));
        if (B.size() - Off < size_t(4) + RecSize)
          return Bad("record_size does not fit the remaining NOTE data");
        for (uint32_t I = 25 + StableLen; I != RecSize; ++I)
          if (B[Off + 4 + I] != 0)
            return Bad("non-zero padding");
        if (StringRef(R.Stable).contains('\0'))
          return Bad("stable symbol contains a NUL");
        Obj.Notes.push_back(std::move(R));
        Off += 4 + RecSize;
      }
    }

    const Section *Names = Obj.E.findByName(NamesNoteSectionName);
    if (Names) {
      Obj.HasNames = true;
      if (Names->Type != SHT_NOTE || Names->Flags != 0 || Names->Align != 4)
        return failVerify("malformed placement names NOTE in " + P +
                          ": carrier is not a flags=0 align=4 SHT_NOTE");
      std::vector<uint8_t> B;
      if (!Obj.E.sectionData(*Names, B))
        return failVerify("malformed placement names NOTE in " + P +
                          ": section body out of bounds");
      auto Bad = [&](const Twine &Why2) {
        return failVerify("malformed placement names NOTE in " + P + ": " +
                          Why2);
      };
      uint32_t Namesz = 0, Descsz = 0, NType = 0;
      if (B.size() < 20 || !rd32(B, 0, Namesz) || !rd32(B, 4, Descsz) ||
          !rd32(B, 8, NType))
        return Bad("envelope truncated");
      if (Namesz != PlacementNoteNameSize)
        return Bad("namesz != 7");
      const uint8_t Magic[8] = {'M', 'C', 'S', '2', '5', '1', 0, 0};
      for (unsigned I = 0; I != 8; ++I)
        if (B[12 + I] != Magic[I])
          return Bad("note name is not MCS251");
      if (NType != PlacementNamesType)
        return Bad("note type != 2");
      if (Descsz != B.size() - 20)
        return Bad("descsz does not cover the association table");
      uint32_t Version = 0, Count = 0;
      if (!rd32(B, 20, Version) || !rd32(B, 24, Count))
        return Bad("truncated header");
      if (Version != PlacementNamesVersion)
        return Bad("association_version != 1");
      size_t Off = 28;
      std::set<uint32_t> Seen;
      for (uint32_t I = 0; I != Count; ++I) {
        uint32_t RecIdx = 0, NameLen = 0;
        if (!rd32(B, Off, RecIdx) || !rd32(B, Off + 4, NameLen))
          return Bad("truncated entry " + Twine(I));
        Off += 8;
        if (uint64_t(Off) + NameLen > B.size())
          return Bad("entry " + Twine(I) + " name out of bounds");
        NameEntry E;
        E.RecordIndex = RecIdx;
        E.Name.assign(reinterpret_cast<const char *>(B.data() + Off), NameLen);
        if (E.Name.empty())
          return Bad("entry " + Twine(I) + " has an empty name");
        if (E.Name.find('\0') != std::string::npos)
          return Bad("entry " + Twine(I) + " name contains a NUL");
        if (!Seen.insert(RecIdx).second)
          return Bad("duplicate placement record index");
        if (RecIdx >= Obj.Notes.size())
          return Bad("placement record index out of range");
        Off += NameLen;
        uint32_t Pad = (4 - (NameLen & 3)) & 3;
        if (uint64_t(Off) + Pad > B.size())
          return Bad("entry " + Twine(I) + " padding out of bounds");
        for (uint32_t K = 0; K != Pad; ++K)
          if (B[Off + K] != 0)
            return Bad("entry " + Twine(I) + " padding is not zero");
        Off += Pad;
        Obj.Names.push_back(std::move(E));
      }
      if (Off != B.size())
        return Bad("trailing bytes after the association table");
      for (uint32_t I = 0; I != Obj.Notes.size(); ++I)
        if (!Seen.count(I))
          return Bad("missing placement record index " + Twine(I));
    }
    Objs.push_back(std::move(Obj));
  }
  if (Objs.empty())
    return failVerify("no --object inputs were provided; the NOTE/manifest "
                      "enumeration source is unknown");
  return true;
}

// V3: resolve each record's ELF symbol carrier.  With a `.names` carrier the
// association is checked strictly; without one, the design's legacy boundary
// applies: an OWNED record may recover its name from the dedicated fixed
// section's unique entity symbol, while a BIND record must be refused outright
// -- "legacy bind must not be guessed, not even when the stable happens to
// equal the ELF name" (design §8.3 旧对象接受边界, PM ruling R-2026-09-17-3).
bool Verifier::checkNameAssociation() {
  for (InputObj &Obj : Objs) {
    for (const NoteRecord &R : Obj.Notes) {
      if (Obj.HasNames) {
        const NameEntry *E = nullptr;
        for (const NameEntry &N : Obj.Names)
          if (N.RecordIndex == R.Index)
            E = &N;
        if (!E)
          return failVerify("missing placement name association for " +
                            R.Stable + " in " + Obj.Arg +
                            " (recompile placement object)");
        if (R.Ownership == PO_OWNED) {
          const Section *Sec =
              Obj.E.findByName((FixedSectionPrefix + R.Stable).str());
          std::vector<const Symbol *> Ents;
          if (Sec)
            for (const Symbol &S : Obj.E.Syms)
              if (S.Shndx < Obj.E.Secs.size() && &Obj.E.Secs[S.Shndx] == Sec &&
                  (S.Type() == STT_FUNC || S.Type() == STT_OBJECT))
                Ents.push_back(&S);
          if (!Sec || Ents.size() != 1 || Ents[0]->Name != E->Name)
            return failVerify("placement name association mismatch for " +
                              R.Stable + " in " + Obj.Arg);
          Obj.OwnedSymByRecord[R.Index] = Ents[0]->Name;
          Obj.OwnedSecByRecord[R.Index] = Sec->Name;
          for (unsigned SI = 0; SI != Obj.E.Secs.size(); ++SI)
            if (&Obj.E.Secs[SI] == Sec) {
              Obj.OwnedSecIdxByRecord[R.Index] = SI;
              break;
            }
        } else {
          const Symbol *Found = nullptr;
          unsigned Hits = 0;
          for (const Symbol &S : Obj.E.Syms)
            if (S.Name == E->Name) {
              Found = &S;
              ++Hits;
            }
          if (Hits != 1 || Found->Shndx != 0 || Found->Bind() != STB_GLOBAL)
            return failVerify("placement name association mismatch for " +
                              R.Stable + " in " + Obj.Arg);
        }
      } else if (R.Ownership == PO_OWNED) {
        const Section *Sec =
            Obj.E.findByName((FixedSectionPrefix + R.Stable).str());
        std::vector<const Symbol *> Ents;
        if (Sec)
          for (const Symbol &S : Obj.E.Syms)
            if (S.Shndx < Obj.E.Secs.size() && &Obj.E.Secs[S.Shndx] == Sec &&
                (S.Type() == STT_FUNC || S.Type() == STT_OBJECT))
              Ents.push_back(&S);
        if (!Sec || Ents.size() != 1)
          return failVerify("missing placement name association for " +
                            R.Stable + " in " + Obj.Arg +
                            " (recompile placement object)");
        Obj.OwnedSymByRecord[R.Index] = Ents[0]->Name;
        Obj.OwnedSecByRecord[R.Index] = Sec->Name;
        for (unsigned SI = 0; SI != Obj.E.Secs.size(); ++SI)
          if (&Obj.E.Secs[SI] == Sec) {
            Obj.OwnedSecIdxByRecord[R.Index] = SI;
            break;
          }
      } else {
        // Design §8.3: an old BIND object carries no association carrier, and
        // the association must NOT be guessed.  Refusing here is the rule, not
        // a capability gap: the verifier could match an undefined external by
        // name, but "stable == declaration name today" is exactly the identity
        // assumption the carrier exists to replace.
        return failVerify("missing placement name association for " + R.Stable +
                          " in " + Obj.Arg +
                          " (recompile placement object)");
      }
    }
  }
  return true;
}

// V4: every original NOTE record's own `H_source`, recomputed from that
// record's own explicit fields.  Manifest rows carry no hash field (the design
// forbids inventing one), so they are not covered.
bool Verifier::checkSourceHashes() {
  for (const InputObj &Obj : Objs)
    for (const NoteRecord &R : Obj.Notes) {
      uint32_t Want =
          layoutHash(R.Class, R.Entity, R.Ownership, R.Address, R.Align,
                     R.Flags);
      if (Want != R.Hash)
        return failVerify("layout hash mismatch for " + R.Stable +
                          ": record " + Twine(R.Index) + " in " + Obj.Arg +
                          " stores " + hex0x(R.Hash) +
                          " but its own recorded fields hash to " +
                          hex0x(Want));
    }
  return true;
}

// V5: independent manifest parse; absent-vs-zero is preserved and the frozen
// malformed shapes are rejected with the file/line locator.
bool Verifier::loadManifests() {
  for (const std::string &P : O.Manifests) {
    auto MB = MemoryBuffer::getFile(P);
    if (!MB)
      return failVerify("malformed placement manifest entry for " + P +
                        ": cannot read the manifest");
    StringRef C = (*MB)->getBuffer();
    uint32_t LineNo = 0;
    while (!C.empty()) {
      auto [Line, Rest] = C.split('\n');
      ++LineNo;
      C = Rest;
      StringRef T = Line.trim();
      if (T.empty() || T.starts_with("#"))
        continue;
      ManifestRow M;
      M.Path = absPath(P);
      M.Line = LineNo;
      SmallVector<StringRef, 8> Toks;
      splitWhitespace(T, Toks);
      auto Bad = [&](const std::string &Sym) {
        return failVerify("malformed placement manifest entry for " + Sym +
                          " (" + M.Path + ":" + Twine(LineNo) + ")");
      };
      std::string Guess = Toks.size() > 1 ? Toks[1].str() : T.str();
      if (Toks.size() < 3 || Toks[0] != "place")
        return Bad(Guess);
      M.Stable = Toks[1].str();
      if (M.Stable.empty())
        return Bad(Guess);
      if (Toks[2].getAsInteger(0, M.Address) || M.Address > 0xffffff)
        return Bad(M.Stable);
      for (unsigned I = 3; I != Toks.size(); ++I) {
        StringRef Tok = Toks[I];
        if (Tok == "retain")
          M.Retain = true;
        else if (Tok == "noinit")
          M.NoInit = true;
        else if (Tok == "bind")
          M.Bind = true;
        else if (Tok == "data" || Tok == "xdata" || Tok == "code") {
          M.HasClass = true;
          M.Class = Tok == "data"   ? uint8_t(PSC_AS0_DATA)
                    : Tok == "xdata" ? uint8_t(PSC_XDATA)
                                     : uint8_t(PSC_CODE);
        } else if (Tok.starts_with("align=")) {
          if (Tok.substr(6).getAsInteger(0, M.Align) || M.Align == 0 ||
              (M.Align & (M.Align - 1)) != 0)
            return Bad(M.Stable);
          M.HasAlign = true;
        } else if (Tok.starts_with("size=")) {
          if (Tok.substr(5).getAsInteger(0, M.Size))
            return Bad(M.Stable);
          M.HasSize = true;
        } else
          return Bad(M.Stable);
      }
      if (M.Bind && (M.NoInit || M.Retain))
        return Bad(M.Stable);
      Manifest.push_back(std::move(M));
    }
  }
  return true;
}

// V6: the independent semantic merge, in P-5 precedence order, producing the
// expected entities and their complete source expansion.
bool Verifier::mergeAndCompare() {
  struct Entry {
    bool IsManifest = false;
    const InputObj *Obj = nullptr;
    const NoteRecord *Rec = nullptr;
    const ManifestRow *Man = nullptr;
    std::string Stable, Sym, Section;
    uint8_t Class = 0, EntKind = PE_OBJECT, Ownership = PO_OWNED;
    uint32_t A = 0, Size = 0, Align = 1, Flags = 0;
    bool HasClass = false, HasSize = false, HasAlign = false;
    bool Retain = false, NoInit = false;
  };
  std::map<std::string, std::vector<Entry>> Groups;
  for (const InputObj &Obj : Objs) {
    for (const NoteRecord &R : Obj.Notes) {
      Entry E;
      E.Obj = &Obj;
      E.Rec = &R;
      E.Stable = R.Stable;
      E.Class = R.Class;
      E.EntKind = R.Entity;
      E.Ownership = R.Ownership;
      E.A = R.Address;
      E.Size = R.Size;
      E.Align = R.Align;
      E.Flags = R.Flags;
      E.HasClass = E.HasSize = true;
      if (R.Ownership == PO_OWNED) {
        auto It = Obj.OwnedSymByRecord.find(R.Index);
        if (It == Obj.OwnedSymByRecord.end())
          return failVerify("missing placement name association for " +
                            R.Stable + " in " + Obj.Arg +
                            " (recompile placement object)");
        E.Sym = It->second;
        E.Section = Obj.OwnedSecByRecord.at(R.Index);
      } else {
        // The independent merge applies the SAME legacy boundary V3 does: a
        // bind record's ELF name comes from the association carrier only.
        // Guessing it from an undefined external is precisely the identity
        // assumption the carrier replaces (§8.3), so an object without the
        // carrier never reaches the merge.
        for (const NameEntry &N : Obj.Names)
          if (N.RecordIndex == R.Index)
            E.Sym = N.Name;
        if (E.Sym.empty())
          return failVerify("missing placement name association for " +
                            R.Stable + " in " + Obj.Arg +
                            " (recompile placement object)");
      }
      Groups[R.Stable].push_back(std::move(E));
    }
  }
  for (const ManifestRow &M : Manifest) {
    Entry E;
    E.IsManifest = true;
    E.Man = &M;
    E.Stable = M.Stable;
    E.A = M.Address;
    E.Align = M.HasAlign ? M.Align : 1;
    E.HasAlign = M.HasAlign;
    E.Retain = M.Retain;
    E.NoInit = M.NoInit;
    E.Size = M.Size;
    E.HasSize = M.HasSize;
    E.HasClass = M.HasClass;
    E.Class = M.Class;
    E.EntKind = PE_OBJECT;
    E.Ownership = M.Bind ? uint8_t(PO_BIND) : uint8_t(PO_OWNED);
    E.Flags = (M.Retain ? FlagRetain : 0) | (M.NoInit ? FlagNoInit : 0);
    Groups[M.Stable].push_back(std::move(E));
  }

  auto Ref = [](const Entry &T) {
    return T.IsManifest ? T.Man->Path + ":" + Twine(T.Man->Line).str()
                        : T.Obj->Arg;
  };
  auto Conflict = [&](const Entry &X, const Entry &Y, const char *Reason) {
    return failVerify("conflicting placement for " + X.Stable + ": " + Reason +
                      " (" + Ref(X) + " @ " + hex0x(X.A) + " vs " + Ref(Y) +
                      " @ " + hex0x(Y.A) + ")");
  };
  // G11-D review B9: a NOTE/manifest conflict uses the contract's V5/V6
  // conflict template -- `conflicting placement for %sym: %reason` -- with the
  // file/line locator kept as the origin suffix.  The former
  // "conflicting placement constraints (NOTE vs manifest) for ..." wording was
  // not one of the approved templates.
  auto ManifestClash = [&](const std::string &Sym, const Entry &M,
                           const char *What) {
    return failVerify("conflicting placement for " + Sym + ": " + What + " (" +
                      M.Man->Path + ":" + Twine(M.Man->Line) + ")");
  };

  for (auto &G : Groups) {
    const std::string &Stable = G.first;
    std::vector<Entry> &Ents = G.second;
    std::vector<const Entry *> Owned;
    for (const Entry &E : Ents)
      if (E.Ownership == PO_OWNED && !E.IsManifest)
        Owned.push_back(&E);
    // P-5(1): identity collision over the complete owned set, before any
    // duplicate-owned verdict (order independent).
    for (size_t I = 1; I < Owned.size(); ++I)
      if (Owned[0]->Class != Owned[I]->Class ||
          Owned[0]->EntKind != Owned[I]->EntKind ||
          Owned[0]->Sym != Owned[I]->Sym)
        return Conflict(*Owned[0], *Owned[I],
                        "the stable symbol names different entities");
    // P-5(2): all-equal owned identities are one duplicated record.
    if (Owned.size() > 1)
      return failVerify("placement NOTE duplicate owned record for " + Stable +
                        " in " + Owned[0]->Obj->Arg + " and " +
                        Owned[1]->Obj->Arg);

    const Entry *FirstOwned = Owned.empty() ? nullptr : Owned[0];
    const bool HasOwned = FirstOwned != nullptr;
    const Entry *Seed = FirstOwned;
    if (!Seed)
      for (const Entry &E : Ents)
        if (!E.IsManifest) {
          Seed = &E;
          break;
        }
    const bool ManifestOnly = Seed == nullptr;

    uint8_t MClass = 0, MEnt = PE_OBJECT;
    uint32_t MAddress = 0, MSize = 0, MAlign = 1;
    bool HasKnownSpan = true;
    if (!ManifestOnly) {
      MClass = Seed->Class;
      MEnt = Seed->EntKind;
      MAddress = Seed->A;
      MSize = Seed->Size;
      MAlign = Seed->Align;
    } else {
      const ManifestRow *ClassRow = nullptr, *SizeRow = nullptr;
      for (const Entry &E : Ents) {
        if (E.Retain || E.NoInit)
          return failVerify("malformed placement manifest entry for " + Stable +
                            " (" + E.Man->Path + ":" + Twine(E.Man->Line) +
                            ")");
        if (E.HasClass) {
          if (ClassRow && ClassRow->Class != E.Class)
            return ManifestClash(Stable, E, "manifest storage_class disagrees");
          if (!ClassRow)
            ClassRow = E.Man;
        }
        if (E.HasSize) {
          if (SizeRow && SizeRow->Size != E.Size)
            return ManifestClash(Stable, E, "manifest size disagrees");
          if (!SizeRow)
            SizeRow = E.Man;
        }
        if (E.A != Ents.front().A)
          return ManifestClash(Stable, E, "manifest addresses disagree");
      }
      if (!ClassRow)
        return failVerify("malformed placement manifest entry for " + Stable +
                          ": no storage class is declared anywhere");
      MClass = ClassRow->Class;
      MEnt = PE_OBJECT;
      MAddress = Ents.front().A;
      if (SizeRow)
        MSize = SizeRow->Size;
      else
        HasKnownSpan = false; // No NOTE and no explicit size: unprovable.
      MAlign = Ents.front().Align;
    }

    const bool HasNote = !ManifestOnly && !Seed->IsManifest;
    // The group's actual ELF name, computed BEFORE the field merge so every
    // record of the group can be held to it (review B6).  `NameSeed` is the
    // record that supplied the name; the order-independent agreement check
    // below reports ITS origin, so the diagnostic names the two records that
    // disagree regardless of which one was read first (review R2-3).
    const Entry *NameSeed = nullptr;
    std::string GroupElf;
    for (const Entry &E : Ents)
      if (!E.IsManifest && !E.Sym.empty()) {
        GroupElf = E.Sym;
        NameSeed = &E;
        break;
      }
    for (const Entry &E : Ents) {
      if (E.IsManifest) {
        if (E.HasClass && E.Class != MClass)
          return ManifestClash(Stable, E, "manifest storage_class disagrees");
        if (E.A != MAddress)
          return ManifestClash(Stable, E, "manifest address disagrees");
        if (E.HasSize && HasNote && E.Size != MSize)
          return ManifestClash(Stable, E, "manifest size disagrees");
        if (E.HasAlign && E.Align != 1 && MAlign != 1 && E.Align != MAlign)
          return ManifestClash(Stable, E, "manifest align disagrees");
        if (E.Align != 1)
          MAlign = std::max(MAlign, E.Align);
        if (!HasOwned && (E.Retain || E.NoInit))
          return failVerify("malformed placement manifest entry for " + Stable +
                            " (" + E.Man->Path + ":" + Twine(E.Man->Line) +
                            ")");
        // A non-bind policy marker is an explicit constraint on the OWNED
        // initialization policy (design §8.6): it cannot manufacture a
        // policy the owned NOTE does not carry.
        if (HasOwned && E.NoInit && !(FirstOwned->Flags & FlagNoInit))
          return ManifestClash(Stable, E,
                               "manifest [noinit] contradicts the owned NOTE");
        if (HasOwned && E.Retain && !(FirstOwned->Flags & FlagRetain))
          return ManifestClash(Stable, E,
                               "manifest [retain] contradicts the owned NOTE");
        continue;
      }
      // Bind NOTE entries: field-semantic merge against the merged value.
      // G11-D review R2-3 (B6): one stable names ONE entity, so every NOTE
      // record of the group -- owned AND bind -- must name the same ELF
      // entity, INDEPENDENT OF INPUT ORDER.  The old loop skipped the owned
      // entries, so a bind record that happened to be read first was only
      // compared with itself and the owned record's name never took part;
      // the same mismatch then passed or failed on the input order alone.
      if (E.Sym != GroupElf)
        return Conflict(NameSeed ? *NameSeed : *Seed, E,
                        "the group's records name different ELF entities");
      if (&E == Seed || E.Ownership == PO_OWNED)
        continue;
      if (E.Class != MClass || E.EntKind != MEnt)
        return Conflict(*Seed, E, "bind storage_class/entity disagrees");
      if (E.A != MAddress)
        return Conflict(*Seed, E, "bind address disagrees");
      if (E.EntKind == PE_OBJECT && E.Size != MSize)
        return Conflict(*Seed, E, "bind object size disagrees");
      if (E.Align != 1 && MAlign != 1 && E.Align != MAlign)
        return Conflict(*Seed, E, "bind align disagrees");
      if (E.Align != 1)
        MAlign = std::max(MAlign, E.Align);
    }

    MergedEntity ME;
    ME.Stable = Stable;
    ME.Class = MClass;
    ME.EntKind = MEnt;
    ME.A = MAddress;
    ME.Size = MSize;
    ME.Align = MAlign;
    ME.HasKnownSpan = HasKnownSpan;
    ME.Ownership = HasOwned ? PO_OWNED : PO_BIND;
    ME.Flags = HasOwned
                   ? ((FirstOwned->Flags & FlagNoInit) |
                      (FirstOwned->Flags & FlagRetain))
                   : 0;
    ME.BoundOnly = !HasOwned;
    ME.Hash =
        layoutHash(ME.Class, ME.EntKind, ME.Ownership, ME.A, ME.Align, ME.Flags);
    if (HasOwned) {
      ME.OwnedObj = FirstOwned->Obj;
      ME.OwnedRec = FirstOwned->Rec;
      ME.OwnedSection = FirstOwned->Section;
      auto IdxIt =
          FirstOwned->Obj->OwnedSecIdxByRecord.find(FirstOwned->Rec->Index);
      ME.OwnedSecIdx =
          IdxIt != FirstOwned->Obj->OwnedSecIdxByRecord.end()
              ? IdxIt->second
              : 0;
      ME.Sym = FirstOwned->Sym;
    }
    if (ME.Sym.empty())
      ME.Sym = GroupElf.empty() ? Stable : GroupElf;
    for (const Entry &E : Ents) {
      SourceRef S;
      if (E.IsManifest) {
        S.K = SourceRef::Manifest;
        S.Path = E.Man->Path;
        S.ElfName = ME.Sym;
        S.Index = E.Man->Line;
      } else {
        S.K = SourceRef::Note;
        S.Path = E.Obj->Abs;
        S.ElfName = E.Sym;
        S.Section = E.Section;
        S.Index = E.Rec->Index;
      }
      ME.Sources.push_back(std::move(S));
    }
    // Design §8.2: an EXTERNAL name may not be claimed by two stables; an
    // internal (STB_LOCAL) name is FILE-SCOPED and may legitimately repeat
    // across inputs (G11-D review B6 -- the old code applied the global rule
    // to every entity, including internal owned ones, and wrongly rejected
    // two distinct same-named statics at different addresses).
    if (HasOwned && !ME.Sym.empty()) {
      const Symbol *Out = nullptr;
      for (const Symbol &S : FirstOwned->Obj->E.Syms)
        if (S.Name == ME.Sym && S.Shndx != 0) {
          Out = &S;
          break;
        }
      ME.ExternalName = Out && Out->Bind() == STB_GLOBAL;
    } else if (!ME.Sym.empty()) {
      // bind-only / manifest-only: the identity lives in an undefined
      // external (V3 checked STB_GLOBAL for a carrier-backed record), so its
      // name is external by construction.
      ME.ExternalName = true;
    }
    for (const MergedEntity &Other : Entities) {
      if (Other.Sym != ME.Sym)
        continue;
      if (ME.ExternalName || Other.ExternalName)
        return failVerify("conflicting placement for " + Stable +
                          ": two stable symbols share the external ELF name " +
                          ME.Sym);
      // Both internal: legal only when they are genuinely different objects
      // in different input files.  The same file and different stables would
      // mean the file carries two placement entities under one ELF name,
      // which no input can express.
      if (Other.OwnedObj && ME.OwnedObj &&
          Other.OwnedObj->Arg == ME.OwnedObj->Arg)
        return failVerify("conflicting placement for " + Stable +
                          ": two stable symbols in " + ME.OwnedObj->Arg +
                          " share the internal ELF name " + ME.Sym);
    }
    Entities.push_back(std::move(ME));
  }
  // A bind-only name is a GLOBAL/NOTYPE/ABS synthesized output symbol: its
  // bare name is external, so the global uniqueness rule does apply.  Internal
  // entities are excluded -- their name is file-scoped (R5's fail-closed rule
  // covers the undecidable output matching, not this identity map).
  std::map<std::string, const MergedEntity *> ByOutputName;
  for (const MergedEntity &E : Entities) {
    if (E.Sym.empty() || !E.ExternalName)
      continue;
    auto It = ByOutputName.find(E.Sym);
    if (It != ByOutputName.end() && It->second->Stable != E.Stable)
      return failVerify("conflicting placement for " + E.Stable +
                        ": duplicate bind-only symbol name " + E.Sym);
    if (It == ByOutputName.end())
      ByOutputName[E.Sym] = &E;
  }
  return true;
}

// ---------------------------------------------------------------------------
// V7: strict report parse + explicit field cross-check.
// ---------------------------------------------------------------------------
bool Verifier::loadAndCheckReport() {
  auto MB = MemoryBuffer::getFile(O.Report);
  if (!MB)
    return failVerify("cannot read placement report " + O.Report);
  StringRef C = (*MB)->getBuffer();
  if (!C.empty() && !C.ends_with("\n"))
    return failVerify("malformed placement report at line 0: the last line "
                      "does not end with LF");
  if (C.contains("\r"))
    return failVerify("malformed placement report at line 0: a CR byte is "
                      "present");
  unsigned LineNo = 0;
  // G11-D review B2: source uniqueness is the contract's (file, sym, stable)
  // TRIPLE.  A four-tuple let a second row for one source through merely by
  // carrying a different section column.
  std::set<std::tuple<std::string, std::string, std::string>> Seen;
  while (!C.empty()) {
    auto [Line, Rest] = C.split('\n');
    C = Rest;
    ++LineNo;
    auto Bad = [&](const Twine &Why) {
      return failVerify("malformed placement report at line " + Twine(LineNo) +
                        ": " + Why);
    };
    if (Line.empty())
      return Bad("empty line");
    SmallVector<StringRef, 16> Cols;
    splitExactDelim(Line, " | ", Cols);
    if (Cols.size() != 13 && Cols.size() != 14)
      return Bad("expected 13 columns plus an optional retained marker, got " +
                 Twine(Cols.size()));
    ReportRow R;
    R.Line = LineNo;
    std::string Why;
    if (!decodeReportField(Cols[0], R.Stable, Why)) return Bad(Why);
    if (!decodeReportField(Cols[1], R.Sym, Why)) return Bad(Why);
    if (!decodeReportField(Cols[2], R.File, Why)) return Bad(Why);
    if (!decodeReportField(Cols[3], R.Section, Why)) return Bad(Why);
    uint32_t V = 0;
    if (!parseDecNoLead(Cols[4], V) || V > 2) return Bad("bad class column");
    R.Class = uint8_t(V);
    if (!parseDecNoLead(Cols[5], V) || V > 1) return Bad("bad entity column");
    R.Entity = uint8_t(V);
    if (!parseDecNoLead(Cols[6], V) || V > 1) return Bad("bad ownership column");
    R.Ownership = uint8_t(V);
    if (!parseFixedHex8(Cols[7], R.A)) return Bad("bad address column");
    if (!parseDecNoLead(Cols[8], R.Size)) return Bad("bad size column");
    if (!parseDecNoLead(Cols[9], R.Align) || R.Align == 0)
      return Bad("bad align column");
    if (!parseDecNoLead(Cols[10], R.Flags)) return Bad("bad flags column");
    if (!parseFixedHex8(Cols[11], R.Hash)) return Bad("bad layout_hash column");
    if (!parseDecNoLead(Cols[12], V) || V > 1)
      return Bad("bad bound_only column");
    R.BoundOnly = V != 0;
    const bool MarkerExpected = (R.Flags & FlagRetain) != 0;
    if (Cols.size() == 14) {
      if (Cols[13] != "retained")
        return Bad("unknown trailing column " + Cols[13]);
      R.RetainedMarker = true;
    }
    if (MarkerExpected != R.RetainedMarker)
      return Bad("the retained marker disagrees with the flags column");
    if (!Seen.insert({R.Stable, R.File, R.Sym}).second)
      return failVerify("duplicate placement report row for " + R.Sym);
    Report.push_back(std::move(R));
  }
  // A zero-byte contract has an empty Report: the loop must not index [0]
  // (G11-D review B1: `I != Report.size()` with an empty vector entered the
  // body at I = 1 and read out of bounds -> SIGSEGV).
  for (size_t I = 1; I < Report.size(); ++I) {
    const ReportRow &A = Report[I - 1], &B = Report[I];
    if (std::tie(B.Stable, B.File, B.Sym, B.Section) <
        std::tie(A.Stable, A.File, A.Sym, A.Section))
      return failVerify("malformed placement report at line " + Twine(B.Line) +
                        ": rows are not in (stable, file, sym, section) byte "
                        "order");
  }
  if (Entities.empty() && !Report.empty())
    return failVerify("malformed placement report at line 1: rows exist but no "
                      "placement record does");

  // Independently rebuild the EXPECTED report rows from the original NOTE and
  // manifest sources: key (stable, file, sym) -> the source section(s) that
  // key may legitimately carry.  The report can never act as its own oracle.
  struct Expected {
    const MergedEntity *E = nullptr;
    std::vector<std::string> Sections;
  };
  std::map<std::tuple<std::string, std::string, std::string>, Expected> Want;
  for (const MergedEntity &E : Entities)
    for (const SourceRef &S : E.Sources) {
      auto &W = Want[{E.Stable, S.Path, S.ElfName}];
      W.E = &E;
      if (std::find(W.Sections.begin(), W.Sections.end(), S.Section) ==
          W.Sections.end())
        W.Sections.push_back(S.Section);
    }
  // Every report row belonging to a group must carry that group's merged
  // fields -- not merely the last row that happened to match.  Combined with
  // the (file, sym, stable) duplicate rule above this proves "all expanded
  // rows of one group carry the same merged fields" (G11-D review B2).
  for (const ReportRow &R : Report) {
    auto It = Want.find({R.Stable, R.File, R.Sym});
    if (It == Want.end())
      continue; // V8 owns the "row has no source" verdict.
    const MergedEntity &E = *It->second.E;
    auto Mismatch = [&](const char *Field) {
      return failVerify(std::string("placement report field ") + Field +
                        " mismatch for " + E.Stable);
    };
    if (R.Class != E.Class) return Mismatch("class");
    if (R.Entity != E.EntKind) return Mismatch("entity");
    if (R.Ownership != E.Ownership) return Mismatch("ownership");
    if (R.A != E.A) return Mismatch("A");
    if (R.Size != E.Size) return Mismatch("size");
    if (R.Align != E.Align) return Mismatch("align");
    if (R.Flags != E.Flags) return Mismatch("flags");
    if (R.BoundOnly != E.BoundOnly) return Mismatch("bound_only");
    // The section column is not a free field: it must be one of the input
    // sections the independently rebuilt source vector assigns to this
    // source triple.
    if (std::find(It->second.Sections.begin(), It->second.Sections.end(),
                  R.Section) == It->second.Sections.end())
      return Mismatch("section");
  }
  return true;
}

// V8: bidirectional enumeration, driven by the ORIGINAL records and manifest
// rows -- never by the report.
bool Verifier::enumerateBothWays() {
  for (const MergedEntity &E : Entities) {
    // The source key includes the SECTION column: a row that names the right
    // file/symbol under a different input section is not this source's row
    // (G11-D review B2/B8).
    std::set<std::tuple<std::string, std::string, std::string>> Triples;
    for (const SourceRef &S : E.Sources)
      Triples.insert({S.Path, S.ElfName, S.Section});
    for (const auto &T : Triples) {
      bool Found = false;
      for (const ReportRow &R : Report)
        if (R.Stable == E.Stable && R.File == std::get<0>(T) &&
            R.Sym == std::get<1>(T) && R.Section == std::get<2>(T)) {
          Found = true;
          break;
        }
      if (!Found) {
        // Keep V15's frozen retain diagnostic reachable (review B9): a
        // retained entity whose report row vanished is exactly the V15 case,
        // and this V8 return must not swallow it.
        if (E.Flags & FlagRetain)
          noteRetainedAbsent(E);
        return failVerify("no placement report row for placement record " +
                          E.Stable + " (" + std::get<0>(T) + ")");
      }
    }
  }
  for (const ReportRow &R : Report) {
    bool Found = false;
    for (const MergedEntity &E : Entities) {
      if (E.Stable != R.Stable)
        continue;
      for (const SourceRef &S : E.Sources)
        if (S.Path == R.File && S.ElfName == R.Sym &&
            S.Section == R.Section) {
          Found = true;
          break;
        }
      if (Found)
        break;
    }
    if (!Found)
      return failVerify("placement report row " + R.Sym +
                        " has no NOTE/manifest source");
  }
  return true;
}

// V9: each report row's OWN merged fields must hash to its stored value.  No
// cross-carrier equality between H_source and H_report is asserted.
bool Verifier::checkReportHashes() {
  for (const ReportRow &R : Report) {
    uint32_t Want =
        layoutHash(R.Class, R.Entity, R.Ownership, R.A, R.Align, R.Flags);
    if (Want != R.Hash)
      return failVerify("layout hash mismatch for " + R.Sym +
                        ": report line " + Twine(R.Line) + " stores " +
                        hex0x(R.Hash) + " but its own merged fields hash to " +
                        hex0x(Want));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Locating evidence (contract §4 / R3).
// ---------------------------------------------------------------------------
bool Verifier::loadMap() {
  if (O.LinkMap.empty())
    return failVerify("no --link-map was provided; final entity and section "
                      "positions cannot be independently located");
  auto MB = MemoryBuffer::getFile(O.LinkMap);
  if (!MB)
    return failVerify("cannot read link map " + O.LinkMap);
  StringRef C = (*MB)->getBuffer();
  bool First = true;
  while (!C.empty()) {
    auto [Line, Rest] = C.split('\n');
    C = Rest;
    if (First) {
      First = false;
      if (!Line.starts_with("MCS251 map"))
        return failVerify("malformed link map " + O.LinkMap +
                          ": missing the MCS251 map header");
      continue;
    }
    if (Line.empty())
      continue;
    if (Line.starts_with("FUNC ") || Line.starts_with("BIT ") ||
        Line.starts_with("BITBYTE ") || Line.starts_with("IRQ ") ||
        Line.starts_with("stack "))
      continue;
    SmallVector<StringRef, 4> Toks;
    Line.split(Toks, ' ', -1, false);
    if (Toks.size() == 3 && Toks[0].contains(':') && Toks[1].starts_with("0x") &&
        Toks[2].starts_with("+0x")) {
      MapRow R;
      size_t Colon = Toks[0].rfind(':');
      R.File = Toks[0].take_front(Colon).str();
      R.Section = Toks[0].drop_front(Colon + 1).str();
      uint64_t V = 0;
      if (Toks[1].drop_front(2).getAsInteger(16, V) || V > 0xffffffff)
        return failVerify("malformed link map " + O.LinkMap +
                          ": bad address in " + Line);
      R.Addr = uint32_t(V);
      V = 0;
      // The map writes both numbers through format_hex, so the size column is
      // `+0x<hex>`; the `0x` prefix must be dropped before a base-16 parse.
      if (Toks[2].drop_front(3).getAsInteger(16, V) || V > 0xffffffff)
        return failVerify("malformed link map " + O.LinkMap + ": bad size in " +
                          Line);
      R.Size = uint32_t(V);
      Map.push_back(std::move(R));
      continue;
    }
    if (Toks.size() == 3 && Toks[1] == "=" && Toks[2].starts_with("0x")) {
      uint64_t V = 0;
      // A boundary row that does not parse is MALFORMED EVIDENCE, not a row
      // to skip: the table locations V14/V16 depend on come from exactly these
      // rows, so dropping one silently would turn a locating failure into a
      // green check.
      if (Toks[2].drop_front(2).getAsInteger(16, V) || V > 0xffffffff)
        return failVerify("malformed link map " + O.LinkMap +
                          ": bad boundary value in " + Line);
      MapSynth.emplace_back(Toks[0].str(), uint32_t(V));
      continue;
    }
    // G11-D review B4: an `s_*/l_*` boundary row that does not carry a
    // parseable `0x` value is MALFORMED EVIDENCE.  It used to fall through
    // silently, which removed the row and turned a locating failure into a
    // green "no initialization path" verdict.
    if (Toks.size() >= 2 && Toks[1] == "=" &&
        (Toks[0].starts_with("s_") || Toks[0].starts_with("l_")))
      return failVerify("malformed link map " + O.LinkMap +
                        ": bad boundary value in " + Line);
  }
  return true;
}

bool Verifier::imageByte(uint32_t Addr, uint8_t &Out) const {
  for (const LoadSeg &S : Final.Loads)
    if (Addr >= S.VAddr && uint64_t(Addr) < uint64_t(S.VAddr) + S.FileSz) {
      size_t Off = size_t(S.FileOff) + (Addr - S.VAddr);
      if (Off >= Final.Buf.size())
        return false;
      Out = Final.Buf[Off];
      return true;
    }
  return false;
}

bool Verifier::imageRange(uint32_t Addr, uint32_t Len,
                          std::vector<uint8_t> &Out) const {
  Out.clear();
  for (uint32_t I = 0; I != Len; ++I) {
    uint8_t B = 0;
    if (!imageByte(Addr + I, B))
      return false;
    Out.push_back(B);
  }
  return true;
}

std::vector<const Symbol *> Verifier::outputSymbolsNamed(StringRef N) const {
  std::vector<const Symbol *> R;
  for (const Symbol &S : Final.Syms)
    if (S.Name == N && S.Type() != STT_SECTION)
      R.push_back(&S);
  return R;
}

std::optional<uint32_t> Verifier::outputAddress(StringRef Name) const {
  // A placement entity's own address is independently known from the NOTE;
  // prefer it so this never merely echoes the symtab we are auditing.
  for (const MergedEntity &E : Entities)
    if (E.Sym == Name)
      return E.A;
  for (const Symbol &S : Final.Syms)
    if (S.Name == Name && S.Shndx != 0)
      return S.Value;
  return std::nullopt;
}

// G11-D review R3-3: see the declaration for the rationale.  Mirrors the
// producer's placementTargetClass decision order without consulting any
// producer table: the reloc's own object symbol first (a DEFINED one
// classifies through its section), then the placement entities (fixed and
// bind targets carry their storage class in the NOTE/manifest), then the
// single global definition in the other inputs.
int Verifier::relocTargetSpace(StringRef Name, const InputObj &From) const {
  for (const Symbol &S : From.E.Syms)
    if (S.Name == Name) {
      if (S.Shndx != 0 && S.Shndx < From.E.Secs.size()) {
        const Section &Sec = From.E.Secs[S.Shndx];
        if (StringRef(Sec.Name).starts_with(".mcs251.XSEG"))
          return PSC_XDATA;
        if (!StringRef(Sec.Name).starts_with(FixedSectionPrefix))
          return -1; // A CSEG/DSEG/... target: the producer gate stops too.
        break;       // A fixed entity classifies through its NOTE below.
      }
      break; // Undefined here: the global definition decides.
    }
  for (const MergedEntity &E : Entities)
    if (E.Sym == Name)
      return E.Class;
  for (const InputObj &O2 : Objs)
    for (const Symbol &S : O2.E.Syms)
      if (S.Name == Name && S.Bind() != STB_LOCAL && S.Shndx != 0 &&
          S.Shndx < O2.E.Secs.size())
        return StringRef(O2.E.Secs[S.Shndx].Name).starts_with(".mcs251.XSEG")
                   ? PSC_XDATA
                   : -1;
  return -1;
}

// ---------------------------------------------------------------------------
// V10: output entity presence and value.
// ---------------------------------------------------------------------------
bool Verifier::checkOutputEntities() {
  for (MergedEntity &E : Entities) {
    std::vector<const Symbol *> Cands = outputSymbolsNamed(E.Sym);
    if (Cands.empty()) {
      // V15's frozen retain diagnostic stays reachable (review B9).
      if (E.Flags & FlagRetain)
        noteRetainedAbsent(E);
      return failVerify("no output entity for placement record " + E.Stable);
    }
    if (Cands.size() > 1) {
      // R5 fail-closed: a bare name that matches more than one output symbol
      // is only decidable when exactly one of them carries the recorded
      // address.  "Any same-name match" is never accepted as proof.
      std::vector<const Symbol *> Narrow;
      for (const Symbol *S : Cands)
        if (S->Value == E.A)
          Narrow.push_back(S);
      if (Narrow.size() != 1)
        return failVerify("ambiguous output entity for placement record " +
                          E.Stable);
      Cands = Narrow;
    }
    const Symbol &S = *Cands[0];
    // G11-D review B9: the frozen text is `VERIFY FAIL %sym: address ...` /
    // `VERIFY FAIL %sym: size ...` (the source locator is the `%sym` suffix
    // immediately after the space).  The colon added by an earlier revision
    // changed the frozen wording.
    if (S.Value != E.A)
      return fail("VERIFY FAIL " + E.Stable + ": address " + hex0x(S.Value) +
                  " != expected " + hex0x(E.A));
    if (E.BoundOnly) {
      if (S.Bind() != STB_GLOBAL || S.Type() != STT_NOTYPE ||
          S.Shndx != SHN_ABS || S.Size != 0)
        return failVerify("invalid bind-only output symbol for " + E.Stable);
    } else {
      // G11-D review B7: an OWNED output entity has a defined shape.  A
      // carrier whose symbol was turned into `undefined NOTYPE` (keeping
      // name/value/size) used to pass V10, V11 and V15.
      if (S.Shndx == 0) {
        if (E.Flags & FlagRetain)
          noteRetainedAbsent(E);
        return failVerify("invalid output symbol for placement record " +
                          E.Stable + ": not defined");
      }
      const uint8_t WantType = E.EntKind == PE_FUNCTION ? STT_FUNC : STT_OBJECT;
      if (S.Type() != WantType)
        return failVerify("invalid output symbol for placement record " +
                          E.Stable + ": the entity type disagrees with the "
                          "placement record");
    }
    // Remember the entity THIS check selected so V11/V15 cannot re-search the
    // same name and prove the span against a different candidate (B7).
    E.OutSym = &S;
  }
  return true;
}

// ---------------------------------------------------------------------------
// V11: sizes, main-symbol shape and entity spans.
// ---------------------------------------------------------------------------
bool Verifier::checkSizesAndMainSymbols() {
  for (const MergedEntity &E : Entities) {
    if (!E.OwnedObj)
      continue; // bind-only / manifest-only: address-only contracts.
    const InputObj &Obj = *E.OwnedObj;
    const NoteRecord &R = *E.OwnedRec;
    const Section *Sec = Obj.E.findByName(E.OwnedSection);
    if (!Sec)
      return failVerify("section " + E.OwnedSection +
                        " disagrees with placement NOTE for " + E.Stable);
    const Symbol *Main = nullptr;
    unsigned Ents = 0;
    for (const Symbol &S : Obj.E.Syms)
      if (S.Shndx < Obj.E.Secs.size() && &Obj.E.Secs[S.Shndx] == Sec) {
        if (S.Type() == STT_FUNC || S.Type() == STT_OBJECT) {
          ++Ents;
          Main = &S;
        } else if (!(S.Bind() == STB_LOCAL && S.Type() == STT_NOTYPE &&
                     S.Size == 0 && S.Value > 0 && S.Value < Sec->Size) &&
                   !(S.Bind() == STB_LOCAL && S.Type() == STT_SECTION &&
                     S.Size == 0 && S.Value == 0 && S.Name.empty()))
          return failVerify("section " + Sec->Name +
                            " disagrees with placement NOTE for " + E.Stable);
      }
    if (Ents != 1 || !Main)
      return failVerify("section " + Sec->Name +
                        " disagrees with placement NOTE for " + E.Stable);
    if (Main->Value != 0 ||
        (R.Entity == PE_FUNCTION) != (Main->Type() == STT_FUNC) ||
        R.Size != Sec->Size)
      return failVerify("section " + Sec->Name +
                        " disagrees with placement NOTE for " + E.Stable);
    for (const ReportRow &Row : Report)
      if (Row.Stable == E.Stable && Row.Size != E.Size)
        return failVerify("placement report field size mismatch for " +
                          E.Stable);
    // G11-D review B7: the size proof must reuse the ONE entity V10 already
    // selected (unique name match / unique address narrowing).  "Any symbol
    // of that name has the expected size" allowed proving the span against a
    // different candidate than the one whose address was verified.
    const Symbol *Out = E.OutSym;
    if (!Out)
      return failVerify("no output entity for placement record " + E.Stable);
    // The output st_size that must match: an owned object keeps its stored
    // size exactly; an owned function keeps the input function st_size.
    const uint32_t Want = R.Entity == PE_OBJECT ? Sec->Size : Main->Size;
    if (Out->Size != Want)
      return fail("VERIFY FAIL " + E.Stable + ": size " + Twine(Out->Size) +
                  " != recorded " + Twine(Want));
    if (R.Entity == PE_OBJECT) {
      if (Main->Size != Sec->Size || E.Size != Sec->Size)
        return failVerify("section " + Sec->Name +
                          " disagrees with placement NOTE for " + E.Stable);
    } else if (Main->Size > Sec->Size || E.Size != Sec->Size) {
      return failVerify("section " + Sec->Name +
                        " disagrees with placement NOTE for " + E.Stable);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// V12: the merged alignment is executed, and every entity fits its class's
// numeric representation (including the FIXED-XDATA single-window rule).
// ---------------------------------------------------------------------------
bool Verifier::checkAlignmentAndRanges() {
  for (const MergedEntity &E : Entities) {
    if (E.Align == 0 || (E.A % E.Align) != 0)
      return failVerify(E.Stable + " at " + hex0x(E.A) + " violates alignment " +
                        Twine(E.Align));
    const uint64_t End = uint64_t(E.A) + E.Size;
    auto Outside = [&](const char *Region) {
      return failVerify(E.Stable + " [" + hex0x(E.A) + "," +
                        hex0x(End) + ") outside " + Region);
    };
    if (E.Class == PSC_AS0_DATA) {
      if (End > 0x10000)
        return Outside("AS0-DATA address space");
    } else if (E.Class == PSC_XDATA) {
      if (End > 0x1000000)
        return Outside("24-bit address space");
      if (E.OwnedObj && E.Size != 0) {
        if (E.Size > 65535 || ((E.A ^ uint32_t(End - 1)) & 0xff0000) != 0)
          return Outside("XDATA 64K window");
      }
    } else {
      if (End > 0x1000000)
        return Outside("24-bit address space");
    }
    if (E.OwnedObj && E.Size == 0)
      return failVerify("zero-size entity " + E.Stable + " at " + hex0x(E.A) +
                        " is not placeable");
  }
  return true;
}

// ---------------------------------------------------------------------------
// V13: independent board-level numeric windows.  Never satisfied by the map's
// own "already placed here" claim.
// ---------------------------------------------------------------------------
bool Verifier::checkWindows() {
  for (const MergedEntity &E : Entities) {
    if (!E.HasKnownSpan)
      return failVerify("manifest-only entity " + E.Stable +
                        " has no provable span for window verification");
    const uint64_t End = uint64_t(E.A) + E.Size;
    auto Outside = [&](const char *Region) {
      return failVerify(E.Stable + " [" + hex0x(E.A) + "," +
                        hex0x(End) + ") outside " + Region);
    };
    if (E.Class == PSC_CODE) {
      if (!O.HasFlashBase || !O.HasFlashSize)
        return failVerify("fixed CODE entity " + E.Stable +
                          " requires an explicit CODE window "
                          "(--flash-base/--flash-size)");
      const uint64_t Lo = O.FlashBase, Hi = uint64_t(O.FlashBase) + O.FlashSize;
      if (E.BoundOnly && E.EntKind == PE_FUNCTION) {
        // A bind function constrains only its entry.
        if (E.A < Lo || E.A >= Hi)
          return Outside("CODE window");
        continue;
      }
      if (E.A < Lo || End > Hi)
        return Outside("CODE window");
      continue;
    }
    if (E.Class == PSC_XDATA) {
      if (O.HasXdataSize) {
        uint32_t Base = 0;
        bool HaveBase = false;
        for (const auto &P : O.AreaStarts)
          if (P.first == "XSEG") {
            Base = P.second;
            HaveBase = true;
          }
        if (!HaveBase)
          return failVerify("--xdata-size was given without "
                            "--area-start=XSEG=<base>; the XDATA capacity "
                            "window cannot be located");
        if (E.A < Base || End > uint64_t(Base) + O.XdataSize)
          return Outside("XDATA capacity window");
      }
      continue; // The always-on 24-bit range check ran in V12.
    }
    if (E.BoundOnly)
      continue; // A bind object constrains no storage.
    for (const auto &Rd : O.ReservedData)
      if (E.A < Rd.second && Rd.first < End)
        return Outside("reserved DATA range");
  }
  return true;
}

// ---------------------------------------------------------------------------
// V14: overlap accounting counts each entity ONCE, per storage space; a
// bind-only name reserves nothing.  CODE and XDATA may share numeric values.
// ---------------------------------------------------------------------------
// G11-D review B4 / R3: locate one initialization table from the FINAL ELF's
// `s_<AREA>`/`l_<AREA>` synthesized symbols, cross-checked against the link
// map's candidate boundary rows.  Both carriers must be present and must
// agree; a missing boundary is a locating FAILURE, never "no audited
// initialization path exists".
std::optional<uint32_t> Verifier::elfBoundary(StringRef Name) {
  const Symbol *Hit = nullptr;
  for (const Symbol &S : Final.Syms) {
    if (S.Name != Name || S.Shndx != SHN_ABS)
      continue;
    // G11-D review R2-5 (B4): repeated rows are only tolerable when they
    // agree; the first match must not silently win over a contradiction.
    if (Hit && Hit->Value != S.Value) {
      failVerify("malformed input ELF " + O.Elf + ": the boundary symbol " +
                 Name + " is defined with conflicting values " +
                 hex0x(Hit->Value) + " and " + hex0x(S.Value));
      return std::nullopt;
    }
    Hit = &S;
  }
  if (!Hit)
    return std::nullopt;
  return Hit->Value;
}

std::optional<uint32_t> Verifier::mapBoundary(StringRef Name) {
  bool Hit = false;
  uint32_t Value = 0;
  for (const auto &P : MapSynth) {
    if (P.first != Name)
      continue;
    // G11-D review R2-5 (B4): a map that carries the same boundary key twice
    // with different values does not uniquely locate the table; accepting the
    // first row would hide the tampering.
    if (Hit && Value != P.second) {
      failVerify("malformed link map " + O.LinkMap + ": the boundary row " +
                 Name + " appears with conflicting values " + hex0x(Value) +
                 " and " + hex0x(P.second));
      return std::nullopt;
    }
    Hit = true;
    Value = P.second;
  }
  if (!Hit)
    return std::nullopt;
  return Value;
}

bool Verifier::locateInitTable(const char *Area, InitTableLoc &Out) {
  const std::string SN = (Twine("s_") + Area).str();
  const std::string LN = (Twine("l_") + Area).str();
  auto ES = elfBoundary(SN), EL = elfBoundary(LN);
  auto MS = mapBoundary(SN), ML = mapBoundary(LN);
  if (!ES || !EL)
    return failVerify("malformed input ELF " + O.Elf + ": the " + Area +
                      " boundary symbols (s_/l_) are absent from the final "
                      "symbol table, so the initialization table cannot be "
                      "located");
  if (!MS || !ML)
    return failVerify("malformed link map " + O.LinkMap + ": the " + Area +
                      " boundary rows (s_/l_) are absent; the initialization "
                      "table location has no independent evidence");
  if (*ES != *MS || *EL != *ML)
    return failVerify("malformed link map " + O.LinkMap + ": the " + Area +
                      " boundary disagrees with the final ELF (map s=" +
                      hex0x(*MS) + " l=" + hex0x(*ML) + ", ELF s=" +
                      hex0x(*ES) + " l=" + hex0x(*EL) + ")");
  Out.Present = *EL != 0;
  Out.Start = *ES;
  Out.Length = *EL;
  Out.EndExclusive = uint64_t(*ES) + *EL; // widened; never re-add the base
  return true;
}

// G11-D review R3-1 (R2-1 residue): see the declaration for the rationale.
// Both carriers must be present and must agree; the duplicated-row and
// conflicting-value rules of elfBoundary()/mapBoundary() apply unchanged.
// The message family parallels locateInitTable's frozen wording (only the
// area name and the subject differ); the XINIT/XDATA_INIT families
// themselves go through locateInitTable directly so their frozen texts and
// Present semantics stay byte-identical.
// D2 design §10.4: a boundary pair appearing with only ONE side is refused
// with its own evidence diagnostic; only the wholly missing pair keeps the
// frozen "absent" wording above (required-region missing diagnostics stay
// byte-identical for the whole-pair case).
bool Verifier::regionBounds(const std::string &Area, uint32_t &Lo,
                            uint32_t &Len) {
  const std::string SN = (Twine("s_") + Area).str();
  const std::string LN = (Twine("l_") + Area).str();
  auto ES = elfBoundary(SN), EL = elfBoundary(LN);
  auto MS = mapBoundary(SN), ML = mapBoundary(LN);
  if (!ES || !EL) {
    if (ES || EL)
      return failVerify("malformed input ELF " + O.Elf + ": the " + Area +
                        " boundary symbol " + (ES ? LN : SN) + " appears "
                        "without its s_/l_ pair, so a dynamic section's "
                        "final position cannot be corroborated");
    return failVerify("malformed input ELF " + O.Elf + ": the " + Area +
                      " boundary symbols (s_/l_) are absent from the final "
                      "symbol table, so a dynamic section's final position "
                      "cannot be corroborated");
  }
  if (!MS || !ML) {
    if (MS || ML)
      return failVerify("malformed link map " + O.LinkMap + ": the " + Area +
                        " boundary row " + (MS ? LN : SN) + " appears "
                        "without its s_/l_ pair; the dynamic section "
                        "position has no independent evidence");
    return failVerify("malformed link map " + O.LinkMap + ": the " + Area +
                      " boundary rows (s_/l_) are absent; the dynamic "
                      "section position has no independent evidence");
  }
  if (*ES != *MS || *EL != *ML)
    return failVerify("malformed link map " + O.LinkMap + ": the " + Area +
                      " boundary disagrees with the final ELF (map s=" +
                      hex0x(*MS) + " l=" + hex0x(*ML) + ", ELF s=" +
                      hex0x(*ES) + " l=" + hex0x(*EL) + ")");
  Lo = *ES;
  Len = *EL;
  return true;
}

// ---------------------------------------------------------------------------
// G11-D2: the positioning carrier phase (design §7).  The carrier is parsed
// and cross-checked INDEPENDENTLY of the producer: no serializer, classifier
// or layout oracle is shared with it.  A final ELF without the carrier is a
// verification failure that names the regeneration path -- never a silent
// fallback to map+window positioning.
// ---------------------------------------------------------------------------
int Verifier::spaceOfArea(StringRef Area) {
  if (Area == "XSEG")
    return PSC_XDATA;
  if (Area == "DSEG" || Area == "EDATA" || Area == "ISEG" || Area == "SSEG" ||
      Area == "OSEG" || Area == "BSEG_BYTES" || Area == "BIT_BANK" ||
      Area.starts_with("REG_BANK_") || Area == "DATA")
    return PSC_AS0_DATA;
  return PSC_CODE; // HOME/VECS/BOOT/CSEG/XINIT/XDATA_INIT/BITINIT
}

// The independent ALLOC-section vocabulary (design §5.2), written from the
// frozen design text.  SHF_GNU_RETAIN is masked out everywhere: whether a
// non-fixed section may carry it is V15's frozen verdict, not this
// classifier's.  -1 means "the vocabulary does not admit this section".
ClassifiedSection Verifier::classifyAllocSection(const Section &S) {
  ClassifiedSection C;
  const StringRef N = S.Name;
  const uint64_t Fl = S.Flags & ~SHF_GNU_RETAIN;
  // R5-3: the raw 32-bit sh_type word, exactly like the carrier's own shape
  // check.  Judging on the 16-bit truncation let sh_type=0x10001 pass for a
  // PROGBITS section (and 0x10008 for a NOBITS one) even though the producer
  // refuses the object.
  const bool Progbits = S.RawType == SHT_PROGBITS;
  const bool Nobits = S.RawType == SHT_NOBITS;
  const uint64_t AW = SHF_ALLOC | SHF_WRITE;
  const uint64_t AWE = AW | SHF_MCS251_OVERLAY;
  const uint64_t AWX = AW | SHF_MCS251_XSEG_SPLIT;
  if (N == ".text" || N.starts_with(".text.")) {
    if (!Progbits || Fl != (SHF_ALLOC | SHF_EXECINSTR))
      return C;
    C.Space = PSC_CODE;
    return C;
  }
  if (N == ".rodata" || N.starts_with(".rodata.")) {
    if (!Progbits || Fl != SHF_ALLOC)
      return C;
    C.Space = PSC_CODE;
    return C;
  }
  if (N == ".mcs251.xinit" || N.starts_with(".mcs251.xinit.")) {
    if (!Progbits || Fl != SHF_ALLOC)
      return C;
    C.Space = PSC_CODE;
    return C;
  }
  if (N == ".mcs251.xdata_init" || N.starts_with(".mcs251.xdata_init.")) {
    if (!Progbits || Fl != SHF_ALLOC)
      return C;
    C.Space = PSC_CODE;
    return C;
  }
  auto CodeFragment = [&](const char *Area) {
    if (Fl != (SHF_ALLOC | SHF_EXECINSTR) || (!Progbits && !Nobits))
      return false;
    C.Space = PSC_CODE;
    return true;
  };
  if (N == ".mcs251.HOME" || N.starts_with(".mcs251.HOME."))
    return CodeFragment("HOME") ? C : C;
  if (N == ".mcs251.VECS" || N.starts_with(".mcs251.VECS."))
    return CodeFragment("VECS") ? C : C;
  if (N == ".mcs251.BOOT" || N.starts_with(".mcs251.BOOT."))
    return CodeFragment("BOOT") ? C : C;
  if (N.starts_with(".mcs251.OSEG.")) {
    if (!Nobits || Fl != AWE)
      return C;
    C.Space = PSC_AS0_DATA;
    C.IsOverlay = true;
    C.Group = "OSEG";
    return C;
  }
  if ((N == ".data" || N.starts_with(".data.")) && Progbits && S.Size == 0 &&
      Fl == AW) {
    // SPEC §4.1 / design §5.3: the candidate exemption.  The remaining
    // conditions (no defined symbol, no actual relocation) are object-level
    // and checked by the coverage walk.
    C.Space = PSC_AS0_DATA;
    C.EmptyDataExempt = true;
    return C;
  }
  if (N == ".mcs251.dseg" || N.starts_with(".mcs251.DSEG.") || N == ".data" ||
      N.starts_with(".data.") || N == ".bss" || N.starts_with(".bss.")) {
    const bool MCS = N.starts_with(".mcs251.");
    // The EDATA-movable capability bit is admitted only on the .mcs251.*
    // DSEG names (the producer's v2 contract); a generic name carrying it
    // is malformed, exactly like classifySection.
    if (!Nobits || (Fl != AW && !(MCS && Fl == (AW | SHF_MCS251_EDATA_MOVABLE))))
      return C;
    C.Space = PSC_AS0_DATA;
    C.IsMovable = (Fl & SHF_MCS251_EDATA_MOVABLE) != 0;
    return C;
  }
  if (N == ".mcs251.edata" || N.starts_with(".mcs251.EDATA.")) {
    if (!Nobits || Fl != AW)
      return C;
    C.Space = PSC_AS0_DATA;
    return C;
  }
  if (N.starts_with(".mcs251.REG_BANK_")) {
    if (!Nobits || Fl != AWE)
      return C;
    C.Space = PSC_AS0_DATA;
    C.IsOverlay = true;
    C.Group = N.substr(sizeof(".mcs251.") - 1).split('.').first.str();
    return C;
  }
  if (N.starts_with(".mcs251.BSEG_BYTES")) {
    if (!Nobits || Fl != AW)
      return C;
    C.Space = PSC_AS0_DATA;
    return C;
  }
  if (N.starts_with(".mcs251.BIT_BANK")) {
    if (!Nobits || Fl != AWE)
      return C;
    C.Space = PSC_AS0_DATA;
    C.IsOverlay = true;
    C.Group = "BIT_BANK";
    return C;
  }
  if (N.starts_with(".mcs251.ISEG")) {
    if (!Nobits || Fl != AW)
      return C;
    C.Space = PSC_AS0_DATA;
    return C;
  }
  if (N.starts_with(".mcs251.SSEG")) {
    if (!Nobits || Fl != AWE)
      return C;
    C.Space = PSC_AS0_DATA;
    C.IsOverlay = true;
    C.Group = "SSEG";
    return C;
  }
  if (N.starts_with(".mcs251.DATA.")) {
    if (!Nobits || Fl != AW)
      return C;
    C.Space = PSC_AS0_DATA;
    return C;
  }
  if (N.starts_with(".mcs251.XSEG")) {
    if (!Nobits || (Fl != AW && Fl != AWX))
      return C;
    C.Space = PSC_XDATA;
    C.IsSplit = (Fl & SHF_MCS251_XSEG_SPLIT) != 0;
    return C;
  }
  if (N.starts_with(FixedSectionPrefix)) {
    const bool CodeShape = Progbits && (Fl == SHF_ALLOC ||
                                        Fl == (SHF_ALLOC | SHF_EXECINSTR));
    const bool DataShape = Nobits && Fl == AW;
    if (!CodeShape && !DataShape)
      return C;
    C.Space = -2; // FIXED: the space comes from the placement record.
    return C;
  }
  return C; // -1: unknown or illegal ALLOC section.
}

bool Verifier::parsePositions() {
  const Section *Carrier = nullptr;
  unsigned Count = 0;
  for (const Section &S : Final.Secs)
    if (S.Name == PositionsNoteSectionName) {
      Carrier = &S;
      ++Count;
    }
  if (Count == 0)
    return failVerify("the final ELF " + O.Elf + " carries no " +
                      PositionsNoteSectionName + " positioning carrier; "
                      "regenerate the ELF with an audit option "
                      "(--placement-report or --verify-placement) enabled");
  if (Count > 1)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the section appears " + Twine(Count) + " times");
  // The frozen section shape (design §3.1), judged on the RAW header words.
  if (Carrier->RawType != SHT_NOTE || Carrier->Flags != 0 ||
      Carrier->Addr != 0 || Carrier->Link != 0 || Carrier->Info != 0 ||
      Carrier->RawAlign != 4 || Carrier->EntSize != 0)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the section is not a non-ALLOC flags=0 link=0 "
                      "info=0 raw-align=4 entsize=0 SHT_NOTE");
  if (Carrier->Offset % 4 != 0)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the section is not at a 4-byte file offset");
  if (Carrier->Size < 20 ||
      uint64_t(Carrier->Offset) + Carrier->Size > Final.Buf.size())
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the section body is out of file bounds");
  // File-byte non-reuse (design §3.1): the carrier must not share bytes with
  // the ELF header, the program/section header tables or any other section
  // with file contents.
  {
    const uint64_t CS = Carrier->Offset, CE = CS + Carrier->Size;
    auto Overlaps = [&](uint64_t S, uint64_t E) { return CS < E && S < CE; };
    if (Overlaps(0, 52))
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": the section reuses the ELF header's file bytes");
    if (Final.Phnum && Overlaps(Final.Phoff,
                                uint64_t(Final.Phoff) + 32 * Final.Phnum))
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": the section reuses the program header table's "
                        "file bytes");
    if (Overlaps(Final.Shoff, uint64_t(Final.Shoff) + 40 * Final.Secs.size()))
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": the section reuses the section header table's "
                        "file bytes");
    for (const Section &S : Final.Secs) {
      if (&S == Carrier || S.RawType == SHT_NOBITS || S.Size == 0)
        continue;
      if (Overlaps(S.Offset, uint64_t(S.Offset) + S.Size))
        return failVerify("malformed positioning carrier in " + O.Elf +
                          ": the section reuses the file bytes of " + S.Name);
    }
  }
  std::vector<uint8_t> B;
  if (!rdBytes(Final.Buf, Carrier->Offset, Carrier->Size, B))
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the section body is out of file bounds");
  // The NOTE envelope (design §3.2): exactly one note, no trailing bytes.
  uint32_t Namesz = 0, Descsz = 0, NType = 0;
  if (!rd32(B, 0, Namesz) || !rd32(B, 4, Descsz) || !rd32(B, 8, NType))
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the NOTE envelope is truncated");
  if (Namesz != 7)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": namesz " + Twine(Namesz) + " != 7");
  const uint8_t Magic[8] = {'M', 'C', 'S', '2', '5', '1', 0, 0};
  for (unsigned I = 0; I != 8; ++I)
    if (B[12 + I] != Magic[I])
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": the note name is not MCS251");
  if (NType != PositionsNoteType)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": note type " + Twine(NType) + " != 3");
  if (Descsz != B.size() - 20)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": descsz does not cover the section (trailing bytes "
                      "are not accepted)");
  // The descriptor header (design §3.3).
  uint32_t Version = 0, ObjectCount = 0, RecordCount = 0, Reserved = 0;
  if (!rd32(B, 20, Version) || !rd32(B, 24, ObjectCount) ||
      !rd32(B, 28, RecordCount) || !rd32(B, 32, Reserved))
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the descriptor header is truncated");
  if (Version != PositionsVersion)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": position_version " + Twine(Version) + " != 1");
  if (Reserved != 0)
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the descriptor reserved field is not zero");
  const uint64_t DigestEnd = 36 + uint64_t(32) * ObjectCount;
  if (ObjectCount > 0xffffffffull || DigestEnd > B.size())
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": the object fingerprint table is truncated");
  Pos.Digests.resize(ObjectCount);
  for (uint32_t I = 0; I != ObjectCount; ++I)
    std::copy(B.begin() + 36 + size_t(I) * 32,
              B.begin() + 36 + size_t(I + 1) * 32, Pos.Digests[I].begin());
  // The main records (design §3.5/§3.6): strictly ascending
  // (object_id, input_shndx), canonical slice coverage, no unconsumed bytes.
  size_t Off = DigestEnd;
  uint64_t PrevKey = 0;
  for (uint32_t RI = 0; RI != RecordCount; ++RI) {
    if (B.size() - Off < 24)
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": record " + Twine(RI) + " is truncated");
    PosRecord R;
    uint32_t RecSize = 0, SliceCount = 0;
    rd32(B, Off, RecSize);
    rd32(B, Off + 4, R.ObjectId);
    rd32(B, Off + 8, R.InputShndx);
    R.Space = B[Off + 12];
    const uint8_t R8 = B[Off + 13];
    uint16_t R16 = 0;
    rd16(B, Off + 14, R16);
    rd32(B, Off + 16, R.InputSize);
    rd32(B, Off + 20, SliceCount);
    auto Bad = [&](const Twine &Why) {
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": record " + Twine(RI) + ": " + Why);
    };
    if (R.ObjectId >= ObjectCount)
      return Bad("object_id is out of range");
    if (R.InputShndx == 0)
      return Bad("input_shndx is zero");
    if (R.Space > PSC_CODE)
      return Bad("unknown storage_space " + Twine(unsigned(R.Space)));
    if (R8 != 0 || R16 != 0)
      return Bad("a reserved field is not zero");
    if (SliceCount == 0 || SliceCount > 0xffffffffull / 12)
      return Bad("slice_count is not a legal count");
    if (RecSize != 24 + 12 * uint64_t(SliceCount))
      return Bad("record_size does not equal 24 + 12*slice_count");
    if (B.size() - Off < RecSize)
      return Bad("record_size exceeds the remaining carrier data");
    const uint64_t Key = (uint64_t(R.ObjectId) << 32) | R.InputShndx;
    if (RI != 0 && Key <= PrevKey)
      return Bad("the records are not strictly ascending by "
                 "(object_id, input_shndx)");
    PrevKey = Key;
    size_t SOff = Off + 24;
    uint64_t Sum = 0;
    for (uint32_t SI = 0; SI != SliceCount; ++SI) {
      PosSlice Sl;
      rd32(B, SOff, Sl.InputOffset);
      rd32(B, SOff + 4, Sl.FinalAddress);
      rd32(B, SOff + 8, Sl.Length);
      SOff += 12;
      if (SI != 0) {
        const PosSlice &Prev = R.Slices.back();
        if (Sl.InputOffset != Prev.InputOffset + Prev.Length)
          return Bad("a slice does not continue the input coverage");
        if (Sl.FinalAddress != Prev.FinalAddress + Prev.Length)
          return Bad("a slice's final address is not contiguous");
      } else if (Sl.InputOffset != 0) {
        return Bad("the first slice does not start at input offset 0");
      }
      if (uint64_t(Sum) + Sl.Length > 0xffffffffull)
        return Bad("the slice lengths overflow the u32 range");
      Sum += Sl.Length;
      R.Slices.push_back(Sl);
    }
    if (Sum != R.InputSize)
      return Bad("the slice lengths do not sum to input_size");
    // A non-zero record with more than one slice is only legal for the
    // canonical split-XSEG form; a zero-length record carries exactly one
    // zero slice (design §3.7).  The canonical SPLIT tiling itself is
    // checked against the section's flag in the coverage walk.
    if (R.InputSize != 0 && SliceCount == 1 && R.Slices[0].Length == 0)
      return Bad("a non-empty record carries a zero-length slice");
    if (R.InputSize == 0 && (SliceCount != 1 || R.Slices[0].Length != 0))
      return Bad("a zero-length record does not carry exactly one zero "
                 "slice");
    Pos.Records.push_back(std::move(R));
    Off += RecSize;
  }
  if (Off != B.size())
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": unconsumed bytes after the record table");
  return true;
}

bool Verifier::checkPositionObjects() {
  if (Pos.Digests.size() != Objs.size())
    return failVerify("malformed positioning carrier in " + O.Elf + ": it "
                      "lists " + Twine(Pos.Digests.size()) +
                      " objects but " + Twine(Objs.size()) +
                      " --object inputs were given; the complete ordered "
                      "object list is required");
  for (size_t I = 0; I != Objs.size(); ++I) {
    SHA256 H;
    H.update(StringRef(reinterpret_cast<const char *>(Objs[I].E.Buf.data()),
                       Objs[I].E.Buf.size()));
    std::array<uint8_t, 32> D = H.final();
    if (D != Pos.Digests[I])
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": object " + Twine(I) + " (" + Objs[I].Arg +
                        ") does not match its recorded fingerprint");
  }
  return true;
}

const PosRecord *Verifier::findPosition(uint32_t ObjId, uint32_t Shndx) const {
  for (const PosRecord &R : Pos.Records)
    if (R.ObjectId == ObjId && R.InputShndx == Shndx)
      return &R;
  return nullptr;
}

bool Verifier::resolveInputRange(uint32_t ObjId, uint32_t Shndx,
                                 uint32_t InputOffset, uint32_t Width,
                                 uint32_t &FinalAddr) const {
  const PosRecord *R = findPosition(ObjId, Shndx);
  if (!R)
    return false;
  for (const PosSlice &Sl : R->Slices) {
    const uint64_t Begin = Sl.InputOffset;
    const uint64_t End = Begin + Sl.Length;
    if (InputOffset >= Begin && InputOffset <= End &&
        uint64_t(InputOffset) + Width <= End) {
      FinalAddr = Sl.FinalAddress + (InputOffset - Sl.InputOffset);
      return true;
    }
  }
  return false;
}

bool Verifier::checkPositionsCoverage() {
  // Bidirectional coverage (design §5.3): every legal ALLOC input section
  // has exactly one record, every record belongs to a legal ALLOC section.
  std::set<std::pair<uint32_t, uint32_t>> Consumed;
  for (const PosRecord &R : Pos.Records) {
    if (!Consumed.insert({R.ObjectId, R.InputShndx}).second)
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": duplicate record key");
    if (R.ObjectId >= Objs.size())
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": object_id " + Twine(R.ObjectId) +
                        " exceeds the object list");
    const InputObj &Obj = Objs[R.ObjectId];
    if (R.InputShndx >= Obj.E.Secs.size())
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": object " + Twine(R.ObjectId) + " (" + Obj.Arg +
                        ") has no section index " + Twine(R.InputShndx));
    const Section &IS = Obj.E.Secs[R.InputShndx];
    if (!(IS.Flags & SHF_ALLOC))
      return failVerify("malformed positioning carrier in " + O.Elf +
                        ": object " + Twine(R.ObjectId) + " (" + Obj.Arg +
                        ") section " + Twine(R.InputShndx) +
                        " is not an ALLOC section");
  }
  // The window parameters, computed with the R4-2 formula (design §8.1):
  // an explicit --iram-size of 0 means "no custom length" (cap 0x80/0x100),
  // and the default when absent is 128.
  const uint32_t Iram = O.HasIramSize ? O.IramSize : 128;
  auto AreaStartVal = [&](const char *K) -> uint32_t {
    for (const auto &P : O.AreaStarts)
      if (P.first == K)
        return P.second;
    return 0;
  };
  const uint32_t DsegStart = AreaStartVal("DSEG");
  const uint32_t DsegEnd =
      Iram > 0 && DsegStart < 0x80 && Iram <= 0x80 - DsegStart
          ? DsegStart + Iram : 0x80;
  const uint32_t IsegStart = AreaStartVal("ISEG");
  const uint32_t IsegEnd =
      Iram > 0 && IsegStart < 0x100 && Iram <= 0x100 - IsegStart
          ? IsegStart + Iram : 0x100;
  const uint32_t EdataEnd = O.HasEdataEnd ? O.EdataEnd : 0x0fff;
  const uint32_t EdataLo = std::max<uint32_t>(0x100, DsegStart);

  for (size_t ObjId = 0; ObjId != Objs.size(); ++ObjId) {
    const InputObj &Obj = Objs[ObjId];
    for (uint32_t Idx = 1; Idx != Obj.E.Secs.size(); ++Idx) {
      const Section &IS = Obj.E.Secs[Idx];
      if (!(IS.Flags & SHF_ALLOC))
        continue;
      ClassifiedSection C = classifyAllocSection(IS);
      if (C.EmptyDataExempt) {
        // Verify EVERY exemption condition independently (design §5.3);
        // only size=0 is not enough.
        for (const Symbol &S : Obj.E.Syms)
          if (S.Shndx == Idx && S.Shndx != 0)
            return failVerify("the empty PROGBITS section " + IS.Name +
                              " of " + Obj.Arg + " carries a defined symbol; "
                              "the .data exemption does not apply");
        for (const Section &RS : Obj.E.Secs)
          if (RS.Type == SHT_RELA && RS.Info == Idx && RS.Size != 0)
            return failVerify("the empty PROGBITS section " + IS.Name +
                              " of " + Obj.Arg + " is targeted by "
                              "relocations; the .data exemption does not "
                              "apply");
        if (findPosition(uint32_t(ObjId), Idx))
          return failVerify("malformed positioning carrier in " + O.Elf +
                            ": the exempt empty .data section " + IS.Name +
                            " of " + Obj.Arg + " must not carry a record");
        continue; // No record, no window; the map zero row is checked later.
      }
      if (C.Space == -1)
        return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                          " cannot be classified into a storage space");
      if (C.Space == -2) {
        // FIXED: the space comes from the associated placement record
        // (design §5.2); an owned fixed section without one is V6's own
        // verdict, so here it can only be unclassifiable.
        const MergedEntity *E = nullptr;
        for (const MergedEntity &M : Entities)
          if (M.OwnedObj == &Obj && M.OwnedSecIdx == Idx) {
            E = &M;
            break;
          }
        if (!E)
          return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                            " cannot be tied to a placement record, so its "
                            "storage space is undecidable");
        C.Space = E->Class;
      }
      const PosRecord *R = findPosition(uint32_t(ObjId), Idx);
      if (!R)
        return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                          " has no positioning record, so its final storage "
                          "position is unverified");
      Consumed.erase({uint32_t(ObjId), Idx});
      if (R->Space != C.Space)
        return failVerify("the positioning record for " + IS.Name + " of " +
                          Obj.Arg + " claims storage space " +
                          Twine(unsigned(R->Space)) + " but the section "
                          "classifies as " + Twine(C.Space));
      if (R->InputSize != IS.Size)
        return failVerify("the positioning record for " + IS.Name + " of " +
                          Obj.Arg + " records " + Twine(R->InputSize) +
                          " bytes but the input section is " +
                          Twine(IS.Size) + " bytes");
      const uint32_t Addr = R->Slices.front().FinalAddress;
      const uint64_t End = uint64_t(Addr) + R->InputSize; // widened
      if (StringRef(IS.Name).starts_with(FixedSectionPrefix)) {
        // §7.4: the owned fixed section's base and length must agree with
        // the independently merged placement entity; its space and window
        // rules are V12/V13's verdicts.
        const MergedEntity *E = nullptr;
        for (const MergedEntity &M : Entities)
          if (M.OwnedObj == &Obj && M.OwnedSecIdx == Idx) {
            E = &M;
            break;
          }
        if (E && (R->Slices.size() != 1 || Addr != E->A ||
                  R->InputSize != E->Size))
          return failVerify("the positioning record for " + IS.Name + " of " +
                            Obj.Arg + " disagrees with the placement entity " +
                            E->Stable);
        continue;
      }
      // The canonical slicing (design §3.7): identical layouts have exactly
      // one byte representation.  A plain (non-split) section carries a
      // single slice; a marked XSEG carries exactly the REGENERATED split
      // tiling -- an arbitrary re-tiling of the same continuous range is a
      // malformed record, never an alternative spelling.  Zero-length
      // records keep their single zero slice (enforced at parse).
      if (R->InputSize != 0) {
        if (!C.IsSplit && R->Slices.size() != 1)
          return failVerify("the positioning record for " + IS.Name + " of " +
                            Obj.Arg + " is split into " +
                            Twine(R->Slices.size()) +
                            " slices but the section carries no XSEG-split "
                            "mark");
        if (C.IsSplit) {
          uint64_t Off = 0, Cur = Addr, Rem = R->InputSize;
          unsigned SI = 0;
          bool Canonical = true;
          while (Rem != 0) {
            const uint64_t WindowEnd = (Cur & 0xff0000ull) + 0x10000;
            const uint64_t Len =
                std::min(std::min(Rem, uint64_t(0xffff)), WindowEnd - Cur);
            if (SI >= R->Slices.size() ||
                R->Slices[SI].InputOffset != Off ||
                R->Slices[SI].FinalAddress != Cur ||
                R->Slices[SI].Length != Len) {
              Canonical = false;
              break;
            }
            Off += Len;
            Cur += Len;
            Rem -= Len;
            ++SI;
          }
          if (Canonical && SI != R->Slices.size())
            Canonical = false;
          if (!Canonical)
            return failVerify("the positioning record for " + IS.Name +
                              " of " + Obj.Arg + " does not carry the "
                              "canonical XSEG-split tiling");
        }
      }
      // Actual space range (design §10.1): widened end, never truncated.
      const uint64_t SpaceEnd = C.Space == PSC_AS0_DATA ? 0x10000ull
                                                        : 0x1000000ull;
      if (End > SpaceEnd)
        return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                          " is placed at " + hex0x(Addr) + " with length " +
                          Twine(R->InputSize) + ", ending at " +
                          hex0x(End) + ", outside its storage "
                          "space");
      // Zero-length records carry a cursor; only the space applies to the
      // storage accounting.  R5-4: an EXPLICIT per-section pin is a
      // placement constraint, not a storage-occupancy claim, so it still
      // applies here -- a pinned zero-length section whose record was moved
      // away from the pin is malformed.
      if (R->InputSize == 0) {
        for (const auto &P : O.AreaStarts)
          if (P.first == IS.Name && Addr != P.second)
            return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                              " is placed at " + hex0x(Addr) +
                              " but the pinned --area-start value is " +
                              hex0x(P.second));
        continue;
      }
      // Family windows (design §8.2/§10.2), independent of the map.
      const StringRef N = IS.Name;
      auto In = [&](uint64_t Lo, uint64_t Hi) {
        return uint64_t(Addr) >= Lo && End <= Hi;
      };
      auto Outside = [&](const char *What) {
        return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                          " is placed at " + hex0x(Addr) +
                          " but outside its allocation window (" + What +
                          ")");
      };
      if (N.starts_with(".mcs251.XSEG")) {
        // Unmarked XSEG: one 64K window, at most 65535 bytes.  Marked:
        // the canonical tiling was validated at parse; only the 24-bit
        // space and the optional capacity window apply.
        if (!C.IsSplit) {
          if (R->InputSize > 0xffff)
            return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                              " exceeds the 65535-byte single-object limit");
          if (R->Slices.size() != 1)
            return failVerify("the positioning record for " + IS.Name +
                              " of " + Obj.Arg + " is split without the "
                              "XSEG-split mark");
          if (((Addr ^ (End - 1)) & 0xff0000ull) != 0)
            return Outside("the XDATA 64K window");
        }
        // R5-1: a marked XSEG whose canonical tiling is a SINGLE slice is a
        // legal result, not an error -- the frozen regeneration algorithm
        // (§3.7) yields one slice whenever the object starts far enough from
        // the window end (e.g. a 4-byte object at 0x010000).  The tiling was
        // already recomputed and compared above; there is no separate
        // "at least two slices" condition.
        for (const PosSlice &Sl : R->Slices)
          if (Sl.Length > 0xffff)
            return failVerify("the positioning record for " + IS.Name +
                              " of " + Obj.Arg + " carries a slice longer "
                              "than 0xffff");
        if (O.HasXdataSize) {
          uint32_t Base = 0;
          bool HaveBase = false;
          for (const auto &P : O.AreaStarts)
            if (P.first == "XSEG") {
              Base = P.second;
              HaveBase = true;
            }
          if (HaveBase && !In(Base, uint64_t(Base) + O.XdataSize))
            return Outside("the --xdata-size capacity window");
        }
      } else if (N == ".mcs251.dseg" || N.starts_with(".mcs251.DSEG.") ||
                 N == ".data" || N.starts_with(".data.") || N == ".bss" ||
                 N.starts_with(".bss.")) {
        // §8.2: the EDATA window is only open to a section the producer
        // marked EDATA-movable; an unmarked DSEG never gets the union.
        if (In(DsegStart, DsegEnd))
          ;
        else if (C.IsMovable && In(EdataLo, uint64_t(EdataEnd) + 1))
          ;
        else
          return Outside("the DSEG low window");
      } else if (N == ".mcs251.edata" || N.starts_with(".mcs251.EDATA.")) {
        if (!In(EdataLo, uint64_t(EdataEnd) + 1))
          return Outside("the EDATA window");
      } else if (N.starts_with(".mcs251.ISEG")) {
        if (!In(IsegStart, IsegEnd))
          return Outside("the ISEG window");
      } else if (N.starts_with(".mcs251.SSEG")) {
        if (!In(IsegStart, IsegEnd))
          return Outside("the SSEG window");
      } else if (N.starts_with(".mcs251.OSEG.")) {
        if (!In(DsegStart, DsegEnd))
          return Outside("the DSEG low window");
      } else if (N.starts_with(".mcs251.BSEG_BYTES")) {
        if (!In(0x20, 0x30))
          return Outside("the bit byte window");
      } else if (N.starts_with(".mcs251.BIT_BANK")) {
        if (!In(0x20, 0x30))
          return Outside("the bit bank window");
      } else if (N.starts_with(".mcs251.REG_BANK_")) {
        const StringRef G = C.Group;
        const bool OkBank = G.size() == 10 &&
                            G.starts_with("REG_BANK_") && G[9] >= '0' &&
                            G[9] <= '3';
        const unsigned Bank = OkBank ? unsigned(G[9] - '0') : 0;
        if (!OkBank)
          return failVerify("register bank out of range in " + IS.Name +
                            " of " + Obj.Arg);
        if (R->InputSize > 8 || !In(8 * Bank, 8 * Bank + 8))
          return Outside("the register bank window");
      } else if (N.starts_with(".mcs251.DATA.")) {
        // Pinned by its own section name; the exact CLI number is the
        // anchor (design §5.2 FIXED/DATA_ABS row).
        bool Pinned = false;
        uint32_t Pin = 0;
        for (const auto &P : O.AreaStarts)
          if (P.first == IS.Name) {
            Pinned = true;
            Pin = P.second;
          }
        if (!Pinned)
          return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                            " has no --area-start pin, so its final "
                            "position cannot be corroborated");
        if (Addr != Pin)
          return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                            " is placed at " + hex0x(Addr) +
                            " but the pinned --area-start value is " +
                            hex0x(Pin));
      }
      // A per-section pin beats every other anchor where one exists.
      for (const auto &P : O.AreaStarts)
        if (P.first == IS.Name && Addr != P.second)
          return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                            " is placed at " + hex0x(Addr) +
                            " but the pinned --area-start value is " +
                            hex0x(P.second));
      // Supplementary region-summary consistency (design §7.7(4)): the span
      // families must lie inside their region boundary.  The record itself
      // (already checked above) is the position evidence; this only ties the
      // located section to the region summary the final ELF publishes.
      {
        StringRef Area = "";
        if (N.starts_with(".mcs251.XSEG"))
          Area = "XSEG";
        else if (N == ".text" || N.starts_with(".text.") ||
                 N == ".rodata" || N.starts_with(".rodata."))
          Area = "CSEG";
        else if (N == ".mcs251.xinit" || N.starts_with(".mcs251.xinit."))
          Area = "XINIT";
        else if (N == ".mcs251.xdata_init" ||
                 N.starts_with(".mcs251.xdata_init."))
          Area = "XDATA_INIT";
        else if (N == ".mcs251.HOME" || N.starts_with(".mcs251.HOME."))
          Area = "HOME";
        else if (N == ".mcs251.VECS" || N.starts_with(".mcs251.VECS."))
          Area = "VECS";
        else if (N == ".mcs251.BOOT" || N.starts_with(".mcs251.BOOT."))
          Area = "BOOT";
        if (!Area.empty()) {
          uint64_t RLo = 0, RHi = 0;
          if (Area == "XINIT" || Area == "XDATA_INIT") {
            InitTableLoc T;
            if (!locateInitTable(std::string(Area).c_str(), T))
              return false;
            RLo = T.Start;
            RHi = T.Present ? T.EndExclusive : T.Start;
          } else {
            uint32_t S = 0, L = 0;
            if (!regionBounds(std::string(Area), S, L))
              return false;
            RLo = S;
            RHi = uint64_t(S) + L;
          }
          if (!In(RLo, RHi))
            return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                              " is placed at " + hex0x(Addr) +
                              " but outside the final ELF's " + Area +
                              " region [" + hex0x(uint32_t(RLo)) + "," +
                              hex0x(RHi) + ")");
        }
      }
    }
  }
  // Every record was consumed by exactly one ALLOC section.
  if (!Consumed.empty())
    return failVerify("malformed positioning carrier in " + O.Elf +
                      ": a record does not correspond to any allocated "
                      "input section");
  return true;
}

// R4-4 (design §10): the independent region-boundary range table.  The
// values' AGREEMENT between the ELF and the map is necessary but not
// sufficient: each present boundary pair must itself be representable and
// inside its storage space, with the semantics each l_<AREA> actually has.
bool Verifier::checkRegionRanges() {
  const uint32_t Iram = O.HasIramSize ? O.IramSize : 128;
  auto AreaStartVal = [&](const char *K) -> uint32_t {
    for (const auto &P : O.AreaStarts)
      if (P.first == K)
        return P.second;
    return 0;
  };
  const uint32_t DsegStart = AreaStartVal("DSEG");
  const uint32_t DsegEnd =
      Iram > 0 && DsegStart < 0x80 && Iram <= 0x80 - DsegStart
          ? DsegStart + Iram : 0x80;
  const uint32_t IsegStart = AreaStartVal("ISEG");
  const uint32_t IsegEnd =
      Iram > 0 && IsegStart < 0x100 && Iram <= 0x100 - IsegStart
          ? IsegStart + Iram : 0x100;
  // §8.1: an invalid window is an invalid CONFIGURATION, never skipped just
  // because no family member exists (the producer judges it the same way).
  if (DsegStart >= DsegEnd)
    return failVerify("invalid DSEG allocation window: start " +
                      hex0x(DsegStart) + ", end " + hex0x(DsegEnd));
  if (IsegStart >= IsegEnd)
    return failVerify("invalid ISEG allocation window: start " +
                      hex0x(IsegStart) + ", end " + hex0x(IsegEnd));
  // l_IRAM is a configuration statistic with no s_ counterpart (design
  // §10.3); it is checked against the formula, never paired.
  if (auto L = elfBoundary("l_IRAM")) {
    const uint32_t Want = Iram > 0 && Iram <= 0x100 ? Iram : 0x100;
    if (*L != Want)
      return failVerify("malformed input ELF " + O.Elf + ": l_IRAM is " +
                        hex0x(*L) + " but the --iram-size configuration "
                        "makes it " + hex0x(Want));
    if (auto ML = mapBoundary("l_IRAM"); ML && *ML != *L)
      return failVerify("malformed link map " + O.LinkMap + ": the l_IRAM "
                        "boundary disagrees with the final ELF");
  }
  struct AreaInfo {
    const char *Name;
    uint8_t Kind; // 0=CODE span, 1=XSEG span, 2=AS0 misc span,
                  // 3=DSEG used, 4=ISEG total, 5=EDATA total,
                  // 6=group overlay, 7=reg bank, 8=bit span, 9=SSEG reserve
  };
  static const AreaInfo Areas[] = {
      {"HOME", 0},       {"VECS", 0},      {"BOOT", 0},      {"CSEG", 0},
      {"XINIT", 0},      {"XDATA_INIT", 0}, {"BITINIT", 0},
      {"XSEG", 1},       {"DSEG", 3},      {"ISEG", 4},      {"EDATA", 5},
      {"OSEG", 6},       {"BIT_BANK", 8},  {"BSEG_BYTES", 8},
      {"REG_BANK_0", 7}, {"REG_BANK_1", 7}, {"REG_BANK_2", 7},
      {"REG_BANK_3", 7}, {"SSEG", 9},
  };
  for (const AreaInfo &A : Areas) {
    const std::string SN = (Twine("s_") + A.Name).str();
    const std::string LN = (Twine("l_") + A.Name).str();
    auto ES = elfBoundary(SN), EL = elfBoundary(LN);
    auto MS = mapBoundary(SN), ML = mapBoundary(LN);
    if (!ES && !EL && !MS && !ML)
      continue; // The area does not participate in this link.
    // Pair completeness (design §10.4): one side of the pair alone is
    // malformed evidence.
    if ((!ES && !MS) || (!EL && !ML))
      return failVerify("malformed input ELF " + O.Elf + ": the " + A.Name +
                        " boundary appears without its s_/l_ pair");
    // The XINIT/XDATA_INIT families keep their frozen locating diagnostics
    // FIRST (missing/inconsistent), then gain the range diagnostics.
    if (A.Kind == 0 && (StringRef(A.Name) == "XINIT" ||
                        StringRef(A.Name) == "XDATA_INIT")) {
      InitTableLoc T;
      if (!locateInitTable(A.Name, T))
        return false;
    }
    // Agreement ELF <-> map for every other present area (regionBounds
    // owns the frozen message family).
    uint32_t S = 0, L = 0;
    if (!(StringRef(A.Name) == "XINIT" || StringRef(A.Name) == "XDATA_INIT")) {
      if (!regionBounds(A.Name, S, L))
        return false;
    } else {
      InitTableLoc T;
      if (!locateInitTable(A.Name, T))
        return false;
      S = T.Start;
      L = T.Length;
    }
    const uint64_t End = uint64_t(S) + L; // widened, never truncated
    auto RangeBad = [&](uint64_t Limit, const char *What) {
      return failVerify("malformed input ELF " + O.Elf + ": the " + A.Name +
                        " boundary spans [" + hex0x(S) + "," + hex0x(End) +
                        ") (start + length, widened), which leaves " + What);
    };
    switch (A.Kind) {
    case 0: // CODE-family span.
      if (End > 0x1000000ull)
        return RangeBad(0x1000000ull, "the CODE address space");
      break;
    case 1: // XSEG span.
      if (End > 0x1000000ull)
        return RangeBad(0x1000000ull, "the XDATA address space");
      break;
    case 3: // DSEG: bytes used below 0x80; never a continuous span.
      if (S != 0)
        return failVerify("malformed input ELF " + O.Elf + ": s_DSEG is " +
                          hex0x(S) + " but the DSEG boundary counts bytes "
                          "used below 0x80 (s_DSEG must be 0)");
      if (L > 0x80)
        return failVerify("malformed input ELF " + O.Elf + ": l_DSEG is " +
                          hex0x(L) + " but the low window holds at most "
                          "0x80 bytes");
      break;
    case 4: // ISEG: sum of slice sizes; s_ is the first non-empty member.
    case 5: { // EDATA: sum of member sizes.
      const bool IsIseg = A.Kind == 4;
      uint64_t Total = 0;
      const PosRecord *FirstNonEmpty = nullptr;
      const PosRecord *First = nullptr;
      for (const PosRecord &R : Pos.Records) {
        if (R.ObjectId >= Objs.size() ||
            R.InputShndx >= Objs[R.ObjectId].E.Secs.size())
          continue;
        const Section &IS = Objs[R.ObjectId].E.Secs[R.InputShndx];
        const StringRef N = IS.Name;
        const bool Member =
            IsIseg ? N.starts_with(".mcs251.ISEG")
                   : (N == ".mcs251.edata" || N.starts_with(".mcs251.EDATA.") ||
                      // A migrated DSEG slice reports Region EDATA; its name
                      // stays a DSEG name, and only the movable-marked ones
                      // may sit in the window (checked per record).
                      ((N == ".mcs251.dseg" || N.starts_with(".mcs251.DSEG.")) &&
                       (IS.Flags & SHF_MCS251_EDATA_MOVABLE) != 0 &&
                       R.Slices.front().FinalAddress >= 0x100));
        if (!Member)
          continue;
        if (!First)
          First = &R;
        if (R.InputSize) {
          Total += R.InputSize;
          if (!FirstNonEmpty)
            FirstNonEmpty = &R;
        }
      }
      const uint32_t WantS =
          (FirstNonEmpty ? FirstNonEmpty : First)
              ? (FirstNonEmpty ? FirstNonEmpty->Slices.front().FinalAddress
                              : First->Slices.front().FinalAddress)
              : 0;
      if (S != WantS || L != Total)
        return failVerify("malformed input ELF " + O.Elf + ": the " + A.Name +
                          " boundary is s=" + hex0x(S) + " l=" + hex0x(L) +
                          " but the located members make it s=" +
                          hex0x(WantS) + " l=" + hex0x(uint32_t(Total)));
      break;
    }
    case 6: { // OSEG overlay group: shared base, max occupancy.
      // R5-5: the boundary's OWN range is a property of the published pair,
      // not of the members.  A pair whose widened end leaves the DSEG low
      // window is malformed even when the link has no OSEG member at all
      // (§14.7 "an existing boundary that no dynamic row references").
      if (S < DsegStart || uint64_t(S) + L > DsegEnd)
        return RangeBad(DsegEnd, "the DSEG low window");
      uint32_t Max = 0;
      const PosRecord *Any = nullptr;
      for (const PosRecord &R : Pos.Records) {
        if (R.ObjectId >= Objs.size() ||
            R.InputShndx >= Objs[R.ObjectId].E.Secs.size())
          continue;
        const Section &IS = Objs[R.ObjectId].E.Secs[R.InputShndx];
        if (!StringRef(IS.Name).starts_with(".mcs251.OSEG."))
          continue;
        if (Any && R.Slices.front().FinalAddress != S)
          return failVerify("malformed input ELF " + O.Elf + ": the OSEG "
                            "members do not share the group base");
        Any = &R;
        Max = std::max(Max, R.InputSize);
      }
      if (Any && (S != Any->Slices.front().FinalAddress || L != Max))
        return failVerify("malformed input ELF " + O.Elf + ": the OSEG "
                          "boundary is s=" + hex0x(S) + " l=" + hex0x(L) +
                          " but the group base is " +
                          hex0x(Any->Slices.front().FinalAddress) +
                          " with max occupancy " + Twine(Max));
      break;
    }
    case 7: { // REG_BANK_n: base 8n, at most 8 bytes.
      const unsigned Bank = unsigned(A.Name[9] - '0');
      if (S != 8 * Bank)
        return failVerify("malformed input ELF " + O.Elf + ": s_" + A.Name +
                          " is " + hex0x(S) + " but the bank starts at " +
                          hex0x(8 * Bank));
      if (L > 8)
        return failVerify("malformed input ELF " + O.Elf + ": l_" + A.Name +
                          " is " + hex0x(L) + " but a register bank holds at "
                          "most 8 bytes");
      break;
    }
    case 8: // BIT_BANK / BSEG_BYTES within [0x20, 0x30).
      if (S < 0x20 || End > 0x30)
        return RangeBad(0x30, "the bit byte window [0x20,0x30)");
      break;
    case 9: // SSEG: the complete reservation inside the ISEG window.
      if (uint64_t(S) < IsegStart || End > IsegEnd)
        return RangeBad(IsegEnd, "the ISEG window");
      break;
    }
  }
  return true;
}

// §7.3: the map cross-check.  Every expected input row matches exactly once;
// no extra, missing or duplicated rows; same-(path, name) occurrences follow
// the object ordinal + shndx order.
bool Verifier::checkPositionsMap() {
  struct Expect {
    uint32_t Addr = 0, Size = 0;
  };
  std::map<std::pair<std::string, std::string>, std::vector<Expect>> Want;
  for (size_t ObjId = 0; ObjId != Objs.size(); ++ObjId) {
    const InputObj &Obj = Objs[ObjId];
    for (uint32_t Idx = 1; Idx != Obj.E.Secs.size(); ++Idx) {
      const Section &IS = Obj.E.Secs[Idx];
      if (!(IS.Flags & SHF_ALLOC))
        continue;
      const PosRecord *R = findPosition(uint32_t(ObjId), Idx);
      Expect E;
      if (R) {
        E.Addr = R->Slices.front().FinalAddress;
        E.Size = R->InputSize;
      } else {
        // The verified empty-.data exemption: the map's zero row.
        E.Addr = 0;
        E.Size = 0;
      }
      Want[{Obj.Abs, IS.Name}].push_back(E);
    }
  }
  // Group the parsed map rows by the same key, in file order.
  std::map<std::pair<std::string, std::string>, std::vector<const MapRow *>>
      Got;
  for (const MapRow &R : Map) {
    const InputObj *Owner = nullptr;
    for (const InputObj &O2 : Objs)
      if (R.File == O2.Arg || R.File == O2.Abs || absPath(R.File) == O2.Abs) {
        Owner = &O2;
        break;
      }
    if (!Owner)
      return failVerify("malformed link map " + O.LinkMap + ": the section " +
                        R.Section + " of " + R.File +
                        " cannot be tied to any --object input");
    Got[{Owner->Abs, R.Section}].push_back(&R);
  }
  for (const auto &K : Got) {
    if (!Want.count(K.first))
      return failVerify("malformed link map " + O.LinkMap + ": the section " +
                        K.first.second + " of " + K.first.first +
                        " is not an allocated input section, so its row has "
                        "no positioning counterpart");
  }
  for (const auto &P : Want) {
    const std::string &File = P.first.first, &SecName = P.first.second;
    auto It = Got.find(P.first);
    const std::vector<const MapRow *> Rows =
        It == Got.end() ? std::vector<const MapRow *>() : It->second;
    if (Rows.size() != P.second.size())
      return failVerify("malformed link map " + O.LinkMap + ": the section " +
                        SecName + " of " +
                        (Rows.empty() ? File : Rows.front()->File) + " has " +
                        Twine(Rows.size()) + " row(s) but the input set has " +
                        Twine(P.second.size()) + " occurrence(s)");
    for (size_t I = 0; I != Rows.size(); ++I) {
      const MapRow &R = *Rows[I];
      const Expect &E = P.second[I];
      if (R.Size != E.Size)
        return failVerify("malformed link map " + O.LinkMap + ": the section " +
                          SecName + " of " + R.File + " is recorded with size " +
                          Twine(R.Size) + " but the input section is " +
                          Twine(E.Size) + " bytes");
      if (R.Addr != E.Addr)
        return failVerify("malformed link map " + O.LinkMap + ": the section " +
                          SecName + " of " + R.File + " is placed at " +
                          hex0x(R.Addr) +
                          " but the positioning carrier places it at " +
                          hex0x(E.Addr));
    }
  }
  return true;
}

// §7.4: named definitions in ALLOC input sections are checked against the
// final symbol table at final_address + st_value -- never at a map address.
//
// R5-2 (BLOCKER): "some same-name symbol happens to sit at the expected
// value" is NOT a proof of source.  R6-1 (BLOCKER): a second same-name
// output is not, by itself, proof of AMBIGUITY either.  The source of a
// file-scoped definition is established when the input's own retained shape
// plus any applicable independent address constraint single out one output
// symbol:
//   * the expected value must select EXACTLY ONE output symbol of that name
//     (a second symbol at the same value is ambiguous, mirroring V10's own
//     R5 fail-closed narrow rule);
//   * among the same-name output symbols, the input definition's TYPE and
//     SIZE (st_size) must single out exactly one, and it must be the symbol
//     at the expected value.  Two OBJECT locals of different sizes, an
//     OBJECT and a NOTYPE definition, or an OBJECT and a FUNC in different
//     address spaces are therefore distinguished by output fields that the
//     producer already retains, with no new output symbol index protocol;
//   * an owned `.mcu.fixed.*` section (EntityAnchored) or an explicit
//     per-section `--area-start` pin is an independent address constraint:
//     the position is corroborated by that association, so the symbol at the
//     expected value is the source even when a shape-identical twin exists
//     in another independently pinned section.
// Only when none of these establish a unique source does the check fail
// closed (design §7.4, R5/R6).  No output symbol index protocol is
// introduced.
// Rule A (matrix §4-§7): the retained symbol SHAPE (type, binding, specified
// size) defines the candidate set H FIRST; the value W and the independent
// anchor K (per-section pin / owned entity) only narrow H afterwards.  A
// shared numeric address is never by itself a reason to reject -- only an
// un-disambiguable shape set is.  `size == 0` on either side is "unspecified"
// and never discriminates.  Rows keep their multiplicity: an exact duplicate
// output definition is refused, never silently deduplicated, and two input
// definitions may not be matched onto one output row.
bool Verifier::checkPositionsSymbols() {
  struct Def {
    const InputObj *Obj = nullptr;
    const Section *IS = nullptr;
    const Symbol *S = nullptr;
    uint32_t Base = 0;
    uint64_t W = 0;
    bool Pinned = false;
    uint32_t PinValue = 0;
    const MergedEntity *Owned = nullptr; // fixed section's entity, or null
    bool OwnedMain = false;              // entity's main OBJECT/FUNC symbol
  };
  // shapeCompatible (§5.2): defined + same binding + same type + size policy.
  auto SizeCompatible = [](const Symbol &I, const Symbol &F) {
    return I.Size == 0 || F.Size == 0 || I.Size == F.Size;
  };
  // "Defined" is NOT merely `Shndx != 0` (matrix §5.2: an invalid output
  // section index is a definition-shape error and can never be a candidate).
  // A definition must name either a real final output section or the ABS
  // pseudo-section.  ABS stays legal (it is a definition and is not a space
  // or source discriminator); a normal section definition stays legal.  An
  // out-of-range ordinary index, SHN_UNDEF, and the other reserved values
  // (e.g. SHN_COMMON/SHN_XINDEX, which this frozen output format never
  // emits) are not definitions here.
  //
  // R9-1: an ordinary section definition must satisfy BOTH the ordinary
  // index domain and the actual table extent.  Testing only
  // `Shndx < Final.Secs.size()` lets the physical section count re-interpret
  // a reserved value (SHN_COMMON, SHN_XINDEX, ...) as an ordinary index once
  // the table grows past it; the reserved range is fixed by the ELF format,
  // not by how many headers happen to be present.  When this frozen output
  // format has no large-section-index representation, such an index is a
  // structural definition-shape error, never an accepted candidate.
  auto DefinedOut = [&](const Symbol &F) {
    return F.Shndx == SHN_ABS ||
           (F.Shndx != 0 && F.Shndx < SHN_LORESERVE &&
            F.Shndx < Final.Secs.size());
  };
  auto ShapeCompatible = [&](const Symbol &I, const Symbol &F) {
    return DefinedOut(F) && F.Bind() == I.Bind() && F.Type() == I.Type() &&
           SizeCompatible(I, F);
  };
  auto ExactDup = [](const Symbol &A, const Symbol &B) {
    return A.Name == B.Name && A.Value == B.Value && A.Size == B.Size &&
           A.Info == B.Info && A.Other == B.Other && A.Shndx == B.Shndx;
  };

  // Phase 1: collect the applicable input definitions in (object_id, original
  // shndx, symbol) order, each with its record base and independent anchors.
  std::vector<Def> Defs;
  for (size_t ObjId = 0; ObjId != Objs.size(); ++ObjId) {
    const InputObj &Obj = Objs[ObjId];
    for (uint32_t Idx = 1; Idx != Obj.E.Secs.size(); ++Idx) {
      const Section &IS = Obj.E.Secs[Idx];
      if (!(IS.Flags & SHF_ALLOC) || IS.Name == ".mcs251.bit")
        continue;
      const PosRecord *R = findPosition(uint32_t(ObjId), Idx);
      if (!R)
        continue; // The verified exemption; coverage owns the verdict.
      const uint32_t Base = R->Slices.front().FinalAddress;
      bool Pinned = false;
      uint32_t PinValue = 0;
      for (const auto &P : O.AreaStarts)
        if (P.first == IS.Name) {
          Pinned = true;
          PinValue = P.second;
        }
      const MergedEntity *Owned = nullptr;
      if (StringRef(IS.Name).starts_with(FixedSectionPrefix))
        for (const MergedEntity &M : Entities)
          if (M.OwnedObj == &Obj && M.OwnedSecIdx == Idx) {
            Owned = &M;
            break;
          }
      // §4.2: an owned entity and a per-section pin must agree; a conflict is
      // refused instead of letting one silently override the other.
      if (Owned && Pinned && PinValue != Owned->A)
        return failVerify("the section " + IS.Name + " of " + Obj.Arg +
                          " is pinned at " + hex0x(PinValue) +
                          " but disagrees with the placement entity " +
                          Owned->Stable);
      for (const Symbol &S : Obj.E.Syms) {
        if (S.Name.empty() || S.Shndx != Idx || S.Type() == STT_SECTION)
          continue;
        if (S.Value > IS.Size)
          continue; // A metadata carrier, not a byte-positioned entry.
        Def D;
        D.Obj = &Obj;
        D.IS = &IS;
        D.S = &S;
        D.Base = Base;
        D.W = uint64_t(Base) + uint64_t(S.Value); // widened arithmetic
        D.Pinned = Pinned;
        D.PinValue = PinValue;
        D.Owned = Owned;
        D.OwnedMain =
            Owned && (S.Type() == STT_OBJECT || S.Type() == STT_FUNC);
        Defs.push_back(D);
      }
    }
  }

  // Phase 2: exact duplicate output definition rows for an audited name
  // refuse the name outright (§5.3/§5.4).  A duplicate sitting at a
  // definition's expected value keeps the frozen "share the expected value"
  // body; one elsewhere uses the A-DUP-OTHER body.
  {
    std::vector<std::string> Names;
    for (const Def &D : Defs)
      if (std::find(Names.begin(), Names.end(), D.S->Name) == Names.end())
        Names.push_back(D.S->Name);
    for (const std::string &N : Names) {
      std::vector<const Symbol *> Rows;
      for (const Symbol &F : Final.Syms)
        if (F.Name == N && F.Type() != STT_SECTION)
          Rows.push_back(&F);
      const Symbol *Dup = nullptr;
      for (size_t I = 0; I < Rows.size() && !Dup; ++I)
        for (size_t J = I + 1; J < Rows.size(); ++J)
          if (ExactDup(*Rows[I], *Rows[J])) {
            Dup = Rows[I];
            break;
          }
      if (!Dup)
        continue;
      const Def *At = nullptr;
      for (const Def &D : Defs)
        if (D.S->Name == N && D.W == uint64_t(Dup->Value))
          At = &D;
      if (At) {
        unsigned AtValue = 0;
        for (const Symbol *F : Rows)
          if (uint64_t(F->Value) == uint64_t(Dup->Value))
            ++AtValue;
        return failVerify("the section " + At->IS->Name + " of " +
                          At->Obj->Arg + " is placed at " + hex0x(At->Base) +
                          " but " + Twine(AtValue) + " final ELF symbols " + N +
                          " share the expected value " + hex0x(At->W) +
                          ", so the source of the input definition is not "
                          "established");
      }
      return failVerify("the final symbol table contains duplicate definitions "
                        "of " + N + " at " + hex0x(Dup->Value) +
                        ", so the source of the input definition is not "
                        "established");
    }
  }

  // Phase 3: per-definition H -> W -> K, then the shared-row (A-REUSE) guard.
  std::map<const Symbol *, const Def *> SelectedBy;
  for (const Def &D : Defs) {
    const Symbol &I = *D.S;
    std::vector<const Symbol *> N;
    for (const Symbol &F : Final.Syms)
      if (F.Name == I.Name && F.Type() != STT_SECTION)
        N.push_back(&F);
    if (N.empty())
      return failVerify("the input symbol " + I.Name + " of " + D.Obj->Arg +
                        " (section " + D.IS->Name +
                        ") is absent from the final symbol table, so "
                        "the section's position is uncorroborated");
    auto Ambiguous = [&](unsigned Count) {
      // §7 A-AMBIG: the local body keeps the frozen "file-scoped" wording; the
      // GLOBAL variant drops it (a GLOBAL is never a file-scoped symbol).
      const std::string Head =
          (I.Bind() == STB_LOCAL ? "the file-scoped symbol " : "the symbol ");
      return failVerify("the section " + D.IS->Name + " of " + D.Obj->Arg +
                        " is placed at " + hex0x(D.Base) + " but " + Head +
                        I.Name + " is not uniquely identifiable in the final "
                        "symbol table (" + Twine(Count) +
                        " output symbols share the name), so its source is "
                        "not established");
    };
    std::vector<const Symbol *> H;
    for (const Symbol *F : N)
      if (ShapeCompatible(I, *F))
        H.push_back(F);
    if (H.empty())
      return failVerify("the input symbol " + I.Name + " of " + D.Obj->Arg +
                        " (section " + D.IS->Name +
                        ") has no compatible final ELF definition (type, "
                        "binding or specified size disagrees)");
    std::vector<const Symbol *> T;
    for (const Symbol *F : H)
      if (uint64_t(F->Value) == D.W)
        T.push_back(F);
    if (T.empty()) {
      bool SameValue = false;
      for (const Symbol *F : N)
        if (uint64_t(F->Value) == D.W)
          SameValue = true;
      if (SameValue)
        return Ambiguous(unsigned(N.size())); // W holds only incompatible shapes
      return failVerify("the section " + D.IS->Name + " of " + D.Obj->Arg +
                        " is placed at " + hex0x(D.Base) +
                        " but no final ELF symbol " + I.Name + " sits at " +
                        hex0x(D.W));
    }
    if (T.size() > 1) {
      bool Dup = false;
      for (size_t A = 0; A < T.size() && !Dup; ++A)
        for (size_t B = A + 1; B < T.size(); ++B)
          if (ExactDup(*T[A], *T[B])) {
            Dup = true;
            break;
          }
      if (Dup)
        return failVerify("the section " + D.IS->Name + " of " + D.Obj->Arg +
                          " is placed at " + hex0x(D.Base) + " but " +
                          Twine(T.size()) + " final ELF symbols " + I.Name +
                          " share the expected value " + hex0x(D.W) +
                          ", so the source of the input definition is not "
                          "established");
      return Ambiguous(unsigned(T.size()));
    }
    // T is exactly one row, but that alone does not prove the source.
    const Symbol *Selected = nullptr;
    if (H.size() == 1) {
      Selected = H.front();
    } else {
      if (!D.Pinned && !D.Owned)
        return Ambiguous(unsigned(H.size()));
      const uint64_t K = D.Pinned
                             ? uint64_t(D.PinValue) + uint64_t(I.Value)
                             : uint64_t(D.Owned->A) + uint64_t(I.Value);
      std::vector<const Symbol *> U;
      for (const Symbol *F : H)
        if (uint64_t(F->Value) == K)
          U.push_back(F);
      if (U.size() != 1)
        return Ambiguous(unsigned(H.size()));
      Selected = U.front();
    }
    if (uint64_t(Selected->Value) != D.W || !ShapeCompatible(I, *Selected))
      return Ambiguous(unsigned(H.size()));
    // An owned main symbol must also be the ONE row V10 already selected.
    if (D.OwnedMain && D.Owned->OutSym && Selected != D.Owned->OutSym)
      return Ambiguous(unsigned(N.size()));
    if (SelectedBy.find(Selected) != SelectedBy.end())
      return failVerify("the final ELF symbol " + I.Name + " at " +
                        hex0x(Selected->Value) +
                        " is selected for more than one input definition, so "
                        "their sources are not separately established");
    SelectedBy[Selected] = &D;
  }
  return true;
}

// §7.5: every non-empty PROGBITS ALLOC input range has unique PT_LOAD file
// coverage; p_filesz <= p_memsz; the carrier never enters a load range.
bool Verifier::checkPositionsLoad() {
  // Rule B (matrix §8-§11).  The final ELF's PT_LOAD expresses the CODE load
  // image, NOT a unified numeric ledger of the three storage spaces.  The
  // memory bound is therefore ALWAYS CODE's 0x01000000 -- never the smallest
  // upper bound of a record that merely intersects numerically.  NOBITS and
  // zero-length records take no part in the load association; a non-CODE
  // PROGBITS record is refused (B-CLASS).
  //
  // Foff = [p_offset, p_offset+p_filesz)   (ELF file offsets)
  // Fva  = [p_vaddr,  p_vaddr+p_filesz)    (file-backed virtual range)
  // Mva  = [p_vaddr,  p_vaddr+p_memsz)     (memory span)
  // All ends are computed widened (u64) and never truncated.
  for (const LoadSeg &S : Final.Loads) {
    if (S.FileSz > S.MemSz)
      return failVerify("malformed input ELF " + O.Elf +
                        ": a PT_LOAD has p_filesz larger than p_memsz");
    const uint64_t MemEnd = uint64_t(S.VAddr) + uint64_t(S.MemSz);
    if (MemEnd > 0x1000000ull) // B-END: the CODE image upper bound, widened.
      return failVerify("malformed input ELF " + O.Elf +
                        ": a PT_LOAD memory range [" + hex0x(S.VAddr) + "," +
                        hex0x(MemEnd) + ") (vaddr + memsz, widened) leaves "
                        "its address space");
  }
  // B-DUP: only two NON-EMPTY file-backed virtual ranges can overlap; a
  // filesz=0 segment is an EMPTY interval and never a duplicate coverage
  // (§9.1).  This must be explicit, or a zero-length point inside another
  // range would be misread as an overlap.
  for (size_t I = 0; I != Final.Loads.size(); ++I)
    for (size_t J = I + 1; J != Final.Loads.size(); ++J) {
      const LoadSeg &A = Final.Loads[I], &B = Final.Loads[J];
      if (A.FileSz == 0 || B.FileSz == 0)
        continue;
      if (uint64_t(A.VAddr) < uint64_t(B.VAddr) + B.FileSz &&
          uint64_t(B.VAddr) < uint64_t(A.VAddr) + A.FileSz)
        return failVerify("malformed input ELF " + O.Elf +
                          ": two PT_LOAD segments cover the same addresses");
    }
  // B-CARRIER: the carrier's file range must not intersect any NON-EMPTY file
  // offset range; filesz=0 is an empty interval.
  const Section *Carrier = nullptr;
  for (const Section &S : Final.Secs)
    if (S.Name == PositionsNoteSectionName)
      Carrier = &S;
  if (Carrier && Carrier->Size > 0)
    for (const LoadSeg &S : Final.Loads) {
      if (S.FileSz == 0)
        continue;
      if (uint64_t(Carrier->Offset) < uint64_t(S.FileOff) + S.FileSz &&
          uint64_t(S.FileOff) < uint64_t(Carrier->Offset) + Carrier->Size)
        return failVerify("malformed input ELF " + O.Elf +
                          ": the positioning carrier overlaps a PT_LOAD "
                          "file range");
    }
  // Per-record association, in the carrier's own (object, shndx) key order.
  for (const PosRecord &R : Pos.Records) {
    if (R.ObjectId >= Objs.size() ||
        R.InputShndx >= Objs[R.ObjectId].E.Secs.size())
      continue;
    const Section &IS = Objs[R.ObjectId].E.Secs[R.InputShndx];
    if (IS.RawType == SHT_NOBITS || R.InputSize == 0)
      continue; // NOBITS / a cursor reserves no file bytes (design §8.2/§9.1).
    // §8.2/§8.3: a non-empty PROGBITS record must be CODE.  A non-CODE
    // PROGBITS section cannot borrow the CODE file bytes (B-CLASS).
    if (R.Space != PSC_CODE)
      return failVerify("the section " + IS.Name + " of " +
                        Objs[R.ObjectId].Arg +
                        " has PROGBITS contents in storage space " +
                        Twine(unsigned(R.Space)) +
                        ", but the final ELF load image represents CODE");
    const uint32_t Addr = R.Slices.front().FinalAddress;
    const uint64_t End = uint64_t(Addr) + R.InputSize;
    for (uint64_t A = Addr; A != End; ++A) {
      bool Covered = false;
      for (const LoadSeg &S : Final.Loads)
        if (A >= S.VAddr && A < uint64_t(S.VAddr) + S.FileSz) {
          Covered = true;
          break;
        }
      // A hole is reported at the first missing address; a multi-coverage
      // was already refused by the B-DUP pair check above.
      if (!Covered)
        return failVerify("the section " + IS.Name + " of " +
                          Objs[R.ObjectId].Arg + " is placed at " +
                          hex0x(Addr) +
                          " but its PROGBITS range has no PT_LOAD file "
                          "coverage at " + hex0x(uint32_t(A)));
    }
  }
  // B-MAPPING: a non-empty ALLOC PROGBITS final section that shares file
  // offsets with a non-empty PT_LOAD must agree on the address each file
  // offset implies.  Only already-parsed section/program-header fields are
  // used; no new schema, p_paddr, flags or output-index protocol.
  for (const Section &Sec : Final.Secs) {
    if (Sec.Size == 0 || !(Sec.Flags & SHF_ALLOC) ||
        Sec.RawType != SHT_PROGBITS)
      continue;
    const uint64_t SLo = Sec.Offset, SHi = SLo + Sec.Size;
    for (const LoadSeg &S : Final.Loads) {
      if (S.FileSz == 0)
        continue;
      const uint64_t PLo = S.FileOff, PHi = PLo + S.FileSz;
      const uint64_t Lo = std::max(SLo, PLo), Hi = std::min(SHi, PHi);
      if (Lo >= Hi)
        continue;
      const uint64_t SecAddr = uint64_t(Sec.Addr) + (Lo - SLo);
      const uint64_t SegAddr = uint64_t(S.VAddr) + (Lo - PLo);
      if (SecAddr != SegAddr)
        return failVerify("malformed input ELF " + O.Elf +
                          ": the file-backed section " + Sec.Name +
                          " and a PT_LOAD disagree on the address of file "
                          "offset " + hex0x(Lo));
    }
  }
  return true;
}

// G11-D2 (design §7.6): V14's dynamic positions come from the positioning
// records, never from the map.  The owned fixed entities stay the other side
// of the ledger; an owned fixed section is counted ONCE (its own entity),
// never again as a dynamic row.  Overlay members keep their records and are
// judged by the group semantics the producer already applied.
bool Verifier::checkDynamicOverlaps() {
  for (const PosRecord &R : Pos.Records) {
    if (R.ObjectId >= Objs.size())
      continue; // parsePositions already rejected this shape.
    const InputObj &Obj = Objs[R.ObjectId];
    if (R.InputShndx >= Obj.E.Secs.size())
      continue;
    const Section &IS = Obj.E.Secs[R.InputShndx];
    if (StringRef(IS.Name).starts_with(FixedSectionPrefix))
      continue; // The owned entity itself, already in the ledger.
    if (R.InputSize == 0)
      continue; // A cursor reserves nothing.
    const uint32_t Addr = R.Slices.front().FinalAddress;
    const uint64_t Lo = Addr, Hi = uint64_t(Addr) + R.InputSize;
    for (const MergedEntity &E : Entities) {
      if (E.OwnedObj == nullptr || E.BoundOnly || E.Size == 0 ||
          E.Class != R.Space)
        continue;
      if (uint64_t(E.A) < Hi && Lo < uint64_t(E.A) + E.Size)
        return failVerify(E.Stable + " overlaps " + IS.Name);
    }
  }
  return true;
}

bool Verifier::checkOverlaps() {
  for (size_t I = 0; I != Entities.size(); ++I) {
    const MergedEntity &A = Entities[I];
    if (A.BoundOnly || A.Size == 0)
      continue;
    for (size_t J = I + 1; J != Entities.size(); ++J) {
      const MergedEntity &B = Entities[J];
      if (B.BoundOnly || B.Size == 0)
        continue;
      if (A.Class != B.Class)
        continue; // Separate address spaces never interfere numerically.
      if (uint64_t(A.A) < uint64_t(B.A) + B.Size &&
          uint64_t(B.A) < uint64_t(A.A) + A.Size)
        return failVerify(A.Stable + " overlaps " + B.Stable);
    }
  }
  // G11-D review B3: fixed x DYNAMIC storage.  The owned-vs-owned loop above
  // only covers explicitly placed entities; ordinary dynamically allocated
  // sections (XSEG/DSEG/CSEG/...) must be enumerated from the link map and
  // compared in the SAME storage space.
  if (!checkDynamicOverlaps())
    return false;
  // Fixed CODE spans against the initialization tables sharing the CODE
  // address space (located from the final ELF's boundary symbols, cross
  // checked against the map -- never by byte search).
  for (const char *Area : {"XINIT", "XDATA_INIT"}) {
    InitTableLoc T;
    if (!locateInitTable(Area, T))
      return false;
    if (!T.Present)
      continue;
    for (const MergedEntity &E : Entities) {
      if (E.BoundOnly || E.Size == 0 || E.Class != PSC_CODE)
        continue;
      if (uint64_t(E.A) < T.EndExclusive && uint64_t(T.Start) < uint64_t(E.A) + E.Size)
        return failVerify(E.Stable + " overlaps " + Area +
                          " initialization table");
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// V15: retain.  The owned NOTE bit0 and the input section's GNU_RETAIN flag
// must agree in both directions; no non-fixed section may carry the bit; and
// a retained entity must exist in the report and the output symtab.
// ---------------------------------------------------------------------------
bool Verifier::checkRetain() {
  for (const InputObj &Obj : Objs)
    for (const Section &S : Obj.E.Secs)
      if ((S.Flags & SHF_GNU_RETAIN) &&
          !StringRef(S.Name).starts_with(FixedSectionPrefix))
        return failVerify("retain flag mismatch for " + S.Name + " in " +
                          Obj.Arg +
                          ": SHF_GNU_RETAIN outside .mcu.fixed.*");
  for (const InputObj &Obj : Objs)
    for (const NoteRecord &R : Obj.Notes) {
      if (R.Ownership != PO_OWNED)
        continue;
      const Section *Sec =
          Obj.E.findByName((FixedSectionPrefix + R.Stable).str());
      if (!Sec)
        continue; // V11 owns this shape failure.
      const bool NoteBit = (R.Flags & FlagRetain) != 0;
      const bool SecBit = (Sec->Flags & SHF_GNU_RETAIN) != 0;
      if (NoteBit != SecBit)
        return failVerify("retain flag mismatch for " + R.Stable + " in " +
                          Obj.Arg);
    }
  for (const MergedEntity &E : Entities) {
    if (!(E.Flags & FlagRetain))
      continue;
    bool InReport = false;
    for (const ReportRow &R : Report)
      if (R.Stable == E.Stable) {
        InReport = true;
        if (!R.RetainedMarker)
          return failVerify("retain flag mismatch for " + E.Stable);
      }
    if (!InReport || outputSymbolsNamed(E.Sym).empty())
      return failVerify("retained entity " + E.Stable +
                        " absent from placement report/symtab");
  }
  return true;
}

// ---------------------------------------------------------------------------
// V16: noinit initialization coverage.  Tables are located through the map's
// boundary symbols and decoded from the FINAL image; never by byte search.
// ---------------------------------------------------------------------------
bool Verifier::checkNoInitCoverage() {
  struct Table {
    const char *Name;
    uint32_t Lo = 0, Hi = 0;
    bool Present = false;
  };
  Table XInit{"XINIT"}, XDataInit{"XDATA_INIT"};
  for (Table *T : {&XInit, &XDataInit}) {
    InitTableLoc L;
    if (!locateInitTable(T->Name, L))
      return false;
    T->Lo = L.Start;
    T->Hi = L.EndExclusive;
    T->Present = L.Present;
  }

  std::vector<std::pair<uint64_t, uint64_t>> XInitDests, XDataDests;
  for (Table *T : {&XInit, &XDataInit}) {
    if (!T->Present)
      continue;
    const bool IsXData = std::strcmp(T->Name, "XDATA_INIT") == 0;
    const size_t Hdr = IsXData ? 7 : 6;
    std::vector<uint8_t> Bytes;
    if (!imageRange(T->Lo, T->Hi - T->Lo, Bytes))
      return failVerify(std::string("malformed ") + T->Name +
                        " initialization record at " + hex0x(T->Lo) +
                        ": the table is not fully present in the load image");
    size_t Off = 0;
    while (Off != Bytes.size()) {
      if (Bytes.size() - Off < Hdr)
        return failVerify(std::string("malformed ") + T->Name +
                          " initialization record at " +
                          hex0x(T->Lo + uint32_t(Off)) + ": truncated header");
      uint32_t Dest = 0, ObjectSize = 0, PayloadSize = 0;
      if (IsXData) {
        Dest = (uint32_t(Bytes[Off]) << 16) | (uint32_t(Bytes[Off + 1]) << 8) |
               Bytes[Off + 2];
        ObjectSize = uint32_t(Bytes[Off + 3]) << 8 | Bytes[Off + 4];
        PayloadSize = uint32_t(Bytes[Off + 5]) << 8 | Bytes[Off + 6];
      } else {
        Dest = uint32_t(Bytes[Off]) << 8 | Bytes[Off + 1];
        ObjectSize = uint32_t(Bytes[Off + 2]) << 8 | Bytes[Off + 3];
        PayloadSize = uint32_t(Bytes[Off + 4]) << 8 | Bytes[Off + 5];
      }
      if (!ObjectSize || (PayloadSize != 0 && PayloadSize != ObjectSize) ||
          PayloadSize > Bytes.size() - Off - Hdr)
        return failVerify(std::string("malformed ") + T->Name +
                          " initialization record at " +
                          hex0x(T->Lo + uint32_t(Off)) +
                          ": bad object/payload length");
      // G11-D review B4: the destination's END boundary is part of the record
      // check -- a record whose destination range leaves its address space is
      // malformed evidence, not a record to skip.
      const uint64_t SpaceEnd = IsXData ? 0x1000000ull : 0x10000ull;
      if (uint64_t(Dest) + ObjectSize > SpaceEnd)
        return failVerify(std::string("malformed ") + T->Name +
                          " initialization record at " +
                          hex0x(T->Lo + uint32_t(Off)) +
                          ": the destination range ends outside the " +
                          (IsXData ? "24-bit" : "16-bit") +
                          " address space");
      (IsXData ? XDataDests : XInitDests)
          .push_back({Dest, uint64_t(Dest) + ObjectSize});
      Off += Hdr + PayloadSize;
    }
  }
  // G11-D review B4: match each noinit entity in ITS OWN storage space --
  // XINIT records carry 16-bit AS0-DATA destinations, XDATA_INIT records
  // carry 24-bit XDATA destinations.  Comparing every entity against both
  // tables would let an out-of-space numeric coincidence decide the verdict.
  for (const MergedEntity &E : Entities) {
    if (!(E.Flags & FlagNoInit) || E.Size == 0 || !E.OwnedObj)
      continue;
    const uint64_t Lo = E.A, Hi = uint64_t(E.A) + E.Size;
    const std::vector<std::pair<uint64_t, uint64_t>> &Dests =
        E.Class == PSC_XDATA ? XDataDests : XInitDests;
    const char *TableName = E.Class == PSC_XDATA ? "XDATA_INIT" : "XINIT";
    if (E.Class != PSC_XDATA && E.Class != PSC_AS0_DATA)
      continue; // A CODE-class noinit entity has no data-initialization path.
    for (const auto &D : Dests)
      if (Lo < D.second && D.first < Hi)
        return failVerify(std::string("noinit entity ") + E.Stable +
                          " is covered by " + TableName + " initialization");
  }
  return true;
}

// ---------------------------------------------------------------------------
// V17: bind-only relocation encoding, driven from the ORIGINAL objects.
// ---------------------------------------------------------------------------
bool Verifier::checkBindRelocations() {
  std::map<std::string, const MergedEntity *> BindByName;
  for (const MergedEntity &E : Entities)
    if (E.BoundOnly)
      BindByName[E.Sym] = &E;
  if (BindByName.empty())
    return true;

  for (size_t ObjOrd = 0; ObjOrd != Objs.size(); ++ObjOrd) {
    const InputObj &Obj = Objs[ObjOrd];
    for (unsigned SI = 0; SI != Obj.E.Secs.size(); ++SI) {
      const Section &Rel = Obj.E.Secs[SI];
      if (Rel.Type != SHT_RELA)
        continue;
      if (Rel.Info == 0 || Rel.Info >= Obj.E.Secs.size()) {
        if (Rel.Size == 0)
          continue;
        return failVerify("relocation sites cannot be located in " + Obj.Arg);
      }
      const Section &Target = Obj.E.Secs[Rel.Info];
      if (Rel.EntSize != 12 || (Rel.Size % 12) != 0 ||
          uint64_t(Rel.Offset) + Rel.Size > Obj.E.Buf.size())
        return failVerify("relocation table is malformed in " + Obj.Arg);
      if (Target.Type == SHT_NOBITS)
        continue;
      // G11-D2 (design §7.2): the site's position comes from the positioning
      // record keyed by the relocation section's own sh_info -- the ORIGINAL
      // index, never the section name.
      uint32_t SiteBase = 0;
      const bool HaveSite = resolveInputRange(uint32_t(ObjOrd), Rel.Info, 0, 0,
                                              SiteBase);
      for (unsigned RI = 0; RI != Rel.Size / 12; ++RI) {
        size_t Off2 = Rel.Offset + size_t(RI) * 12;
        uint32_t ROff = 0, RInfo = 0, AddU = 0;
        rd32(Obj.E.Buf, Off2, ROff);
        rd32(Obj.E.Buf, Off2 + 4, RInfo);
        rd32(Obj.E.Buf, Off2 + 8, AddU);
        const int32_t Addend = int32_t(AddU);
        const uint32_t Type = RInfo & 0xff, SymIdx = RInfo >> 8;
        if (SymIdx >= Obj.E.Syms.size())
          return failVerify("relocation symbol index is out of range in " +
                            Obj.Arg);
        auto It = BindByName.find(Obj.E.Syms[SymIdx].Name);
        if (It == BindByName.end())
          continue;
        const MergedEntity &E = *It->second;
        if (!HaveSite)
          return failVerify("relocation site for " + E.Stable +
                            " cannot be located in " + Obj.Arg + " (" +
                            Target.Name + ")");
        // G11-D review B5: the site must be inside the INPUT SECTION's own
        // span.  A relocation offset beyond the section (or one whose field
        // crosses its end) is malformed evidence, never a skippable row.
        const uint32_t W0 = relocWidth(Type);
        if (uint64_t(ROff) + W0 > Target.Size)
          return failVerify("relocation to " + E.Stable + " at " +
                            hex0x(SiteBase + ROff) +
                            " lies outside the relocated input section " +
                            Target.Name + " in " + Obj.Arg);
        uint32_t Site = 0;
        if (!resolveInputRange(uint32_t(ObjOrd), Rel.Info, ROff,
                               std::max(W0, 1u), Site) &&
            !resolveInputRange(uint32_t(ObjOrd), Rel.Info, ROff, 0, Site))
          return failVerify("relocation to " + E.Stable +
                            " cannot be resolved to a final address in " +
                            Obj.Arg + " (" + Target.Name + ")");
        const int64_t Value = int64_t(E.A) + Addend;
        // G11-D review B5: an UNKNOWN relocation type must never be skipped.
        // A type this verifier cannot decode is a capability requirement it
        // cannot satisfy for a relocated bind reference -- fail closed.
        if (Type > R_J11)
          return failVerify("unknown relocation type " + Twine(Type) +
                            " for " + E.Stable + " at " + hex0x(Site) +
                            " in " + Obj.Arg);
        if (Value < 0 || Value > 0xffffff)
          return failVerify("relocation to " + E.Stable + " at " +
                            hex0x(Site) + " does not encode the expected "
                            "placement address");
        const uint32_t U = uint32_t(Value);
        const uint32_t W = W0;
        if (W == 0)
          continue;
        std::vector<uint8_t> Got;
        if (!imageRange(Site, W, Got))
          return failVerify("relocation to " + E.Stable + " at " +
                            hex0x(Site) + " does not encode the expected "
                            "placement address (bytes absent)");
        // G11-D review B5: the XDATA 16-bit rejection covers BOTH 16-bit
        // channels (the producer gates R_16 and R_J16 identically).
        if ((Type == R_16 || Type == R_J16) && E.Class == PSC_XDATA)
          return failVerify("XDATA symbol " + E.Stable +
                            " uses a 16-bit relocation channel");
        bool Ok = true;
        switch (Type) {
        case R_16:
        case R_J16:
          Ok = Got[0] == uint8_t(U >> 8) && Got[1] == uint8_t(U);
          break;
        case R_24:
          Ok = Got[0] == uint8_t(U >> 16) && Got[1] == uint8_t(U >> 8) &&
               Got[2] == uint8_t(U);
          break;
        case R_LO8:
          Ok = Got[0] == uint8_t(U);
          break;
        case R_MID8:
          Ok = Got[0] == uint8_t(U >> 8);
          break;
        case R_HI8:
          Ok = Got[0] == uint8_t(U >> 16);
          break;
        case R_J11: {
          uint8_t OrigByte = 0;
          std::vector<uint8_t> TData;
          if (Obj.E.sectionData(Target, TData) && ROff < TData.size())
            OrigByte = TData[ROff];
          // G11-D review B5: the full J11 condition -- the opcode must really
          // be ACALL/AJMP (low five bits 0x01/0x11), the page must match, and
          // the low 11 bits must be encoded.
          const uint8_t Op = OrigByte & 0x1f;
          if (Op != 0x01 && Op != 0x11)
            return failVerify("relocation to " + E.Stable + " at " +
                              hex0x(Site) + " is not an ACALL/AJMP field in " +
                              Obj.Arg);
          if ((((Site + 2) & 0xffffff) & 0xfffff800) != (U & 0xfffff800))
            return failVerify("relocation to " + E.Stable + " at " +
                              hex0x(Site) + " overflows the J11 page in " +
                              Obj.Arg);
          Ok = Got[0] == uint8_t((OrigByte & 0x1f) | ((U >> 3) & 0xe0)) &&
               Got[1] == uint8_t(U);
          break;
        }
        case R_PC8: {
          // G11-D review B5: the relative channel is independently checked,
          // not skipped: the displacement is computed from the FINAL site and
          // must be an in-range byte.
          const int64_t Rel = int64_t(E.A) + Addend - int64_t(Site) - 1;
          if (Rel < -128 || Rel > 127)
            return failVerify("relocation to " + E.Stable + " at " +
                              hex0x(Site) + " overflows the PC8 field");
          Ok = Got[0] == uint8_t(uint8_t(int8_t(Rel)));
          break;
        }
        case R_NONE:
          continue;
        default:
          // Unreachable: every type above is either handled or already
          // rejected as unknown.  Kept fail-closed.
          return failVerify("unknown relocation type " + Twine(Type) +
                            " for " + E.Stable + " at " + hex0x(Site));
        }
        if (!Ok)
          return failVerify("relocation to " + E.Stable + " at " +
                            hex0x(Site) + " does not encode the expected "
                            "placement address");
        // G11-D review B5: the J16 bank condition (independent of the
        // producer's encode) plus the CODE-target requirement for the control
        // channels.
        if (Type == R_J16 || Type == R_J11) {
          if (E.Class != PSC_CODE)
            return failVerify("relocation to " + E.Stable + " at " +
                              hex0x(Site) +
                              " uses a control channel to a non-CODE target");
          if (Type == R_J16 && (((Site + 2) & 0xffffff) & 0xff0000) !=
                                  (U & 0xff0000))
            return failVerify("relocation to " + E.Stable + " at " +
                              hex0x(Site) + " overflows the J16 bank");
        }
        // A stored XDATA pointer must resolve inside the target object;
        // one-past-end is the last legal value.  The reference's addend is the
        // only part the object records, so it must lie in [0, size].
        if (E.Class == PSC_XDATA && Type == R_24 && E.Size != 0 &&
            (Addend < 0 || uint64_t(Addend) > E.Size))
          return failVerify("stored XDATA pointer resolves outside target "
                            "object " + E.Stable);
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// V18: owned CODE image evidence.  Every span byte must exist in the final
// load image; non-relocated bytes are compared directly; relocated bytes are
// verified through their own independent relocation encoding rule.
// ---------------------------------------------------------------------------
bool Verifier::checkCodeImage() {
  for (const MergedEntity &E : Entities) {
    if (!E.OwnedObj || E.Class != PSC_CODE)
      continue;
    const InputObj &Obj = *E.OwnedObj;
    const uint32_t ObjOrd = uint32_t(&Obj - &Objs[0]);
    // G11-D2 (design §7.2): the owned section is located through its
    // ORIGINAL index, never through a name that could repeat.
    if (E.OwnedSecIdx == 0 || E.OwnedSecIdx >= Obj.E.Secs.size() ||
        Obj.E.Secs[E.OwnedSecIdx].Name != E.OwnedSection)
      return failVerify("section " + E.OwnedSection +
                        " disagrees with placement NOTE for " + E.Stable);
    const Section &Sec = Obj.E.Secs[E.OwnedSecIdx];
    if (Sec.Type == SHT_NOBITS)
      continue; // Spec-defined non-applicability: no CODE bytes exist.
    const PosRecord *PR = findPosition(ObjOrd, E.OwnedSecIdx);
    if (!PR)
      return failVerify("missing CODE image bytes for " + E.Stable +
                        ": the fixed section has no positioning record");
    const uint32_t Base = PR->Slices.front().FinalAddress;
    if (PR->InputSize != Sec.Size || Base != E.A)
      return failVerify("CODE image mismatch for " + E.Stable + " at " +
                        hex0x(Base) + ": the positioning carrier disagrees "
                        "with the NOTE");
    std::vector<uint8_t> Img;
    if (!imageRange(E.A, E.Size, Img))
      return failVerify("missing CODE image bytes for " + E.Stable);
    std::vector<uint8_t> Orig;
    if (!Obj.E.sectionData(Sec, Orig))
      return failVerify("CODE image mismatch for " + E.Stable + " at " +
                        hex0x(E.A) + ": the input section body is out of "
                        "bounds");

    // Collect the section's relocations (offset -> type/addend/target).
    struct R { uint32_t Type; int32_t Addend; std::string Target; };
    std::map<uint32_t, R> Relocs;
    std::set<uint32_t> Covered;
    for (const Section &Maybe : Obj.E.Secs) {
      if (Maybe.Type != SHT_RELA || Maybe.Info == 0 ||
          Maybe.Info >= Obj.E.Secs.size() ||
          &Obj.E.Secs[Maybe.Info] != &Sec)
        continue;
      if (Maybe.EntSize != 12 || (Maybe.Size % 12) != 0 ||
          uint64_t(Maybe.Offset) + Maybe.Size > Obj.E.Buf.size())
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A) + ": malformed relocation table");
      for (unsigned RI = 0; RI != Maybe.Size / 12; ++RI) {
        size_t O2 = Maybe.Offset + size_t(RI) * 12;
        uint32_t ROff = 0, RInfo = 0, AddU = 0;
        rd32(Obj.E.Buf, O2, ROff);
        rd32(Obj.E.Buf, O2 + 4, RInfo);
        rd32(Obj.E.Buf, O2 + 8, AddU);
        const uint32_t Type = RInfo & 0xff, SymIdx = RInfo >> 8;
        if (SymIdx >= Obj.E.Syms.size())
          return failVerify("CODE image mismatch for " + E.Stable + " at " +
                            hex0x(E.A) + ": relocation symbol out of range");
        // G11-D review B5: an unknown relocation type used to be inserted into
        // `Covered` and then skipped by the `default:` arm, which masked the
        // bytes it (nominally) rewrote -- a CODE byte could be tampered with
        // and still pass.  An undecodable type is a FAIL.
        if (Type > R_J11)
          return failVerify("CODE image mismatch for " + E.Stable + " at " +
                            hex0x(E.A + ROff) +
                            ": unknown relocation type " + Twine(Type));
        if (uint64_t(ROff) + relocWidth(Type) > Sec.Size)
          return failVerify("CODE image mismatch for " + E.Stable + " at " +
                            hex0x(E.A + ROff) +
                            ": the relocation field leaves the section");
        Relocs[ROff] = {Type, int32_t(AddU), Obj.E.Syms[SymIdx].Name};
        for (uint32_t K = 0; K != relocWidth(Type); ++K)
          Covered.insert(ROff + K);
      }
    }
    // Non-relocated bytes: direct comparison with the input section body.
    for (uint32_t I = 0; I != E.Size && I < Orig.size(); ++I) {
      if (Covered.count(I))
        continue;
      if (Img[I] != Orig[I])
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + I));
    }
    // Relocated bytes: recompute the encoding from the independently located
    // target and compare.
    for (const auto &P : Relocs) {
      const uint32_t ROff = P.first;
      const R &Rc = P.second;
      const uint32_t W = relocWidth(Rc.Type);
      if (W == 0)
        continue;
      // G11-D review B5: a relocated field that leaves the located span is a
      // FAIL, not a skip -- the bytes it claims to own cannot be excused.
      if (uint64_t(ROff) + W > Img.size())
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + ROff) +
                          ": the relocation field leaves the CODE span");
      std::optional<uint32_t> TargetAddr = outputAddress(Rc.Target);
      if (!TargetAddr) {
        // A section-relative or local target: resolve through its own input
        // association if possible, otherwise fail closed (cannot locate).
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + ROff) +
                          ": relocation target cannot be located");
      }
      const int64_t Value = int64_t(*TargetAddr) + Rc.Addend;
      if (Value < 0 || Value > 0xffffff)
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + ROff));
      const uint32_t U = uint32_t(Value);
      // G11-D review R3-3: the producer's F9 gate rejects a 16-bit channel
      // to an XDATA-class target for EVERY relocation ("XDATA symbol ghost
      // truncated to 16 bits (use the 24-bit relocation channel)"), not only
      // bind names.  V17 enforces it for the bind entities; a non-bind
      // target's bank being legal says nothing about its storage space, so
      // V18 resolves the target's space independently (relocTargetSpace)
      // and applies the same channel constraint.  Undecidable targets are
      // left alone, exactly like the producer's classifier.
      if (Rc.Type == R_16 || Rc.Type == R_J16) {
        if (relocTargetSpace(Rc.Target, Obj) == PSC_XDATA)
          return failVerify("CODE image mismatch for " + E.Stable + " at " +
                            hex0x(E.A + ROff) +
                            ": the relocation targets XDATA symbol " +
                            Rc.Target + " through a 16-bit channel");
      }
      bool Ok = true;
      switch (Rc.Type) {
      case R_16:
      case R_J16:
        Ok = Img[ROff] == uint8_t(U >> 8) && Img[ROff + 1] == uint8_t(U);
        break;
      case R_24:
        Ok = Img[ROff] == uint8_t(U >> 16) && Img[ROff + 1] == uint8_t(U >> 8) &&
             Img[ROff + 2] == uint8_t(U);
        break;
      case R_LO8:
        Ok = Img[ROff] == uint8_t(U);
        break;
      case R_MID8:
        Ok = Img[ROff] == uint8_t(U >> 8);
        break;
      case R_HI8:
        Ok = Img[ROff] == uint8_t(U >> 16);
        break;
      case R_J11: {
        // G11-D review B5: the full J11 condition, not just the low bits.
        const uint8_t Op = Orig[ROff] & 0x1f;
        if (Op != 0x01 && Op != 0x11)
          return failVerify("CODE image mismatch for " + E.Stable + " at " +
                            hex0x(E.A + ROff) +
                            ": the J11 field is not an ACALL/AJMP opcode");
        if (((E.A + ROff + 2) & 0xfffff800) != (U & 0xfffff800))
          return failVerify("CODE image mismatch for " + E.Stable + " at " +
                            hex0x(E.A + ROff) + ": J11 page overflow");
        Ok = Img[ROff] == uint8_t((Orig[ROff] & 0x1f) | ((U >> 3) & 0xe0)) &&
             Img[ROff + 1] == uint8_t(U);
        break;
      }
      case R_PC8: {
        const int64_t Rel = int64_t(*TargetAddr) + Rc.Addend -
                            (int64_t(E.A) + ROff) - 1;
        Ok = Rel >= -128 && Rel <= 127 &&
             Img[ROff] == uint8_t(uint8_t(int8_t(Rel)));
        break;
      }
      case R_NONE:
        continue;
      default:
        // Unreachable: unknown types were rejected when the table was read.
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + ROff) +
                          ": unknown relocation type " + Twine(Rc.Type));
      }
      if (!Ok)
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + ROff));
      // G11-D review R2-4 (B5/V18): a J16 field encodes only the low 16
      // bits, so a correct low-half encoding is NOT proof by itself -- the
      // TARGET must also live in the field's own 64K bank.  A cross-bank
      // target whose low bytes happen to match used to pass V18; V17 checks
      // this condition for bind targets, and V18 must apply the same rule to
      // its own (non-bind) relocations independently.
      if (Rc.Type == R_J16 &&
          ((((uint64_t(E.A) + ROff) + 2) & 0xffffff) & 0xff0000) !=
              (U & 0xff0000))
        return failVerify("CODE image mismatch for " + E.Stable + " at " +
                          hex0x(E.A + ROff) + ": the J16 target leaves the "
                          "field's bank");
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Driver.
// ---------------------------------------------------------------------------
bool Verifier::run() {
  if (O.Elf.empty() || O.Report.empty())
    return fail("mcs251-placement-verify: error: --elf and --report are "
                "required");
  if (!checkOutputEntry()) return false;             // V1
  if (!checkInputNoteStructure()) return false;      // V2
  if (!checkNameAssociation()) return false;         // V3
  if (!checkSourceHashes()) return false;            // V4
  if (!loadManifests()) return false;                // V5
  if (!mergeAndCompare()) return false;              // V6
  if (!loadAndCheckReport()) return false;           // V7
  if (!enumerateBothWays()) return false;            // V8
  if (!checkReportHashes()) return false;            // V9
  if (!loadMap()) return false;                      // locating evidence
  // G11-D2 (design §7.1): the positioning carrier phase.  Steps 3-7 of the
  // recommended flow: independent parse, ordered digest agreement,
  // bidirectional coverage with the independent classification and the
  // R4-2/R4-4 range and window rules.  The map/symbol/PT_LOAD cross-checks
  // (step 8) run after V10-V13 so the frozen entity diagnostics keep their
  // order; V14/V17/V18 consume the validated queries (step 9).
  if (!parsePositions()) return false;
  if (!checkPositionObjects()) return false;
  if (!checkPositionsCoverage()) return false;
  if (!checkRegionRanges()) return false;
  if (!checkOutputEntities()) return false;          // V10
  if (!checkSizesAndMainSymbols()) return false;     // V11
  if (!checkAlignmentAndRanges()) return false;      // V12
  if (!checkWindows()) return false;                 // V13
  if (!checkPositionsMap()) return false;            // §7.3 map cross-check
  if (!checkPositionsSymbols()) return false;        // §7.4 symbol cross-check
  if (!checkPositionsLoad()) return false;           // §7.5 PT_LOAD cross-check
  if (!checkOverlaps()) return false;                // V14
  if (!checkRetain()) return false;                  // V15
  if (!checkNoInitCoverage()) return false;          // V16
  if (!checkBindRelocations()) return false;         // V17
  if (!checkCodeImage()) return false;               // V18
  return true;
}

} // namespace

bool verifyPlacement(const Options &O, raw_ostream &Err) {
  Verifier V(O, Err);
  return V.run();
}

} // namespace lld::mcs251::placementverify
