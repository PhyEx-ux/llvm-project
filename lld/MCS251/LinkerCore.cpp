//===- LinkerCore.cpp - MCS251 ELF link semantics --------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This target semantic core has no dependency on the MCS251 flavor shell.
// It is intentionally shaped for a future lld/ELF/Arch/MCS251.cpp target handler.
//===----------------------------------------------------------------------===//

#include "LinkerCore.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support;
using namespace llvm::support::endian;

namespace lld::mcs251 {

static constexpr uint16_t EM_MCS251 = 0x9999;
static constexpr uint32_t SHF_MCS251_OVERLAY = 0x10000000;
static constexpr uint32_t ABI_FLAGS = 0x00000001;
static constexpr uint32_t EF_ABI_MASK = 0xff;

struct InputSection;
struct InputFile;

struct Relocation {
  uint32_t Offset = 0;
  uint32_t Type = 0;
  uint32_t Sym = 0;
  int32_t Addend = 0;
};

struct InputSymbol {
  std::string Name;
  uint32_t Value = 0;
  uint32_t Size = 0;
  uint8_t Bind = 0;
  uint8_t Type = 0;
  uint16_t Section = 0;
  InputFile *File = nullptr;
  InputSection *Sec = nullptr;
  bool Defined = false;
  uint32_t Address = 0;
};

struct InputSection {
  InputFile *File = nullptr;
  uint32_t Index = 0;
  std::string Name;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t Offset = 0;
  uint32_t Align = 1;
  uint32_t RelocIndex = 0;
  uint32_t Address = 0;
  bool IsAlloc = false;
  bool IsCode = false;
  bool IsNobits = false;
  bool IsOverlay = false;
  bool IsLoadable = false;
  std::vector<uint8_t> Data;
  std::vector<Relocation> Relocs;
  std::string Region;
  std::string Group;
};

struct InputFile {
  std::string Path;
  std::unique_ptr<MemoryBuffer> Buffer;
  std::unique_ptr<ObjectFile> Object;
  std::vector<std::unique_ptr<InputSection>> Sections;
  std::vector<InputSymbol> Symbols;
  bool NoteSeen = false;
};

static bool fail(raw_ostream &Err, const Twine &Msg) {
  Err << "mcs251 linker: error: " << Msg << "\n";
  return false;
}

static uint32_t read32BE(ArrayRef<uint8_t> B, size_t O) {
  return support::endian::read32be(B.data() + O);
}

static uint16_t read16BE(ArrayRef<uint8_t> B, size_t O) {
  return support::endian::read16be(B.data() + O);
}

static bool rangeFits(uint64_t Start, uint64_t Size, uint64_t Limit = 0x1000000) {
  return Start <= Limit && Size <= Limit - Start;
}

static bool classifySection(InputSection &S, raw_ostream &Err) {
  StringRef N = S.Name;
  S.IsAlloc = (S.Flags & ELF::SHF_ALLOC) != 0;
  S.IsCode = (S.Flags & ELF::SHF_EXECINSTR) != 0;
  S.IsNobits = S.Type == ELF::SHT_NOBITS;
  S.IsOverlay = (S.Flags & SHF_MCS251_OVERLAY) != 0;
  S.IsLoadable = S.IsAlloc && !S.IsNobits && S.Size != 0;

  if (!S.IsAlloc)
    return true; // validateMetaSection() checks the exact supported set.

  const uint64_t Common = ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_EXECINSTR |
                          SHF_MCS251_OVERLAY;
  if (S.Flags & ~Common)
    return fail(Err, "unsupported ALLOC section flags for " + N);
  if (N == ".text" || N.starts_with(".text.")) {
    if (S.Type != ELF::SHT_PROGBITS || S.Flags != (ELF::SHF_ALLOC | ELF::SHF_EXECINSTR))
      return fail(Err, "invalid flags or type for " + N);
    S.Region = "CSEG";
    return true;
  }
  if (N == ".rodata" || N.starts_with(".rodata.")) {
    if (S.Type != ELF::SHT_PROGBITS || S.Flags != ELF::SHF_ALLOC)
      return fail(Err, "invalid flags or type for " + N);
    S.Region = "CSEG";
    return true;
  }
  if (N == ".mcs251.xinit" || N.starts_with(".mcs251.xinit.")) {
    if (S.Type != ELF::SHT_PROGBITS || S.Flags != ELF::SHF_ALLOC)
      return fail(Err, "invalid XINIT section " + N);
    S.Region = "XINIT";
    return true;
  }
  auto CodeFragment = [&](StringRef Region) {
    if (S.Flags != (ELF::SHF_ALLOC | ELF::SHF_EXECINSTR) ||
        (S.Type != ELF::SHT_PROGBITS && S.Type != ELF::SHT_NOBITS))
      return false;
    S.Region = Region.str();
    return true;
  };
  if (N == ".mcs251.HOME" || N.starts_with(".mcs251.HOME."))
    return CodeFragment("HOME") || fail(Err, "invalid HOME section " + N);
  if (N == ".mcs251.VECS" || N.starts_with(".mcs251.VECS."))
    return CodeFragment("VECS") || fail(Err, "invalid VECS section " + N);
  if (N == ".mcs251.BOOT" || N.starts_with(".mcs251.BOOT."))
    return CodeFragment("BOOT") || fail(Err, "invalid BOOT section " + N);
  if (N.starts_with(".mcs251.OSEG.")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE | SHF_MCS251_OVERLAY))
      return fail(Err, "OSEG must be writable NOBITS overlay: " + N);
    S.Region = "OSEG";
    S.Group = "OSEG";
    return true;
  }
  if ((N == ".data" || N.starts_with(".data.")) &&
      S.Type == ELF::SHT_PROGBITS && S.Size == 0 &&
      S.Flags == (ELF::SHF_ALLOC | ELF::SHF_WRITE)) {
    // SPEC §4.1: only ignorable when it has no defined symbols and no
    // relocations.  The full precondition is verified after symbol/reloc
    // loading in loadFile().
    S.Region = "DATA_EMPTY_PENDING";
    return true;
  }
  if (N == ".mcs251.dseg" || N.starts_with(".mcs251.DSEG.") ||
      N == ".data" || N.starts_with(".data.") || N == ".bss" ||
      N.starts_with(".bss.")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE))
      return fail(Err, "DSEG sections must be writable NOBITS: " + N);
    S.Region = "DSEG";
    return true;
  }
  if (N.starts_with(".mcs251.REG_BANK_")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE | SHF_MCS251_OVERLAY))
      return fail(Err, "register bank must be writable NOBITS overlay: " + N);
    S.Region = "REG";
    S.Group = N.substr(StringRef(".mcs251.").size()).split('.').first.str();
    return true;
  }
  if (N.starts_with(".mcs251.BSEG_BYTES")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE))
      return fail(Err, "BSEG_BYTES must be writable NOBITS: " + N);
    S.Region = "BSEG_BYTES";
    return true;
  }
  if (N.starts_with(".mcs251.BIT_BANK")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE | SHF_MCS251_OVERLAY))
      return fail(Err, "BIT_BANK must be writable NOBITS overlay: " + N);
    S.Region = "BIT_BANK";
    S.Group = "BIT_BANK";
    return true;
  }
  if (N.starts_with(".mcs251.ISEG")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE))
      return fail(Err, "ISEG must be writable NOBITS: " + N);
    S.Region = "ISEG";
    return true;
  }
  if (N.starts_with(".mcs251.SSEG")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE | SHF_MCS251_OVERLAY))
      return fail(Err, "SSEG must be writable NOBITS overlay: " + N);
    S.Region = "SSEG";
    S.Group = "SSEG";
    return true;
  }
  if (N.starts_with(".mcs251.DATA.")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE))
      return fail(Err, "DATA reservation must be writable NOBITS: " + N);
    S.Region = "DATA_ABS";
    return true;
  }
  if (N.starts_with(".mcs251.XSEG")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE))
      return fail(Err, "XSEG must be writable NOBITS: " + N);
    S.Region = "XSEG";
    return true;
  }
  return fail(Err, "unsupported ALLOC section " + N);
}

static bool validateMetaSection(const InputSection &S, raw_ostream &Err) {
  if (S.IsAlloc)
    return true;
  StringRef N = S.Name;
  if (S.Type == ELF::SHT_NULL)
    return S.Index == 0 || fail(Err, "unexpected SHT_NULL section " + N);
  if (N == ".note.mcs251.abi")
    return S.Type == ELF::SHT_NOTE && S.Flags == 0 && S.Align == 4 ||
           fail(Err, "malformed .note.mcs251.abi");
  if (N == ".symtab")
    return S.Type == ELF::SHT_SYMTAB && S.Flags == 0 ||
           fail(Err, "malformed .symtab");
  if (N == ".strtab" || N == ".shstrtab")
    return S.Type == ELF::SHT_STRTAB && S.Flags == 0 ||
           fail(Err, "malformed string table " + N);
  if (S.Type == ELF::SHT_RELA)
    return (S.Flags == 0 || S.Flags == ELF::SHF_INFO_LINK) ||
           fail(Err, "unsupported RELA section flags");
  if (S.Type == ELF::SHT_REL)
    return fail(Err, "SHT_REL is unsupported; MCS251 requires SHT_RELA");
  if (N == ".comment")
    return S.Type == ELF::SHT_PROGBITS && S.Flags == 0 ||
           fail(Err, "malformed .comment");
  if (N == ".note.GNU-stack")
    return S.Type == ELF::SHT_PROGBITS && S.Flags == 0 && S.Size == 0 ||
           fail(Err, "malformed .note.GNU-stack");
  return fail(Err, "unsupported non-ALLOC metadata section " + N);
}

static bool validateNote(InputFile &F, raw_ostream &Err) {
  unsigned Count = 0;
  for (const auto &S : F.Sections) {
    if (S->Name != ".note.mcs251.abi")
      continue;
    ++Count;
    if (S->Type != ELF::SHT_NOTE || S->Flags != 0 || S->Align != 4 ||
        S->Size != 52 || S->Data.size() != 52)
      return fail(Err, F.Path + ": malformed .note.mcs251.abi");
    ArrayRef<uint8_t> B(S->Data);
    if (read32BE(B, 0) != 7 || read32BE(B, 4) != 32 || read32BE(B, 8) != 1 ||
        memcmp(B.data() + 12, "MCS251\0", 7) != 0)
      return fail(Err, F.Path + ": invalid MCS251 ABI note header");
    static const uint32_t Expected[] = {1, 1, 0, 2, 0x0000f3ff, 7, 0, 0};
    for (unsigned I = 0; I != 8; ++I)
      if (read32BE(B, 20 + I * 4) != Expected[I])
        return fail(Err, F.Path + ": invalid MCS251 ABI note descriptor");
  }
  if (Count != 1)
    return fail(Err, F.Path + ": expected exactly one .note.mcs251.abi");
  return true;
}

static bool loadFile(StringRef Path, InputFile &F, raw_ostream &Err) {
  F.Path = Path.str();
  auto MB = MemoryBuffer::getFile(Path);
  if (!MB)
    return fail(Err, Path + ": cannot read input");
  F.Buffer = std::move(*MB);
  Expected<std::unique_ptr<ObjectFile>> Obj =
      ObjectFile::createObjectFile(F.Buffer->getMemBufferRef());
  if (!Obj)
    return fail(Err, Path + ": " + toString(Obj.takeError()));
  F.Object = std::move(*Obj);
  auto *ELFObj = dyn_cast<ELF32BEObjectFile>(F.Object.get());
  if (!ELFObj)
    return fail(Err, Path + ": expected ELF32 big-endian object");
  const auto &H = ELFObj->getELFFile().getHeader();
  if (H.e_type != ELF::ET_REL || H.e_machine != EM_MCS251 ||
      H.e_version != ELF::EV_CURRENT || H.e_flags != ABI_FLAGS)
    return fail(Err, Path + ": invalid MCS251 ELF header");
  // SPEC §3.1: strict identity and ET_REL structural checks.
  if (H.e_ident[ELF::EI_OSABI] != ELF::ELFOSABI_NONE ||
      H.e_ident[ELF::EI_ABIVERSION] != 0 ||
      H.e_ident[ELF::EI_VERSION] != ELF::EV_CURRENT)
    return fail(Err, Path + ": invalid MCS251 ELF identity");
  if (H.e_entry != 0 || H.e_phoff != 0 || H.e_phnum != 0 ||
      H.e_phentsize != 0 || H.e_ehsize != sizeof(ELF::Elf32_Ehdr) ||
      H.e_shentsize != sizeof(ELF::Elf32_Shdr))
    return fail(Err, Path + ": invalid MCS251 ET_REL header fields");

  const ELFFile<ELF32BE> &ELF = ELFObj->getELFFile();
  Expected<ELF32BE::ShdrRange> RawSections = ELF.sections();
  if (!RawSections)
    return fail(Err, Path + ": " + toString(RawSections.takeError()));
  F.Sections.resize(RawSections->size());
  for (uint32_t I = 0; I != RawSections->size(); ++I) {
    const ELF32BE::Shdr &H = (*RawSections)[I];
    auto S = std::make_unique<InputSection>();
    S->File = &F;
    S->Index = I;
    Expected<StringRef> Name = ELF.getSectionName(H);
    if (!Name) {
      consumeError(Name.takeError());
      return fail(Err, Path + ": invalid section name");
    }
    S->Name = Name->str();
    S->Type = H.sh_type;
    S->Flags = H.sh_flags;
    S->Size = H.sh_size;
    S->Offset = H.sh_offset;
    S->Align = std::max<uint32_t>(1, H.sh_addralign);
    if (H.sh_addr != 0)
      return fail(Err, Path + ": ET_REL section has non-zero sh_addr: " + S->Name);
    if (!classifySection(*S, Err) || !validateMetaSection(*S, Err))
      return false;
    if (S->IsAlloc && S->Align != 1)
      return fail(Err, Path + ": ALLOC section alignment must be 1: " + S->Name);
    if (!S->IsNobits) {
      Expected<ArrayRef<uint8_t>> Contents = ELF.getSectionContents(H);
      if (!Contents) {
        consumeError(Contents.takeError());
        return fail(Err, Path + ": truncated section " + S->Name);
      }
      S->Data.assign(Contents->begin(), Contents->end());
    }
    F.Sections[I] = std::move(S);
  }
  if (!validateNote(F, Err))
    return false;

  uint32_t SymtabIndex = 0;
  for (uint32_t I = 0; I != RawSections->size(); ++I)
    if ((*RawSections)[I].sh_type == ELF::SHT_SYMTAB) {
      if (SymtabIndex)
        return fail(Err, Path + ": expected exactly one SHT_SYMTAB");
      SymtabIndex = I;
    }
  if (!SymtabIndex)
    return fail(Err, Path + ": missing SHT_SYMTAB");
  const ELF32BE::Shdr &Symtab = (*RawSections)[SymtabIndex];
  if (Symtab.sh_link >= RawSections->size() ||
      (*RawSections)[Symtab.sh_link].sh_type != ELF::SHT_STRTAB ||
      Symtab.sh_entsize != sizeof(ELF32BE::Sym))
    return fail(Err, Path + ": malformed SHT_SYMTAB");
  Expected<ELF32BE::SymRange> RawSymbols = ELF.symbols(&Symtab);
  Expected<StringRef> SymStrings = ELF.getStringTableForSymtab(Symtab);
  if (!RawSymbols) {
    consumeError(RawSymbols.takeError());
    consumeError(SymStrings.takeError());
    return fail(Err, Path + ": malformed symbol table");
  }
  if (!SymStrings) {
    consumeError(SymStrings.takeError());
    return fail(Err, Path + ": malformed symbol table");
  }
  for (const ELF32BE::Sym &Sym : *RawSymbols) {
    InputSymbol IS;
    IS.File = &F;
    Expected<StringRef> Name = Sym.getName(*SymStrings);
    if (!Name) {
      consumeError(Name.takeError());
      return fail(Err, Path + ": invalid symbol name");
    }
    IS.Name = Name->str();
    IS.Value = Sym.st_value;
    IS.Size = Sym.st_size;
    IS.Bind = Sym.getBinding();
    IS.Type = Sym.getType();
    IS.Section = Sym.st_shndx;
    if (IS.Bind != ELF::STB_LOCAL && IS.Bind != ELF::STB_GLOBAL)
      return fail(Err, Path + ": unsupported symbol binding " + IS.Name);
    if (IS.Type != ELF::STT_NOTYPE && IS.Type != ELF::STT_OBJECT &&
        IS.Type != ELF::STT_FUNC && IS.Type != ELF::STT_SECTION)
      return fail(Err, Path + ": unsupported symbol type " + IS.Name);
    if (IS.Section == ELF::SHN_COMMON)
      return fail(Err, Path + ": unsupported common symbol " + IS.Name);
    if (IS.Section != ELF::SHN_UNDEF && IS.Section != ELF::SHN_ABS &&
        IS.Section >= F.Sections.size())
      return fail(Err, Path + ": symbol section index out of range");
    IS.Defined = IS.Section != ELF::SHN_UNDEF;
    if (IS.Section != ELF::SHN_UNDEF && IS.Section != ELF::SHN_ABS) {
      IS.Sec = F.Sections[IS.Section].get();
      if (IS.Value > IS.Sec->Size || IS.Size > IS.Sec->Size - IS.Value)
        return fail(Err, Path + ": symbol exceeds section bounds: " + IS.Name);
    }
    F.Symbols.push_back(std::move(IS));
  }

  for (uint32_t I = 0; I != RawSections->size(); ++I) {
    const ELF32BE::Shdr &Rela = (*RawSections)[I];
    if (Rela.sh_type != ELF::SHT_RELA)
      continue;
    if (Rela.sh_info == 0 || Rela.sh_info >= F.Sections.size() ||
        Rela.sh_link != SymtabIndex || Rela.sh_entsize != sizeof(ELF32BE::Rela))
      return fail(Err, Path + ": malformed SHT_RELA section");
    InputSection *TS = F.Sections[Rela.sh_info].get();
    if (!TS->IsAlloc || TS->Type == ELF::SHT_NOBITS)
      return fail(Err, Path + ": RELA targets non-loadable section " + TS->Name);
    Expected<ELF32BE::RelaRange> Relocs = ELF.relas(Rela);
    if (!Relocs) {
      consumeError(Relocs.takeError());
      return fail(Err, Path + ": malformed SHT_RELA section");
    }
    // SPEC §4.1: an empty PROGBITS .data section with a non-empty RELA is
    // not supported.  An empty RELA table (zero entries) is harmless and
    // does not disqualify the section from being ignored.
    if (TS->Region == "DATA_EMPTY_PENDING" && !Relocs->empty())
      return fail(Err, Path + ": empty PROGBITS .data with relocations "
                         "is not supported: " + TS->Name);
    for (const ELF32BE::Rela &RelaEntry : *Relocs) {
      Relocation R{static_cast<uint32_t>(RelaEntry.r_offset),
                   RelaEntry.getType(false), RelaEntry.getSymbol(false),
                   static_cast<int32_t>(RelaEntry.r_addend)};
      if (R.Sym >= F.Symbols.size())
        return fail(Err, Path + ": relocation symbol index out of range");
      uint32_t Width = R.Type == ELF::R_MCS251_16 ||
                               R.Type == ELF::R_MCS251_J16 ||
                               R.Type == ELF::R_MCS251_J11 ? 2
                         : R.Type == ELF::R_MCS251_24 ? 3
                         : R.Type == ELF::R_MCS251_NONE ? 0 : 1;
      if (R.Type > ELF::R_MCS251_J11 || R.Offset > TS->Size ||
          Width > TS->Size - R.Offset)
        return fail(Err, Path + ": relocation offset/type out of range");
      TS->RelocIndex = I;
      TS->Relocs.push_back(R);
    }
  }
  // SPEC §4.1: a size=0 PROGBITS .data section is only ignorable when it has
  // no defined symbols and no relocations.  Relocations are already rejected
  // above; here we check for defined symbols.
  for (const auto &S : F.Sections)
    if (S->Region == "DATA_EMPTY_PENDING") {
      for (const InputSymbol &Sym : F.Symbols)
        if (Sym.Section == S->Index && Sym.Defined)
          return fail(Err, Path + ": empty PROGBITS .data with defined symbol "
                             "is not supported: " + S->Name);
      S->Region = "IGNORE";
    }
  return true;
}

class Linker {
public:
  Linker(LinkerConfig C, raw_ostream &E) : Config(std::move(C)), Err(E) {}
  bool run(LinkerResult &Result);

private:
  LinkerConfig Config;
  raw_ostream &Err;
  std::vector<std::unique_ptr<InputFile>> Files;
  std::map<std::string, InputSymbol *> Globals;
  std::vector<InputSection *> AllSections;
  std::vector<Range> CodeUsed;
  std::vector<Range> XDataUsed;
  std::map<uint32_t, uint8_t> Image;
  std::map<std::string, uint32_t> Synth;
  std::vector<Range> DataUsed;
  uint32_t StackH = 0;
  uint32_t SPX = 0;
  uint32_t Capacity = 0;
  uint32_t SsegReservedSize = 0;
  bool StackRequested = false;

  uint32_t areaStart(StringRef Name, uint32_t Default) const;
  bool hasAreaStart(StringRef Name) const;
  bool resolveSymbols();
  bool layout();
  bool layoutCode();
  bool layoutData();
  bool allocate(InputSection &S, uint32_t Lo, uint32_t Hi);
  bool reserve(uint32_t Start, uint32_t Size, StringRef What);
  bool applyRelocations();
  bool validateXInit();
  void buildMap(raw_ostream &Out) const;
  void printInputs(raw_ostream &Out) const;
  InputSymbol *findSymbol(InputFile &F, uint32_t Index);
  bool errorUndefined();
};

uint32_t Linker::areaStart(StringRef Name, uint32_t Default) const {
  for (const auto &P : Config.AreaStarts)
    if (P.first == Name)
      return P.second;
  return Default;
}

bool Linker::hasAreaStart(StringRef Name) const {
  return llvm::any_of(Config.AreaStarts,
                      [&](const auto &P) { return P.first == Name; });
}

InputSymbol *Linker::findSymbol(InputFile &F, uint32_t Index) {
  return Index < F.Symbols.size() ? &F.Symbols[Index] : nullptr;
}

static bool isReservedBoundarySymbol(StringRef Name) {
  // SPEC §6.1: s_<AREA>, l_<AREA>, l_IRAM are reserved synthesised boundary
  // symbols.  User definitions of these names are rejected.
  // This set must match exactly the set of synthesised boundary symbols
  // generated in layoutData().
  if (Name == "l_IRAM")
    return true;
  if (Name == "__mcs251_stack_base")
    return true;
  if (Name.starts_with("s_") || Name.starts_with("l_")) {
    StringRef Area = Name.substr(2);
    static const char *Areas[] = {
        "DSEG",       "OSEG",       "ISEG",        "SSEG",
        "HOME",       "VECS",       "BOOT",        "CSEG",
        "XINIT",      "BSEG_BYTES", "BIT_BANK",    "XSEG",
        "REG_BANK_0", "REG_BANK_1", "REG_BANK_2",  "REG_BANK_3"};
    for (const char *A : Areas)
      if (Area == A)
        return true;
  }
  return false;
}

bool Linker::resolveSymbols() {
  for (auto &F : Files)
    for (auto &S : F->Symbols) {
      if (S.Name.empty() || !S.Defined)
        continue;
        if (S.Bind == ELF::STB_WEAK || S.Bind == ELF::STB_GNU_UNIQUE ||
          S.Type == ELF::STT_COMMON)
        return fail(Err, "unsupported weak/common symbol " + S.Name);
      if (isReservedBoundarySymbol(S.Name))
        return fail(Err, "user definition of reserved symbol " + S.Name);
      if (S.Sec && S.Sec->IsAlloc)
        S.Address = S.Sec->Address + S.Value;
      else
        S.Address = S.Value;
      if (S.Bind != ELF::STB_LOCAL) {
        auto It = Globals.find(S.Name);
        if (It != Globals.end() && It->second->Defined)
          return fail(Err, "duplicate definition of " + S.Name);
        Globals[S.Name] = &S;
      }
    }
  for (auto &F : Files)
    for (auto &S : F->Symbols)
      if (S.Bind != ELF::STB_LOCAL && !S.Defined) {
        auto It = Globals.find(S.Name);
        if (It != Globals.end())
          S.Address = It->second->Address;
      }
  return true;
}

bool Linker::reserve(uint32_t Start, uint32_t Size, StringRef What) {
  if (!Size)
    return true;
  if (!rangeFits(Start, Size, 0x10000))
    return fail(Err, What + ": address range is out of bounds");
  Range R{Start, Start + Size};
  for (const Range &U : DataUsed)
    if (R.Start < U.End && U.Start < R.End)
      return fail(Err, "internal DATA overlap for " + What);
  DataUsed.push_back(R);
  StackH = std::max(StackH, R.End);
  return true;
}

bool Linker::allocate(InputSection &S, uint32_t Lo, uint32_t Hi) {
  if (!S.Size)
    return true;
  for (uint32_t A = Lo; A <= Hi && S.Size <= Hi - A + 1; ++A) {
    if (S.Align > 1)
      A = (A + S.Align - 1) & ~(S.Align - 1);
    if (A > Hi || S.Size > Hi - A + 1)
      break;
    Range R{A, A + static_cast<uint32_t>(S.Size)};
    bool Good = true;
    for (const Range &U : DataUsed)
      if (R.Start < U.End && U.Start < R.End) {
        Good = false;
        break;
      }
    if (Good) {
      S.Address = A;
      return reserve(A, S.Size, S.Name);
    }
  }
  return fail(Err, "cannot allocate " + S.Name);
}

bool Linker::layoutCode() {
  CodeUsed.clear();
  auto reserveCode = [&](uint32_t Start, uint32_t Size, StringRef What) {
    if (!rangeFits(Start, Size))
      return fail(Err, "CODE address overflow in " + What);
    Range R{Start, Start + Size};
    for (const Range &U : CodeUsed)
      if (R.Start < U.End && U.Start < R.End)
        return fail(Err, "CODE overlap for " + What);
    if (Size)
      CodeUsed.push_back(R);
    return true;
  };
  struct Cursor { uint32_t V; };
  std::map<std::string, Cursor> C;
  C["HOME"].V = areaStart("HOME", 0);
  C["VECS"].V = areaStart("VECS", 0);
  C["BOOT"].V = areaStart("BOOT", 0);
  C["CSEG"].V = areaStart("CSEG", 0);
  C["XINIT"].V = areaStart("XINIT", 0);
  for (InputSection *S : AllSections)
    if ((S->Region == "HOME" || S->Region == "VECS" ||
         S->Region == "BOOT" || S->Region == "CSEG" ||
         S->Region == "XINIT") && !hasAreaStart(S->Region))
      return fail(Err, "missing --area-start=" + S->Region);
  for (InputSection *S : AllSections) {
    if (S->Region != "HOME" && S->Region != "VECS" &&
        S->Region != "BOOT" && S->Region != "CSEG" && S->Region != "XINIT")
      continue;
    uint32_t &V = C[S->Region].V;
    if (S->Align > 1)
      V = (V + S->Align - 1) & ~(S->Align - 1);
    if (!reserveCode(V, S->Size, S->Name))
      return false;
    S->Address = V;
    V += S->Size;
    if (S->IsLoadable)
      for (size_t I = 0; I != S->Data.size(); ++I) {
        uint32_t A = S->Address + I;
        if (Image.count(A))
          return fail(Err, "duplicate CODE byte at 0x" + Twine::utohexstr(A));
        Image[A] = S->Data[I];
      }
  }
  return true;
}

bool Linker::layoutData() {
  DataUsed.clear();
  StackH = 0x100;
  for (const Range &R : Config.ReservedData)
    if (!reserve(R.Start, R.End - R.Start, "--reserve-data"))
      return false;

  // Absolute DATA reservations participate before all first-fit classes.
  for (InputSection *S : AllSections)
    if (S->Region == "DATA_ABS") {
      auto It = std::find_if(Config.AreaStarts.begin(), Config.AreaStarts.end(),
                             [&](const auto &P) { return P.first == S->Name; });
      if (It == Config.AreaStarts.end())
        return fail(Err, "no --area-start for " + S->Name);
      S->Address = It->second;
      if (!reserve(S->Address, S->Size, S->Name))
        return false;
    }

  // Bit-addressable byte reservations precede every first-fit DATA class.
  for (InputSection *S : AllSections)
    if (S->Region == "BSEG_BYTES" && !allocate(*S, 0x20, 0x2f))
      return false;

  // Reject a syntactically accepted but nonexistent physical register bank.
  for (InputSection *S : AllSections)
    if (S->Region == "REG" && S->Group != "REG_BANK_0" &&
        S->Group != "REG_BANK_1" && S->Group != "REG_BANK_2" &&
        S->Group != "REG_BANK_3")
      return fail(Err, "register bank out of range in " + S->Name);

  // One fixed reservation per physical register bank, with group max size.
  for (uint32_t Bank = 0; Bank != 4; ++Bank) {
    std::string Group = (Twine("REG_BANK_") + Twine(Bank)).str();
    uint32_t Size = 0;
    for (InputSection *S : AllSections)
      if (S->Region == "REG" && S->Group == Group)
        Size = std::max<uint32_t>(Size, S->Size);
    if (Size > 8)
      return fail(Err, "register bank exceeds 8 bytes: " + Group);
    if (Size && !reserve(Bank * 8, Size, Group))
      return false;
    for (InputSection *S : AllSections)
      if (S->Region == "REG" && S->Group == Group)
        S->Address = Bank * 8;
  }

  auto allocateOverlayGroup = [&](StringRef Group, uint32_t Lo, uint32_t Hi) {
    uint32_t Size = 0;
    InputSection *Representative = nullptr;
    for (InputSection *S : AllSections)
      if (S->Group == Group) {
        if (S->Size >= Size) {
          Size = S->Size;
          Representative = S;
        }
      }
    if (!Representative || !Size)
      return true;
    if (!allocate(*Representative, Lo, Hi))
      return false;
    for (InputSection *S : AllSections)
      if (S->Group == Group)
        S->Address = Representative->Address;
    return true;
  };
  if (!allocateOverlayGroup("BIT_BANK", 0x20, 0x2f))
    return false;

  uint32_t DsegStart = areaStart("DSEG", 0);
  uint32_t DsegEnd = Config.IramSize > 0 && DsegStart < 0x80 &&
                             Config.IramSize <= 0x80 - DsegStart
                         ? DsegStart + Config.IramSize
                         : 0x80;
  if (DsegStart >= DsegEnd)
    return fail(Err, "invalid DSEG allocation window");
  for (InputSection *S : AllSections)
    if (S->Region == "DSEG" && !allocate(*S, DsegStart, DsegEnd - 1))
      return false;
  if (!allocateOverlayGroup("OSEG", DsegStart, DsegEnd - 1))
    return false;

  uint32_t IsegStart = areaStart("ISEG", 0);
  uint32_t IsegEnd = Config.IramSize > 0 && IsegStart < 0x100 &&
                             Config.IramSize <= 0x100 - IsegStart
                         ? IsegStart + Config.IramSize
                         : 0x100;
  if (IsegStart >= IsegEnd)
    return fail(Err, "invalid ISEG allocation window");
  for (InputSection *S : AllSections)
    if (S->Region == "ISEG") {
      if (S->Size) {
        if (!allocate(*S, IsegStart, IsegEnd - 1))
          return false;
      } else {
        // Legacy chain (mcs251_ld.py lnkarea2): an empty ISEG slice is not
        // first-fit placed; it carries the area cursor (area start) address.
        S->Address = IsegStart;
      }
    }

  // SSEG overlay group: SPEC §5.2/§5.1.  If --stack-size is given, use it as
  // the reservation size.  Otherwise, when valid SSEG input exists, select the
  // largest free contiguous region in [IsegStart, IsegEnd) that is at least the
  // max input section size.
  //
  // The complete ReserveSize is placed in one first-fit allocation within the
  // constrained window [IsegStart, IsegEnd).  This ensures that the stack
  // cannot spill outside the ISEG/SSEG window (e.g. --stack-size=129 with a
  // 128-byte default window must fail).
  {
    uint32_t SsegInputMax = 0;
    InputSection *SsegRepresentative = nullptr;
    for (InputSection *S : AllSections)
      if (S->Group == "SSEG") {
        if (S->Size >= SsegInputMax) {
          SsegInputMax = S->Size;
          SsegRepresentative = S;
        }
      }
    if (SsegRepresentative && SsegInputMax) {
      uint32_t ReserveSize;
      if (Config.StackSize > 0) {
        ReserveSize = Config.StackSize;
        if (ReserveSize < SsegInputMax)
          return fail(Err, "--stack-size is smaller than SSEG input size");
      } else {
        // Auto-size: find the largest free contiguous region in
        // [IsegStart, IsegEnd) and use it as the reservation.
        ReserveSize = SsegInputMax;
        for (uint32_t A = IsegStart; A < IsegEnd; ++A) {
          uint32_t End = A;
          while (End < IsegEnd) {
            bool Used = false;
            for (const Range &R : DataUsed)
              if (R.Start <= End && End < R.End) {
                Used = true;
                break;
              }
            if (Used)
              break;
            ++End;
          }
          uint32_t HoleSize = End - A;
          if (HoleSize >= SsegInputMax && HoleSize > ReserveSize)
            ReserveSize = HoleSize;
          A = End; // skip to end of this hole
        }
      }
      // Allocate the complete ReserveSize in one first-fit pass within the
      // constrained window [IsegStart, IsegEnd).  This replaces the old
      // allocate(input) + reserve(extra) pattern that failed to enforce the
      // window boundary.
      uint32_t SsegBase = 0;
      bool Found = false;
      for (uint32_t A = IsegStart; A < IsegEnd && ReserveSize <= IsegEnd - A;
           ++A) {
        Range R{A, A + ReserveSize};
        bool Good = true;
        for (const Range &U : DataUsed)
          if (R.Start < U.End && U.Start < R.End) {
            Good = false;
            break;
          }
        if (Good) {
          SsegBase = A;
          Found = true;
          break;
        }
      }
      if (!Found)
        return fail(Err, "cannot allocate SSEG (stack) in ISEG window");
      if (!reserve(SsegBase, ReserveSize, "SSEG"))
        return false;
      SsegRepresentative->Address = SsegBase;
      SsegReservedSize = ReserveSize;
      for (InputSection *S : AllSections)
        if (S->Group == "SSEG")
          S->Address = SsegBase;
    }
  }

  XDataUsed.clear();
  bool HasXSeg = llvm::any_of(AllSections,
                              [](const InputSection *S) { return S->Region == "XSEG"; });
  if (HasXSeg && !hasAreaStart("XSEG"))
    return fail(Err, "missing --area-start=XSEG");
  uint32_t XsegCursor = areaStart("XSEG", 0);
  for (InputSection *S : AllSections)
    if (S->Region == "XSEG") {
      if (hasAreaStart(S->Name))
        XsegCursor = areaStart(S->Name, 0);
      if (!rangeFits(XsegCursor, S->Size))
        return fail(Err, "XDATA address overflow in " + S->Name);
      Range R{XsegCursor, XsegCursor + static_cast<uint32_t>(S->Size)};
      for (const Range &U : XDataUsed)
        if (R.Start < U.End && U.Start < R.End)
          return fail(Err, "XDATA overlap for " + S->Name);
      S->Address = XsegCursor;
      if (S->Size)
        XDataUsed.push_back(R);
      XsegCursor += S->Size;
    }

  Synth["s_DSEG"] = 0;
  uint32_t LowUsed = 0;
  for (uint32_t A = 0; A != 0x80; ++A)
    for (const Range &R : DataUsed)
      if (R.Start <= A && A < R.End) {
        ++LowUsed;
        break;
      }
  Synth["l_DSEG"] = LowUsed;
  Synth["l_IRAM"] = (Config.IramSize > 0 && Config.IramSize <= 0x100)
                        ? Config.IramSize : 0x100;
  for (const char *R : {"HOME", "VECS", "BOOT", "CSEG", "XINIT"}) {
    uint32_t Start = areaStart(R, 0), End = Start;
    for (InputSection *S : AllSections)
      if (S->Region == R)
        End = std::max(End, S->Address + static_cast<uint32_t>(S->Size));
    Synth[(Twine("s_") + R).str()] = Start;
    Synth[(Twine("l_") + R).str()] = End - Start;
  }
  for (InputSection *S : AllSections)
    if (S->Region == "OSEG") {
      Synth["s_OSEG"] = S->Address;
      Synth["l_OSEG"] = std::max(Synth["l_OSEG"],
                                  static_cast<uint32_t>(S->Size));
    }
  // ISEG: s_ is the address of the first non-empty slice in input order and
  // l_ is the sum of slice sizes, matching the legacy linker mcs251_ld.py
  // (ap.addr = ap.areaxs[0].addr, then replaced by the first areax with
  // size != 0; size accumulates size += ax.size).  Zero-length ISEG sections
  // still generate boundary symbols; when every slice is empty, s_ keeps the
  // empty-area address (the area cursor carried by the empty slices).
  {
    InputSection *FirstIseg = nullptr;
    InputSection *FirstUsedIseg = nullptr;
    uint32_t IsegTotal = 0;
    for (InputSection *S : AllSections)
      if (S->Region == "ISEG") {
        if (!FirstIseg)
          FirstIseg = S;
        if (S->Size && !FirstUsedIseg)
          FirstUsedIseg = S;
        IsegTotal += static_cast<uint32_t>(S->Size);
      }
    if (FirstIseg) {
      Synth["s_ISEG"] =
          FirstUsedIseg ? FirstUsedIseg->Address : FirstIseg->Address;
      Synth["l_ISEG"] = IsegTotal;
    }
  }
  // SSEG overlay group: s_ is the group base, l_ is the reserved max size.
  for (InputSection *S : AllSections)
    if (S->Group == "SSEG") {
      Synth["s_SSEG"] = S->Address;
      Synth["l_SSEG"] = std::max(Synth["l_SSEG"], SsegReservedSize);
    }
  // BSEG_BYTES: start (first non-empty slice) and span.
  {
    InputSection *FirstBseg = nullptr;
    uint32_t BsegEnd = 0;
    for (InputSection *S : AllSections)
      if (S->Region == "BSEG_BYTES" && S->Size) {
        if (!FirstBseg)
          FirstBseg = S;
        BsegEnd = std::max(BsegEnd, S->Address +
                                        static_cast<uint32_t>(S->Size));
      }
    if (FirstBseg) {
      Synth["s_BSEG_BYTES"] = FirstBseg->Address;
      Synth["l_BSEG_BYTES"] =
          std::max(Synth["l_BSEG_BYTES"],
                   BsegEnd - FirstBseg->Address);
    }
  }
  // BIT_BANK overlay group.
  for (InputSection *S : AllSections)
    if (S->Group == "BIT_BANK") {
      Synth["s_BIT_BANK"] = S->Address;
      uint32_t BbMax = 0;
      for (InputSection *S2 : AllSections)
        if (S2->Group == "BIT_BANK")
          BbMax = std::max(BbMax, static_cast<uint32_t>(S2->Size));
      Synth["l_BIT_BANK"] = std::max(Synth["l_BIT_BANK"], BbMax);
    }
  // REG_BANK_n overlay groups.
  for (uint32_t Bank = 0; Bank != 4; ++Bank) {
    std::string Group = (Twine("REG_BANK_") + Twine(Bank)).str();
    bool HasReg = false;
    uint32_t RegMax = 0;
    for (InputSection *S : AllSections)
      if (S->Group == Group) {
        HasReg = true;
        RegMax = std::max(RegMax, static_cast<uint32_t>(S->Size));
      }
    if (HasReg) {
      Synth[(Twine("s_") + Group).str()] = Bank * 8;
      Synth[(Twine("l_") + Group).str()] = RegMax;
    }
  }
  // XSEG: start and span.
  if (hasAreaStart("XSEG")) {
    uint32_t XsegLow = areaStart("XSEG", 0), XsegHigh = XsegLow;
    bool HasXseg = false;
    for (InputSection *S : AllSections)
      if (S->Region == "XSEG" && S->Size) {
        HasXseg = true;
        XsegLow = std::min(XsegLow, S->Address);
        XsegHigh = std::max(XsegHigh, S->Address +
                                          static_cast<uint32_t>(S->Size));
      }
    if (HasXseg) {
      Synth["s_XSEG"] = XsegLow;
      Synth["l_XSEG"] = XsegHigh - XsegLow;
    }
  }

  for (auto &F : Files)
    for (auto &S : F->Symbols)
      if (S.Sec && S.Sec->IsAlloc)
        S.Address = S.Sec->Address + S.Value;
      else if (S.Defined)
        S.Address = S.Value;

  bool StackDefinition = false;
  for (const auto &F : Files)
    for (const InputSymbol &S : F->Symbols)
      if (S.Name == "__mcs251_stack_base") {
        StackRequested |= !S.Defined;
        StackDefinition |= S.Defined;
      }
  if (StackRequested && StackDefinition)
    return fail(Err, "user definition of __mcs251_stack_base");
  if (StackRequested) {
    uint32_t First = ((StackH + 15) & ~15u) + 16;
    if (Config.EnableStackGate &&
        (Config.EdataEnd + 1 < First || Config.EdataEnd + 1 - First < 1024))
      return fail(Err, "stack capacity is less than 1024 bytes");
    SPX = First - 1;
    Capacity = Config.EdataEnd + 1 >= First ? Config.EdataEnd + 1 - First : 0;
    Synth["__mcs251_stack_base"] = SPX;
  }
  return true;
}

bool Linker::layout() { return layoutCode() && layoutData(); }

bool Linker::errorUndefined() {
  for (const auto &F : Files)
    for (const InputSymbol &S : F->Symbols) {
      if (S.Defined || S.Name.empty())
        continue;
      if (S.Bind == ELF::STB_LOCAL)
        return fail(Err, "undefined local symbol: " + S.Name);
      if (!Globals.count(S.Name) && !Synth.count(S.Name))
        return fail(Err, "undefined symbol: " + S.Name);
    }
  return true;
}

bool Linker::applyRelocations() {
  std::set<std::pair<InputSection *, uint32_t>> Written;
  for (auto &F : Files)
    for (auto &S : F->Sections)
      for (const Relocation &R : S->Relocs) {
        InputSymbol *IS = findSymbol(*F, R.Sym);
        if (!IS)
          return fail(Err, "invalid relocation symbol");
        InputSymbol *Target = IS;
        if (!IS->Defined) {
          auto It = Globals.find(IS->Name);
          if (It != Globals.end())
            Target = It->second;
        }
        uint32_t V = IS->Defined ? IS->Address
                     : Globals.count(IS->Name) ? Globals[IS->Name]->Address
                     : Synth.count(IS->Name) ? Synth[IS->Name]
                     : 0;
        int64_t Value = static_cast<int64_t>(V) + R.Addend;
        uint32_t Width = (R.Type == ELF::R_MCS251_16 ||
                          R.Type == ELF::R_MCS251_J16 ||
                          R.Type == ELF::R_MCS251_J11) ? 2
                         : R.Type == ELF::R_MCS251_24 ? 3
                         : R.Type == ELF::R_MCS251_NONE ? 0 : 1;
        if (!Width)
          continue;
        if (R.Offset + Width > S->Size || S->IsNobits)
          return fail(Err, "relocation writes outside PROGBITS section " + S->Name);
        for (uint32_t I = 0; I != Width; ++I)
          if (!Written.insert({S.get(), R.Offset + I}).second)
            return fail(Err, "overlapping relocation in " + S->Name);
        uint32_t P = S->Address + R.Offset;
        if (R.Type == ELF::R_MCS251_PC8)
          Value -= static_cast<int64_t>(P) + 1;
        const bool Is24Slice = R.Type == ELF::R_MCS251_16 ||
                               R.Type == ELF::R_MCS251_LO8 ||
                               R.Type == ELF::R_MCS251_MID8 ||
                               R.Type == ELF::R_MCS251_HI8;
        if ((R.Type == ELF::R_MCS251_PC8 && (Value < -128 || Value > 127)) ||
            (R.Type == ELF::R_MCS251_24 && (Value < 0 || Value > 0xffffff)) ||
            (Is24Slice && (Value < -0x800000 || Value > 0xffffff)) ||
            ((R.Type == ELF::R_MCS251_J16 || R.Type == ELF::R_MCS251_J11) &&
             (Value < 0 || Value > 0xffffff)))
          return fail(Err, "relocation overflow in " + S->Name);
        uint32_t U = static_cast<uint32_t>(Value);
        if ((R.Type == ELF::R_MCS251_J16 || R.Type == ELF::R_MCS251_J11) &&
            (!Target->Sec || !Target->Sec->IsCode))
          return fail(Err, "control relocation target is not CODE in " + S->Name);
        if (R.Type == ELF::R_MCS251_J16 &&
            (((P + 2) & 0xffffff) & 0xff0000) != (U & 0xff0000))
          return fail(Err, "J16 bank overflow in " + S->Name);
        auto Put = [&](uint32_t O, uint8_t B) { S->Data[O] = B; Image[S->Address + O] = B; };
        switch (R.Type) {
        case ELF::R_MCS251_16:
        case ELF::R_MCS251_J16:
          Put(R.Offset, U >> 8); Put(R.Offset + 1, U); break;
        case ELF::R_MCS251_24:
          Put(R.Offset, U >> 16); Put(R.Offset + 1, U >> 8); Put(R.Offset + 2, U); break;
        case ELF::R_MCS251_LO8: Put(R.Offset, U); break;
        case ELF::R_MCS251_MID8: Put(R.Offset, U >> 8); break;
        case ELF::R_MCS251_HI8: Put(R.Offset, U >> 16); break;
        case ELF::R_MCS251_PC8: Put(R.Offset, static_cast<uint8_t>(Value)); break;
        case ELF::R_MCS251_J11: {
          if (R.Offset + 1 >= S->Data.size())
            return fail(Err, "J11 overflow");
          const uint8_t Opcode = S->Data[R.Offset] & 0x1f;
          if (Opcode != 0x01 && Opcode != 0x11)
            return fail(Err, "J11 relocation is not ACALL/AJMP in " + S->Name);
          if ((((P + 2) & 0xffffff) & 0xfffff800) != (U & 0xfffff800))
            return fail(Err, "J11 page overflow in " + S->Name);
          Put(R.Offset, (S->Data[R.Offset] & 0x1f) | ((U >> 3) & 0xe0));
          Put(R.Offset + 1, U);
          break;
        }
        default: return fail(Err, "unknown MCS251 relocation");
        }
      }
  return true;
}

bool Linker::validateXInit() {
  bool InitializerPresent = false;
  for (const auto &F : Files)
    for (const InputSymbol &S : F->Symbols)
      if (S.Name == "__mcs251_globals_init" && S.Defined)
        InitializerPresent = true;
  if (!InitializerPresent)
    return true;
  uint64_t TotalXInit = 0;
  for (InputSection *S : AllSections) {
    if (S->Region != "XINIT")
      continue;
    TotalXInit += S->Size;
  }
  if (TotalXInit > 0xffff)
    return fail(Err, "XINIT total length exceeds 65535 bytes");
  for (InputSection *S : AllSections) {
    if (S->Region != "XINIT")
      continue;
    if (S->Size > 0xffff)
      return fail(Err, "XINIT exceeds 65535 bytes");
    size_t Offset = 0;
    while (Offset != S->Data.size()) {
      if (S->Data.size() - Offset < 6)
        return fail(Err, "truncated XINIT record in " + S->Name);
      ArrayRef<uint8_t> Record(S->Data);
      uint32_t Destination = (uint32_t(Record[Offset]) << 8) | Record[Offset + 1];
      uint32_t ObjectSize = (uint32_t(Record[Offset + 2]) << 8) | Record[Offset + 3];
      uint32_t PayloadSize = (uint32_t(Record[Offset + 4]) << 8) | Record[Offset + 5];
      if (!ObjectSize || (PayloadSize != 0 && PayloadSize != ObjectSize) ||
          PayloadSize > S->Data.size() - Offset - 6)
        return fail(Err, "invalid XINIT record in " + S->Name);
      if (Destination + ObjectSize > 0x10000)
        return fail(Err, "XINIT destination overflows DATA in " + S->Name);
      bool WithinOneSlice = false;
      for (InputSection *D : AllSections)
        if ((D->Region == "DSEG" || D->Region == "DATA_ABS") &&
            Destination >= D->Address &&
            rangeFits(uint64_t(Destination) - D->Address, ObjectSize,
                      D->Size)) {
          WithinOneSlice = true;
          break;
        }
      if (!WithinOneSlice)
        return fail(Err, "XINIT destination is not within one DSEG slice");
      Offset += 6 + PayloadSize;
    }
  }
  return true;
}

void Linker::printInputs(raw_ostream &Out) const {
  for (const auto &F : Files) {
    Out << "file " << F->Path << '\n';
    for (const auto &S : F->Sections) {
      Out << "  section[" << S->Index << "] " << S->Name << " type="
          << S->Type << " flags=" << format_hex(S->Flags, 0, false)
          << " size=" << S->Size << '\n';
      for (const Relocation &R : S->Relocs)
        Out << "    rela offset=" << format_hex(R.Offset, 0, false)
            << " type=" << R.Type << " sym=" << R.Sym
            << " addend=" << R.Addend << '\n';
    }
    for (uint32_t I = 0; I != F->Symbols.size(); ++I) {
      const InputSymbol &S = F->Symbols[I];
      Out << "  symbol[" << I << "] " << S.Name << " section=" << S.Section
          << " value=" << format_hex(S.Value, 0, false) << " size=" << S.Size
          << " bind=" << unsigned(S.Bind) << " type=" << unsigned(S.Type) << '\n';
    }
  }
}

void Linker::buildMap(raw_ostream &Out) const {
  Out << "MCS251 map\n";
  for (const auto &P : Synth)
    Out << P.first << " = " << format_hex(P.second, 6, false) << '\n';
  for (const auto &F : Files)
    for (const auto &S : F->Sections)
      if (S->IsAlloc)
        Out << F->Path << ":" << S->Name << " "
            << format_hex(S->Address, 6, false) << " +"
            << format_hex(S->Size, 0, false) << "\n";
  if (StackRequested)
    Out << "stack H=" << format_hex(StackH, 4, false) << " SPX="
        << format_hex(SPX, 4, false) << " capacity=" << Capacity
        << " edata_end=" << format_hex(Config.EdataEnd, 4, false) << '\n';
}

bool Linker::run(LinkerResult &Result) {
  for (StringRef P : Config.Inputs) {
    auto F = std::make_unique<InputFile>();
    if (!loadFile(P, *F, Err))
      return false;
    for (auto &S : F->Sections)
      if (S->IsAlloc)
        AllSections.push_back(S.get());
    Files.push_back(std::move(F));
  }
  if (Files.empty())
    return fail(Err, "no input files");
  raw_string_ostream InputOS(Result.InputReport);
  printInputs(InputOS);
  InputOS.flush();
  if (Config.PrintInput)
    return true;
  if (!resolveSymbols() || !layout() || !errorUndefined() ||
      !applyRelocations() || !validateXInit())
    return false;
  Result.Entry = llvm::any_of(AllSections,
                              [](const InputSection *S) { return S->Region == "HOME"; })
                     ? areaStart("HOME", 0) : 0;
  Result.Image = std::move(Image);
  raw_string_ostream MapOS(Result.Map);
  buildMap(MapOS);
  MapOS.flush();
  return true;
}

bool linkCore(LinkerConfig Config, LinkerResult &Result, raw_ostream &Err) {
  return Linker(std::move(Config), Err).run(Result);
}

} // namespace lld::mcs251
