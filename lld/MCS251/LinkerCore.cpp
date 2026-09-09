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
#include "llvm/BinaryFormat/MCS251ISR.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
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
struct InputSymbol;

struct Relocation {
  uint32_t Offset = 0;
  uint32_t Type = 0;
  uint32_t Sym = 0;
  int32_t Addend = 0;
};

// One parsed 24-byte record of a `.mcs251.isr` metadata section (A3.2/A3.3).
// The zero-width type9 association is resolved to the exact InputSymbol* only
// after resolveSymbols() (T07 step 5).
struct IsrRecord {
  uint32_t Kind = 0;
  uint32_t EntryKind = 0;
  uint32_t HW = 0;
  uint32_t Save = 0;
  uint32_t Slot = 0;
  uint32_t Asset = 0;
  uint32_t Offset = 0;   // Record base inside the metadata section.
  uint32_t SymIndex = 0; // Symbol table index of the type9 association.
  InputSymbol *Ref = nullptr; // Exact referenced definition.
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
  // A3: at most one `.mcs251.isr` per object; presence triggers IRQ mode.
  bool HasIsrMeta = false;
  InputSection *MetaSection = nullptr;
  std::vector<IsrRecord> IsrRecords;
};

// E3: one occupied DATA range plus its provenance.  The owner string is the
// conflict source quoted by allocation-failure diagnostics, so a user can fix
// the layout without reading the linker implementation.
struct DataUse {
  Range R;
  std::string What;
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

static uint32_t relocWidth(uint32_t Type) {
  return Type == ELF::R_MCS251_16 || Type == ELF::R_MCS251_J16 ||
                 Type == ELF::R_MCS251_J11 ? 2
                 : Type == ELF::R_MCS251_24 ? 3
                 : Type == ELF::R_MCS251_NONE ? 0
                                              : 1;
}

// A5: each legal vector slot starts with the EJMP opcode byte 0x8A followed
// by the 3-byte absolute target (R_MCS251_24 field).
static constexpr uint8_t EJMP_OPCODE = 0x8A;

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
  // A3.2: the ISR metadata section is whitelisted by its exact name only; no
  // `.mcs251.*` wildcard exists anywhere in the non-ALLOC whitelist.
  if (N == MCS251ISR::MetaSectionName)
    return S.Type == ELF::SHT_PROGBITS && S.Flags == 0 &&
               S.Align == MCS251ISR::MetaSectionAlignment ||
           fail(Err, "malformed " + MCS251ISR::MetaSectionName);
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
    if (S->Name == MCS251ISR::MetaSectionName) {
      // A3.2 frozen structure: no entry size, records only, no padding. An
      // empty metadata section is an incomplete registration, not a legal
      // input (R1).
      if (S->Size == 0)
        return fail(Err, Path + ": MCS251 ISR: empty .mcs251.isr section");
      if (H.sh_entsize != 0)
        return fail(Err, Path + ": MCS251 ISR: .mcs251.isr must have sh_entsize 0");
      if (S->Size % MCS251ISR::RecordSize != 0)
        return fail(Err, Path + ": MCS251 ISR: .mcs251.isr size must be a "
                             "multiple of 24 with no trailing padding");
      if (F.MetaSection)
        return fail(Err, Path + ": MCS251 ISR: at most one .mcs251.isr per object");
      F.MetaSection = S.get();
    }
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

  // A3.2: parse the exact ISR metadata records. Field-level structure is
  // validated here (input structure validation, so `--print-input` exercises
  // it); cross-record pairing and dedup need resolved symbols and run later.
  if (F.MetaSection) {
    F.HasIsrMeta = true;
    const InputSection &M = *F.MetaSection;
    const ArrayRef<uint8_t> B(M.Data);
    // Subtraction-style loop bound: the size is already known to be a
    // multiple of 24, so the last record ends exactly at the section end and
    // no offset+size addition can overflow.
    for (uint32_t Off = 0; M.Size != 0 && Off <= M.Size - MCS251ISR::RecordSize;
         Off += MCS251ISR::RecordSize) {
      IsrRecord R;
      R.Offset = Off;
      if (read16BE(B, Off + MCS251ISR::RecordOffset::ProtocolVersion) !=
          MCS251ISR::ProtocolVersion)
        return fail(Err, Path + ": MCS251 ISR: unsupported metadata version");
      if (read16BE(B, Off + MCS251ISR::RecordOffset::RecordSizeField) !=
          MCS251ISR::RecordSize)
        return fail(Err, Path + ": MCS251 ISR: invalid metadata record size");
      R.Kind = B[Off + MCS251ISR::RecordOffset::RecordKind];
      R.EntryKind = B[Off + MCS251ISR::RecordOffset::EntryKind];
      R.HW = B[Off + MCS251ISR::RecordOffset::HardwareProfile];
      R.Save = B[Off + MCS251ISR::RecordOffset::SaveProfile];
      R.Slot = read16BE(B, Off + MCS251ISR::RecordOffset::VectorSlot);
      R.Asset = read32BE(B, Off + MCS251ISR::RecordOffset::AssetProfile);
      if (read16BE(B, Off + MCS251ISR::RecordOffset::RequiredCaps) !=
          MCS251ISR::RequiredCaps)
        return fail(Err, Path + ": MCS251 ISR: unsupported required caps");
      if (read32BE(B, Off + MCS251ISR::RecordOffset::SymbolReference) != 0)
        return fail(Err, Path + ": MCS251 ISR: symbol_reference must be zero; "
                             "the association is carried by the type9 RELA");
      if (read32BE(B, Off + MCS251ISR::RecordOffset::Reserved) != 0)
        return fail(Err, Path + ": MCS251 ISR: reserved field must be zero");
      // A3.3 frozen per-kind profile: (entry_kind, hardware, save, asset).
      uint32_t ExpectedEntry, ExpectedHW, ExpectedSave, ExpectedAsset;
      switch (R.Kind) {
      case MCS251ISR::RK_ISR_ENTRY:
      case MCS251ISR::RK_ISR_REGISTER:
        ExpectedEntry = MCS251ISR::EK_IRQ_RETI;
        ExpectedHW = MCS251ISR::HardwareProfileIRQ4;
        ExpectedSave = MCS251ISR::SaveProfileINT37;
        ExpectedAsset = MCS251ISR::AssetProfileCompiled;
        break;
      case MCS251ISR::RK_IRQ_DEFAULT:
        ExpectedEntry = MCS251ISR::EK_IRQ_STOP;
        ExpectedHW = MCS251ISR::HardwareProfileIRQ4;
        ExpectedSave = 0;
        ExpectedAsset = MCS251ISR::AssetProfileCRT;
        break;
      case MCS251ISR::RK_IRQ_RESET:
        ExpectedEntry = MCS251ISR::EK_RESET;
        ExpectedHW = 0;
        ExpectedSave = 0;
        ExpectedAsset = MCS251ISR::AssetProfileCRT;
        break;
      default:
        return fail(Err, Path + ": MCS251 ISR: unknown metadata record kind");
      }
      if (R.Kind == MCS251ISR::RK_ISR_ENTRY ||
          R.Kind == MCS251ISR::RK_ISR_REGISTER) {
        // A4: only the 39 legal slots are user-assignable; reserved, system
        // and out-of-profile numbers (including FFFF) are rejected.
        if (R.Slot == MCS251ISR::NoSlot || !MCS251ISR::isLegalISRSlot(R.Slot))
          return fail(Err, Path + ": MCS251 ISR: vector is not a legal slot "
                             "in profile 0-51");
      } else if (R.Slot != MCS251ISR::NoSlot) {
        return fail(Err, Path + ": MCS251 ISR: default/reset record must not "
                             "claim a slot");
      }
      if (R.EntryKind != ExpectedEntry || R.HW != ExpectedHW ||
          R.Save != ExpectedSave || R.Asset != ExpectedAsset)
        return fail(Err, Path + ": MCS251 ISR: record kind " + Twine(R.Kind) +
                             " does not match the frozen "
                             "entry/hardware/save/asset profile");
      F.IsrRecords.push_back(R);
    }
  }

  unsigned MetaRelaCount = 0;
  for (uint32_t I = 0; I != RawSections->size(); ++I) {
    const ELF32BE::Shdr &Rela = (*RawSections)[I];
    if (Rela.sh_type != ELF::SHT_RELA)
      continue;
    if (Rela.sh_info == 0 || Rela.sh_info >= F.Sections.size() ||
        Rela.sh_link != SymtabIndex || Rela.sh_entsize != sizeof(ELF32BE::Rela))
      return fail(Err, Path + ": malformed SHT_RELA section");
    InputSection *TS = F.Sections[Rela.sh_info].get();
    Expected<ELF32BE::RelaRange> Relocs = ELF.relas(Rela);
    if (!Relocs) {
      consumeError(Relocs.takeError());
      return fail(Err, Path + ": malformed SHT_RELA section");
    }
    if (TS == F.MetaSection) {
      // A3.4/R1: exactly one association RELA per metadata section, under the
      // frozen name (ordinary RELAs keep free naming; this one does not).
      ++MetaRelaCount;
      if (F.Sections[I]->Name !=
          (Twine(".rela") + MCS251ISR::MetaSectionName).str())
        return fail(Err, Path + ": MCS251 ISR: metadata RELA must be named "
                             ".rela.mcs251.isr");
      // A3.4: the zero-width type9 association has its own structural
      // validation, deliberately separate from the ordinary relocation path.
      // These relocations are consumed here; they are never stored for
      // applyRelocations() and never write bytes.
      if (Relocs->size() != F.IsrRecords.size())
        return fail(Err, Path + ": MCS251 ISR: metadata needs exactly one "
                             "type9 relocation per record");
      std::vector<bool> Covered(F.IsrRecords.size(), false);
      for (const ELF32BE::Rela &RelaEntry : *Relocs) {
        Relocation R{static_cast<uint32_t>(RelaEntry.r_offset),
                     RelaEntry.getType(false), RelaEntry.getSymbol(false),
                     static_cast<int32_t>(RelaEntry.r_addend)};
        if (R.Type != ELF::R_MCS251_ISR_REF)
          return fail(Err, Path + ": MCS251 ISR: only R_MCS251_ISR_REF is "
                             "allowed in the ISR metadata RELA");
        if (R.Addend != 0)
          return fail(Err, Path + ": MCS251 ISR: type9 addend must be zero");
        if (R.Sym >= F.Symbols.size())
          return fail(Err, Path + ": relocation symbol index out of range");
        // Subtraction-style bound: Offset >= 12 first so no unsigned
        // wraparound can manufacture a valid record index.
        if (R.Offset < 12)
          return fail(Err, Path + ": MCS251 ISR: type9 offset must be record "
                             "base + 12");
        const uint32_t Into = R.Offset - 12;
        if (Into % MCS251ISR::RecordSize != 0 ||
            Into / MCS251ISR::RecordSize >= F.IsrRecords.size())
          return fail(Err, Path + ": MCS251 ISR: type9 offset must be record "
                             "base + 12");
        const uint32_t Rec = Into / MCS251ISR::RecordSize;
        if (Covered[Rec])
          return fail(Err, Path + ": MCS251 ISR: each metadata record carries "
                             "exactly one type9 relocation");
        Covered[Rec] = true;
        const InputSymbol &Sym = F.Symbols[R.Sym];
        if (Sym.Name.empty() || Sym.Type != ELF::STT_FUNC)
          return fail(Err, Path + ": MCS251 ISR: type9 must name an STT_FUNC "
                             "symbol (never a section+addend fold)");
        if (Sym.Section == ELF::SHN_ABS)
          return fail(Err, Path + ": MCS251 ISR: type9 must not reference "
                             "SHN_ABS");
        if (!Sym.Defined)
          return fail(Err, Path + ": MCS251 ISR: ISR identity must reference "
                             "a definition in the same object");
        if (!Sym.Sec || !Sym.Sec->IsAlloc || !Sym.Sec->IsCode ||
            Sym.Sec->Type != ELF::SHT_PROGBITS)
          return fail(Err, Path + ": MCS251 ISR: ISR target must live in an "
                             "executable PROGBITS section");
        F.IsrRecords[Rec].SymIndex = R.Sym;
        TS->RelocIndex = I;
      }
      for (size_t Rec = 0; Rec != Covered.size(); ++Rec)
        if (!Covered[Rec])
          return fail(Err, Path + ": MCS251 ISR: metadata record " + Twine(Rec) +
                             " has no type9 association");
      continue;
    }
    if (!TS->IsAlloc || TS->Type == ELF::SHT_NOBITS)
      return fail(Err, Path + ": RELA targets non-loadable section " + TS->Name);
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
      if (R.Type == ELF::R_MCS251_ISR_REF)
        return fail(Err, Path + ": MCS251 ISR: R_MCS251_ISR_REF is only "
                           "allowed in the ISR metadata RELA");
      uint32_t Width = relocWidth(R.Type);
      if (R.Type > ELF::R_MCS251_J11 || R.Offset > TS->Size ||
          Width > TS->Size - R.Offset)
        return fail(Err, Path + ": relocation offset/type out of range");
      TS->RelocIndex = I;
      TS->Relocs.push_back(R);
    }
  }
  // R1: object-level association completeness, enforced at input-structure
  // validation time (--print-input rejects it too), never deferred to the
  // vector stage.
  if (F.MetaSection) {
    if (MetaRelaCount == 0)
      return fail(Err, Path + ": MCS251 ISR: metadata records have no "
                         ".rela.mcs251.isr association section");
    if (MetaRelaCount > 1)
      return fail(Err, Path + ": MCS251 ISR: more than one RELA targets "
                         ".mcs251.isr");
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
  std::vector<DataUse> DataUsed;
  uint32_t StackH = 0;
  uint32_t SPX = 0;
  uint32_t Capacity = 0;
  uint32_t SsegReservedSize = 0;
  bool StackRequested = false;

  uint32_t areaStart(StringRef Name, uint32_t Default) const;
  bool hasAreaStart(StringRef Name) const;
  bool rejectInputVecs();
  bool resolveSymbols();
  bool validateISRIdentitiesAndRegistrations();
  bool synthesizeIRQVectors();
  bool layout();
  bool checkFlashGate();
  bool validateIRQReservedRangesAndCRT();
  bool layoutCode();
  bool layoutData();
  bool allocate(InputSection &S, uint32_t Lo, uint32_t Hi);
  bool reserve(uint32_t Start, uint32_t Size, StringRef What);
  bool applyRelocations();
  bool applyVectorJumps();
  bool validateIRQFinalAssets();
  bool validateXInit();
  void buildMap(raw_ostream &Out) const;
  void collectSymbols(std::vector<OutputSymbol> &Out) const;
  void printInputs(raw_ostream &Out) const;
  InputSymbol *findSymbol(InputFile &F, uint32_t Index);
  bool errorUndefined();

  // IRQ mode (A3.6): triggered by any input carrying `.mcs251.isr`.
  bool IrqMode = false;
  InputFile *CrtFile = nullptr;
  InputSymbol *DefaultSym = nullptr;
  InputSymbol *ResetSym = nullptr;
  std::set<InputSymbol *> IsrSymbols;    // Exact registered ISR identities.
  InputSymbol *SlotSym[52] = {};         // Registered handler per legal slot.
  std::vector<std::unique_ptr<InputSection>> OwnedSynth;
  std::vector<InputSection *> SynthSections;
  std::vector<std::pair<InputSection *, uint32_t>> SynthExpect;
  std::vector<std::pair<InputSection *, InputSymbol *>> VectorJumps;
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

// T07 step 7: the synthesized table is the only VECS in IRQ mode. Any input
// VECS section is rejected, including a zero-size one: an old asset must not
// masquerade as the companion CRT.
bool Linker::rejectInputVecs() {
  for (const auto &F : Files)
    for (const auto &S : F->Sections)
      if (S->Region == "VECS")
        return fail(Err, F->Path + ": MCS251 ISR: input VECS section " +
                             S->Name + " is forbidden; the linker synthesizes "
                             "the only vector table (zero-size included)");
  return true;
}

// A3.3 pairing and dedup on the exact InputSymbol* identity table, built
// after resolveSymbols() (T07 step 5).
bool Linker::validateISRIdentitiesAndRegistrations() {
  // Records may only reference same-object definitions (enforced at load),
  // so the per-file InputSymbol pointer is the precise function identity.
  for (auto &F : Files)
    for (IsrRecord &R : F->IsrRecords)
      R.Ref = &F->Symbols[R.SymIndex];

  // Collect every symbol a kind3 IRQ_DEFAULT record references, so the
  // "no user registration on the default entry" rule can fire regardless of
  // input order or which object carries the default record.
  std::set<InputSymbol *> DefaultRefs;
  for (const auto &F : Files)
    for (const IsrRecord &R : F->IsrRecords)
      if (R.Kind == MCS251ISR::RK_IRQ_DEFAULT)
        DefaultRefs.insert(R.Ref);

  // Per-object ENTRY/REGISTER pairing, in-object duplicate detection, and
  // the one-definition-one-slot rule across all objects.
  std::set<uint32_t> SlotOwner;
  for (auto &F : Files) {
    std::map<uint32_t, const IsrRecord *> EntryBySlot;
    std::map<uint32_t, const IsrRecord *> RegBySlot;
    // Function-level duplicate first: two REGISTERs for one precise symbol.
    std::map<InputSymbol *, unsigned> RegCount;
    for (const IsrRecord &R : F->IsrRecords)
      if (R.Kind == MCS251ISR::RK_ISR_REGISTER)
        ++RegCount[R.Ref];
    for (const auto &C2 : RegCount)
      if (C2.second > 1)
        return fail(Err, F->Path + ": MCS251 ISR: duplicate registration of "
                             "the same function " + C2.first->Name);
    for (const IsrRecord &R : F->IsrRecords) {
      if (R.Kind == MCS251ISR::RK_ISR_ENTRY) {
        if (!EntryBySlot.emplace(R.Slot, &R).second)
          return fail(Err, F->Path + ": MCS251 ISR: two ENTRY records for "
                             "slot " + Twine(R.Slot));
      } else if (R.Kind == MCS251ISR::RK_ISR_REGISTER) {
        if (!RegBySlot.emplace(R.Slot, &R).second)
          return fail(Err, F->Path + ": MCS251 ISR: two REGISTER records for "
                             "slot " + Twine(R.Slot));
      }
    }
    for (const auto &P : EntryBySlot) {
      auto It = RegBySlot.find(P.first);
      if (It == RegBySlot.end())
        return fail(Err, F->Path + ": MCS251 ISR: ENTRY without REGISTER for "
                           "slot " + Twine(P.first));
      if (It->second->Ref != P.second->Ref)
        return fail(Err, F->Path + ": MCS251 ISR: ENTRY and REGISTER must "
                           "reference the same exact function for slot " +
                           Twine(P.first));
      if (!SlotOwner.insert(P.first).second)
        return fail(Err, F->Path + ": MCS251 ISR: duplicate registration of "
                           "slot " + Twine(P.first));
      SlotSym[P.first] = P.second->Ref;
      IsrSymbols.insert(P.second->Ref);
    }
    for (const auto &P : RegBySlot)
      if (!EntryBySlot.count(P.first))
        return fail(Err, F->Path + ": MCS251 ISR: REGISTER without ENTRY for "
                           "slot " + Twine(P.first));
  }

  // A3.3: a user registration may never hang off the default entry.
  for (const auto &F : Files)
    for (const IsrRecord &R : F->IsrRecords)
      if ((R.Kind == MCS251ISR::RK_ISR_ENTRY ||
           R.Kind == MCS251ISR::RK_ISR_REGISTER) &&
          DefaultRefs.count(R.Ref))
        return fail(Err, F->Path + ": MCS251 ISR: user registration must not "
                           "reference the default entry");

  // A3.3: exactly one IRQ_DEFAULT and one IRQ_RESET, both from the same CRT
  // object with asset profile 1 (asset value itself is checked at load).
  InputFile *DefaultFile = nullptr;
  InputFile *ResetFile = nullptr;
  for (auto &F : Files) {
    for (const IsrRecord &R : F->IsrRecords) {
      if (R.Kind == MCS251ISR::RK_IRQ_DEFAULT) {
        if (DefaultSym)
          return fail(Err, F->Path + ": MCS251 ISR: more than one IRQ_DEFAULT "
                             "record");
        DefaultSym = R.Ref;
        DefaultFile = F.get();
      } else if (R.Kind == MCS251ISR::RK_IRQ_RESET) {
        if (ResetSym)
          return fail(Err, F->Path + ": MCS251 ISR: more than one IRQ_RESET "
                             "record");
        ResetSym = R.Ref;
        ResetFile = F.get();
      }
    }
  }
  if (!DefaultSym || !ResetSym)
    return fail(Err, "MCS251 ISR: IRQ mode requires exactly one IRQ_DEFAULT "
                     "and one IRQ_RESET from the frozen CRT");
  if (DefaultFile != ResetFile)
    return fail(Err, "MCS251 ISR: IRQ_DEFAULT and IRQ_RESET must come from "
                     "the same CRT object");
  CrtFile = DefaultFile;
  return true;
}

// T07 step 6: synthesize the only vector table as real input-section objects
// that participate in layout, overlap and the ROM gate like any other CODE.
// Legal slot: 4B PROGBITS (EJMP opcode + R_MCS251_24 field) + 4B NOBITS tail.
// Reserved/system slots: one 8B NOBITS range, no EJMP. The synthesized EJMP
// relocations use the ordinary R_MCS251_24 against the vetted entry; type9 is
// never copied into synthesized machine code.
bool Linker::synthesizeIRQVectors() {
  for (unsigned Slot = 0; Slot < MCS251ISR::ISRVectorCount; ++Slot) {
    const uint32_t Base =
        MCS251ISR::ISRVectorBase + MCS251ISR::ISRVectorStride * Slot;
    auto New = [&](const std::string &Name, uint32_t Type, uint64_t Flags,
                   uint64_t Size, uint32_t Expected) {
      auto S = std::make_unique<InputSection>();
      S->Name = Name;
      S->Type = Type;
      S->Flags = Flags;
      S->Size = Size;
      S->Align = 1;
      S->Region = "VECS";
      S->IsAlloc = true;
      S->IsCode = (Flags & ELF::SHF_EXECINSTR) != 0;
      S->IsNobits = Type == ELF::SHT_NOBITS;
      S->IsLoadable = S->IsAlloc && !S->IsNobits && S->Size != 0;
      SynthExpect.push_back({S.get(), Expected});
      SynthSections.push_back(S.get());
      AllSections.push_back(S.get());
      OwnedSynth.push_back(std::move(S));
      return OwnedSynth.back().get();
    };
    std::string NN = Slot < 10 ? "0" + std::to_string(Slot)
                               : std::to_string(Slot);
    if (MCS251ISR::isLegalISRSlot(Slot)) {
      InputSection *J =
          New(".mcs251.VECS." + NN, ELF::SHT_PROGBITS,
              ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 4, Base);
      J->Data = {EJMP_OPCODE, 0, 0, 0};
      J->Relocs.push_back(
          {1, ELF::R_MCS251_24, 0, 0});
      VectorJumps.push_back({J, SlotSym[Slot] ? SlotSym[Slot] : DefaultSym});
      New(".mcs251.VECS." + NN + ".pad", ELF::SHT_NOBITS,
          ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 4, Base + 4);
    } else {
      New(".mcs251.VECS." + NN + ".rsv", ELF::SHT_NOBITS,
          ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 8, Base);
    }
  }
  return true;
}

bool Linker::reserve(uint32_t Start, uint32_t Size, StringRef What) {
  if (!Size)
    return true;
  if (!rangeFits(Start, Size, 0x10000)) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    OS << What << ": address range is out of bounds (["
       << format_hex(Start, 6, false) << ","
       << format_hex(uint64_t(Start) + Size, 6, false)
       << ") does not fit the 16-bit DATA space)";
    return fail(Err, Msg);
  }
  Range R{Start, Start + Size};
  for (const DataUse &U : DataUsed)
    if (R.Start < U.R.End && U.R.Start < R.End) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "internal DATA overlap for " << What << ": new range ["
         << format_hex(R.Start, 6, false) << ","
         << format_hex(R.End, 6, false) << ") collides with ["
         << format_hex(U.R.Start, 6, false) << ","
         << format_hex(U.R.End, 6, false) << ") from " << U.What;
      return fail(Err, Msg);
    }
  DataUsed.push_back({R, What.str()});
  StackH = std::max(StackH, R.End);
  return true;
}

// E3: report why no first-fit slot exists, with every field a layout fix
// needs: the failing object/section and its symbols, the request size and
// alignment, the allocation window, the largest free hole, and every occupied
// range inside the window with its conflict source.  The window itself is
// policy (direct-addressing semantics) and is never changed here.
bool Linker::allocate(InputSection &S, uint32_t Lo, uint32_t Hi) {
  if (!S.Size)
    return true;
  const uint32_t WinEnd = Hi + 1; // Exclusive; windows are <= 0x100 bytes.
  for (uint32_t A = Lo; A <= Hi && S.Size <= Hi - A + 1; ++A) {
    if (S.Align > 1)
      A = (A + S.Align - 1) & ~(S.Align - 1);
    if (A > Hi || S.Size > Hi - A + 1)
      break;
    Range R{A, A + static_cast<uint32_t>(S.Size)};
    bool Good = true;
    for (const DataUse &U : DataUsed)
      if (R.Start < U.R.End && U.R.Start < R.End) {
        Good = false;
        break;
      }
    if (Good) {
      S.Address = A;
      return reserve(A, S.Size, S.Name);
    }
  }

  // Allocation failed: build the actionable diagnostic.
  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "cannot allocate " << S.Name;
  if (S.File && !S.File->Path.empty())
    OS << " (from " << S.File->Path << ")";
  OS << ": size " << S.Size << " bytes, align " << S.Align;
  {
    // Named definitions carried by this section (up to four are listed).
    std::vector<StringRef> Names;
    unsigned Total = 0;
    for (const auto &F : Files)
      for (const InputSymbol &Sym : F->Symbols)
        if (Sym.Defined && Sym.Sec == &S && !Sym.Name.empty()) {
          if (Names.size() < 4)
            Names.push_back(Sym.Name);
          ++Total;
        }
    if (Total) {
      OS << ", symbols";
      for (StringRef N : Names)
        OS << ' ' << N;
      if (Total > Names.size())
        OS << " (+" << (Total - Names.size()) << " more)";
    }
  }
  OS << "; window [" << format_hex(Lo, 6, false) << ","
     << format_hex(WinEnd, 6, false) << ")";

  // Occupied ranges clipped to the window, sorted and merged, so the free
  // holes and their owners can be reported exactly.
  std::vector<std::pair<Range, StringRef>> Occ;
  for (const DataUse &U : DataUsed) {
    const uint32_t B = std::max(U.R.Start, Lo);
    const uint32_t E = std::min(U.R.End, WinEnd);
    if (B < E)
      Occ.push_back({{B, E}, U.What});
  }
  llvm::sort(Occ, [](const auto &A, const auto &B) {
    return A.first.Start < B.first.Start;
  });
  uint32_t Largest = 0;      // Largest free run, any alignment.
  uint32_t LargestAligned = 0; // Largest run meeting S.Align.
  {
    uint32_t Cursor = Lo;
    for (const auto &P : Occ) {
      if (Cursor < P.first.Start) {
        Largest = std::max(Largest, P.first.Start - Cursor);
        const uint32_t AlignedStart =
            (Cursor + S.Align - 1) & ~(S.Align - 1);
        if (AlignedStart < P.first.Start)
          LargestAligned =
              std::max(LargestAligned, P.first.Start - AlignedStart);
      }
      Cursor = std::max(Cursor, P.first.End);
    }
    if (Cursor < WinEnd) {
      Largest = std::max(Largest, WinEnd - Cursor);
      const uint32_t AlignedStart = (Cursor + S.Align - 1) & ~(S.Align - 1);
      if (AlignedStart < WinEnd)
        LargestAligned = std::max(LargestAligned, WinEnd - AlignedStart);
    }
  }
  if (Largest == 0)
    OS << ": every byte is occupied";
  else if (S.Align > 1)
    OS << ": no free range of " << S.Size << " bytes at align " << S.Align
       << "; largest aligned free range is " << LargestAligned
       << " bytes (largest unaligned " << Largest << " bytes)";
  else
    OS << ": no free range of " << S.Size
       << " bytes; largest free range is " << Largest << " bytes";
  if (!Occ.empty()) {
    OS << "; occupied:";
    for (size_t I = 0; I != Occ.size() && I != 6; ++I) {
      if (I)
        OS << ',';
      OS << " [" << format_hex(Occ[I].first.Start, 6, false) << ","
         << format_hex(Occ[I].first.End, 6, false) << ") from "
         << Occ[I].second;
    }
    if (Occ.size() > 6)
      OS << " (+" << (Occ.size() - 6) << " more)";
  }
  return fail(Err, Msg);
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
    return fail(Err, "invalid DSEG allocation window: start 0x" +
                         Twine::utohexstr(DsegStart) + ", end 0x" +
                         Twine::utohexstr(DsegEnd));
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
    return fail(Err, "invalid ISEG allocation window: start 0x" +
                         Twine::utohexstr(IsegStart) + ", end 0x" +
                         Twine::utohexstr(IsegEnd));
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
            for (const DataUse &U : DataUsed)
              if (U.R.Start <= End && End < U.R.End) {
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
        for (const DataUse &U : DataUsed)
          if (R.Start < U.R.End && U.R.Start < R.End) {
            Good = false;
            break;
          }
        if (Good) {
          SsegBase = A;
          Found = true;
          break;
        }
      }
      if (!Found) {
        // E3: keep the asserted message prefix, add the window and the hole
        // facts so the stack placement can be fixed from the message alone.
        uint32_t Largest = 0;
        {
          uint32_t Cursor = IsegStart;
          std::vector<std::pair<uint32_t, uint32_t>> Occ;
          for (const DataUse &U : DataUsed) {
            const uint32_t B = std::max(U.R.Start, IsegStart);
            const uint32_t E = std::min(U.R.End, IsegEnd);
            if (B < E)
              Occ.push_back({B, E});
          }
          llvm::sort(Occ);
          for (const auto &P : Occ) {
            if (Cursor < P.first)
              Largest = std::max(Largest, P.first - Cursor);
            Cursor = std::max(Cursor, P.second);
          }
          if (Cursor < IsegEnd)
            Largest = std::max(Largest, IsegEnd - Cursor);
        }
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "cannot allocate SSEG (stack) in ISEG window: need "
           << ReserveSize << " bytes in ["
           << format_hex(IsegStart, 6, false) << ","
           << format_hex(IsegEnd, 6, false) << "), largest free range is "
           << Largest << " bytes";
        if (!Config.StackSize)
          OS << " (no --stack-size given; the largest hole sizes the stack)";
        return fail(Err, Msg);
      }
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
    for (const DataUse &R : DataUsed)
      if (R.R.Start <= A && A < R.R.End) {
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

// A3.6 layout stage: reserved vector range and CRT shape validation. Runs
// after layout so every synthesized slot has its final address, and before
// checkFlashGate so a rejected image never produces output.
bool Linker::validateIRQReservedRangesAndCRT() {
  // T07 step 8: the vector area is pinned to the frozen formula base; it
  // cannot be moved through --area-start=VECS.
  if (hasAreaStart("VECS") && areaStart("VECS", 0) != MCS251ISR::ISRVectorBase)
    return fail(Err, "MCS251 ISR: --area-start=VECS must be 0xff0003; the "
                     "vector area cannot be moved");
  // The synthesized slots must have landed exactly on the frozen formula.
  for (const auto &P : SynthExpect) {
    if (P.first->Address != P.second)
      return fail(Err, "MCS251 ISR: synthesized vector " + P.first->Name +
                           " did not land on the frozen formula address 0x" +
                           Twine::utohexstr(P.second));
  }
  // T07 step 9: check the whole reserved range against every non-synthetic
  // CODE range, independent of any image bytes.
  for (const Range &U : CodeUsed) {
    bool Owned = llvm::any_of(SynthSections, [&](const InputSection *S) {
      return S->Address == U.Start && S->Address + S->Size == U.End;
    });
    if (!Owned && U.Start < MCS251ISR::ISRVectorEnd &&
        MCS251ISR::ISRVectorBase < U.End)
      return fail(Err, "MCS251 ISR: CODE range [0x" +
                           Twine::utohexstr(U.Start) + ",0x" +
                           Twine::utohexstr(U.End) +
                           ") overlaps the reserved vector area "
                           "[0xff0003,0xff01a3)");
  }
  // T07 step 10 / R2: HOME must sit exactly at 0xff0000 so the reset abuts
  // the vector base, and must be exactly a 3-byte ljmp, never a 4-byte EJMP.
  InputSection *Home = ResetSym->Sec;
  if (!Home || Home->Region != "HOME" || Home->Size != 3 || ResetSym->Size != 3)
    return fail(Err, "MCS251 ISR: reset must be a 3-byte HOME trampoline, "
                     "never a 4-byte EJMP");
  if (Home->Address != 0xFF0000)
    return fail(Err, "MCS251 ISR: HOME must sit at 0xff0000 abutting the "
                     "vector base, got 0x" +
                         Twine::utohexstr(Home->Address));
  if (Home->Data.size() != 3 || Home->Data[0] != 0x02)
    return fail(Err, "MCS251 ISR: HOME must begin with the ljmp opcode 0x02");
  if (Home->Relocs.size() != 1 || Home->Relocs[0].Type != ELF::R_MCS251_J16 ||
      Home->Relocs[0].Offset != 1)
    return fail(Err, "MCS251 ISR: HOME must carry exactly one J16 field at "
                     "offset 1 into the paired BOOT");
  {
    const Relocation &R = Home->Relocs[0];
    // R2: verify the final landing address (symbol address + addend), not
    // just the symbol's section. The J16 must hit the paired BOOT entry
    // itself: zero addend, entry at the start of its BOOT section.
    if (R.Addend != 0)
      return fail(Err, "MCS251 ISR: reset J16 must carry a zero addend, got " +
                           Twine(R.Addend));
    InputSymbol *IS = findSymbol(*CrtFile, R.Sym);
    if (!IS)
      return fail(Err, "invalid relocation symbol");
    InputSymbol *Def =
        IS->Defined ? IS
                    : (Globals.count(IS->Name) ? Globals[IS->Name] : nullptr);
    if (!Def || !Def->Defined || !Def->Sec || Def->File != CrtFile ||
        Def->Sec->Region != "BOOT")
      return fail(Err, "MCS251 ISR: reset J16 must target the paired BOOT of "
                       "the same CRT object");
    if (Def->Address + static_cast<uint32_t>(R.Addend) != Def->Sec->Address)
      return fail(Err, "MCS251 ISR: reset J16 must land on the paired BOOT "
                       "entry itself, not at 0x" +
                           Twine::utohexstr(Def->Address +
                                            static_cast<uint32_t>(R.Addend)));
  }
  // T07 step 11: the CRT BOOT must start at or above 0xff0210 (the T08 asset
  // default); FF0100-era BOOTs are a legacy-layout conflict in IRQ mode.
  for (InputSection *S : AllSections)
    if (S->File == CrtFile && S->Region == "BOOT" && S->Size &&
        S->Address < 0xFF0210)
      return fail(Err, "MCS251 ISR: CRT BOOT must start at or above 0xff0210, "
                       "got 0x" +
                           Twine::utohexstr(S->Address));
  return true;
}

// E3/M4: Code ROM gate.  The build layer supplies the on-board flash window
// as two plain numbers (--flash-base/--flash-size); the linker never knows a
// board model.  Every occupied CODE-class section (HOME/VECS/BOOT/CSEG/XINIT)
// must fit entirely inside [FlashBase, FlashBase+FlashSize); holes between
// sections are a design feature of the sparse layout and are not checked.
// XSEG is XDATA NOBITS in a separate address space (SPEC §4.1: XDATA
// reservations generate no ROM load bytes) and is exempt from this gate.
bool Linker::checkFlashGate() {
  if (!Config.FlashGate)
    return true;
  if (Config.FlashSize == 0 ||
      uint64_t(Config.FlashBase) + Config.FlashSize > 0x1000000)
    return fail(Err, "invalid flash window: need 0 < size and "
                     "base+size <= 0x1000000");
  const uint64_t Lo = Config.FlashBase;
  const uint64_t Hi = Lo + Config.FlashSize; // One past the last flash byte.
  auto Hex = [](uint64_t V) { return "0x" + Twine::utohexstr(V); };
  auto IsCodeArea = [](StringRef N) {
    return N == "HOME" || N == "VECS" || N == "BOOT" || N == "CSEG" ||
           N == "XINIT";
  };
  // A configured CODE-class area start must itself sit inside the window,
  // even when the area turns out to hold no bytes at all.
  for (const auto &P : Config.AreaStarts)
    if (IsCodeArea(StringRef(P.first)) && (P.second < Lo || P.second >= Hi))
      return fail(Err, "--area-start=" + P.first + "=" + Hex(P.second) +
                           " is outside the flash window " + Hex(Lo) + ".." +
                           Hex(Hi) + " (--flash-base/--flash-size)");
  for (const InputSection *S : AllSections) {
    if (!IsCodeArea(StringRef(S->Region)) || S->Size == 0)
      continue;
    const uint64_t Start = S->Address;
    const uint64_t End = Start + S->Size;
    if (Start < Lo || End > Hi)
      return fail(Err, "CODE ROM overflow: " + S->Name + " (" + S->Region +
                           ") occupies " + Hex(Start) + ".." + Hex(End) +
                           " which exceeds the flash window " + Hex(Lo) +
                           ".." + Hex(Hi) + " (--flash-base/--flash-size)");
  }
  return true;
}

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
        // T07 step 15 / R3: an ordinary ALLOC relocation may never use a
        // known registered ISR or the default entry as a plain address/call
        // target. The synthesized vector jumps are the single sanctioned
        // exception and are applied separately below.
        if (IrqMode) {
          const bool IsDefault = Target == DefaultSym;
          if (Target->Defined &&
              (IsrSymbols.count(Target) || IsDefault))
            return fail(Err, IsDefault
                                 ? "MCS251 ISR: ordinary relocation in " +
                                       S->Name + " uses the default entry " +
                                       Target->Name + " as a plain address"
                                 : "MCS251 ISR: ordinary relocation in " +
                                       S->Name + " uses registered ISR " +
                                       Target->Name + " as a plain address");
          if (IS->Type == ELF::STT_SECTION) {
            // T07 step 15, final sentence: a section+addend candidate is
            // cross-checked against the exact entry addresses of the known
            // ISRs and the default entry. Only an exact-entry hit violates
            // the rule; a branch from inside an ISR to its own body (entry
            // + nonzero offset) is intra-function control flow the rule
            // must not reject (the entry block of a function has no
            // predecessors, so a compiler never emits addend 0 here).
            const int64_t Cand =
                static_cast<int64_t>(IS->Address) + R.Addend;
            auto AtEntry = [&](InputSymbol *P) {
              return Cand == static_cast<int64_t>(P->Address);
            };
            for (InputSymbol *ISR : IsrSymbols)
              if (AtEntry(ISR))
                return fail(Err, "MCS251 ISR: section+addend relocation in " +
                                     S->Name + " lands inside registered ISR " +
                                     ISR->Name);
            if (DefaultSym && AtEntry(DefaultSym))
              return fail(Err, "MCS251 ISR: section+addend relocation in " +
                                   S->Name + " lands inside the default entry");
          }
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
  // T07 step 6: the synthesized EJMP table is applied after layout, when the
  // vetted targets have final addresses. Metadata type9 relocations are never
  // seen here: they are consumed during loadFile() (T07 step 14).
  if (IrqMode && !applyVectorJumps())
    return false;
  if (IrqMode && !validateIRQFinalAssets())
    return false;
  return true;
}

// Apply the 52 synthesized vector slots: R_MCS251_24 semantics against the
// exact vetted entry symbol of the slot (registered handler or the CRT
// default fail-stop entry for unregistered legal slots).
bool Linker::applyVectorJumps() {
  for (const auto &VJ : VectorJumps) {
    InputSection *S = VJ.first;
    const Relocation &R = S->Relocs[0];
    InputSymbol *T = VJ.second;
    if (!T || !T->Defined)
      return fail(Err, "MCS251 ISR: vector target for " + S->Name +
                           " is not resolved");
    const int64_t Value = static_cast<int64_t>(T->Address) + R.Addend;
    if (Value < 0 || Value > 0xffffff)
      return fail(Err, "MCS251 ISR: vector target out of 24-bit range for " +
                           S->Name);
    if (!T->Sec || !T->Sec->IsCode)
      return fail(Err, "MCS251 ISR: vector target for " + S->Name +
                           " is not a CODE address");
    S->Data[R.Offset] = (Value >> 16) & 0xff;
    S->Data[R.Offset + 1] = (Value >> 8) & 0xff;
    S->Data[R.Offset + 2] = Value & 0xff;
    Image[S->Address + R.Offset] = S->Data[R.Offset];
    Image[S->Address + R.Offset + 1] = S->Data[R.Offset + 1];
    Image[S->Address + R.Offset + 2] = S->Data[R.Offset + 2];
  }
  return true;
}

// A3.6/applyRelocations stage: final IRQ target and asset validation that
// needs post-layout addresses.
bool Linker::validateIRQFinalAssets() {
  // A5: no relocation of any width may exist inside the default entry range.
  // Zero-width relocations count too: the frozen rule is about the range,
  // not about written bytes, so a zero-width record inside [Value, Value+4)
  // is rejected by its offset (R3).
  {
    const int64_t Lo = static_cast<int64_t>(DefaultSym->Value);
    const int64_t Hi = Lo + 4;
    for (const Relocation &R : DefaultSym->Sec->Relocs) {
      const int64_t RL = static_cast<int64_t>(R.Offset);
      const uint32_t W = relocWidth(R.Type);
      const bool Inside = W == 0 ? (RL >= Lo && RL < Hi)
                                 : (RL < Hi && Lo < RL + static_cast<int64_t>(W));
      if (Inside)
        return fail(Err, "MCS251 ISR: relocation inside the default entry "
                         "range");
    }
  }
  // A5: the default entry is byte-exactly the frozen fail-stop word.
  if (DefaultSym->Size != 4)
    return fail(Err, "MCS251 ISR: default entry must be a 4-byte STT_FUNC");
  InputSection *S = DefaultSym->Sec;
  if (!S || DefaultSym->Value > S->Data.size() ||
      4 > S->Data.size() - DefaultSym->Value)
    return fail(Err, "MCS251 ISR: default entry range is out of its section");
  for (unsigned I = 0; I != 4; ++I)
    if (S->Data[DefaultSym->Value + I] != MCS251ISR::IRQDefaultBytes[I])
      return fail(Err, "MCS251 ISR: default entry machine bytes are not the "
                       "frozen C2AF80FE fail-stop word");
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
        // T08/T07 step 20: explicit initial values may also target an
        // allocated BSEG_BYTES slice (the CRT-owned bit-addressable byte
        // window). No new bit allocator and no v2 capability is introduced:
        // only slices that were actually allocated by the existing BSEG_BYTES
        // rule (allocated slices carry a real Address) qualify.
        if ((D->Region == "DSEG" || D->Region == "DATA_ABS" ||
             D->Region == "BSEG_BYTES") &&
            D->Size != 0 && Destination >= D->Address &&
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
  // E5: function-level rows (address + size straight from the input symbol
  // tables, final layout addresses).  Gated behind KeepSymbols because the
  // frozen release artifacts hash the exact map bytes; without the flag the
  // map is byte-identical to the pre-E5 format.
  if (Config.KeepSymbols) {
    std::vector<OutputSymbol> Funcs;
    collectSymbols(Funcs);
    for (const OutputSymbol &Sym : Funcs)
      if (Sym.Type == ELF::STT_FUNC && Sym.Size)
        Out << "FUNC " << format_hex(Sym.Address, 6, false) << " +"
            << format_hex(Sym.Size, 0, false) << " " << Sym.Name << '\n';
  }
  if (StackRequested)
    Out << "stack H=" << format_hex(StackH, 4, false) << " SPX="
        << format_hex(SPX, 4, false) << " capacity=" << Capacity
        << " edata_end=" << format_hex(Config.EdataEnd, 4, false) << '\n';
  // T07 steps 17-19: in IRQ mode the map carries exactly the 52 synthesized
  // vector rows (slot as two decimal digits, address as 6 lowercase hex
  // digits). No ISR-safe, stack or priority fields exist anywhere.
  if (IrqMode) {
    for (unsigned Slot = 0; Slot < MCS251ISR::ISRVectorCount; ++Slot) {
      const uint32_t Base =
          MCS251ISR::ISRVectorBase + MCS251ISR::ISRVectorStride * Slot;
      Out << "IRQ ";
      if (Slot < 10)
        Out << '0';
      Out << Slot << " " << format_hex(Base, 8, false);
      switch (MCS251ISR::ISRSlots[Slot].Kind) {
      case MCS251ISR::ISRSlotKind::Reserved:
        Out << " RESERVED\n";
        break;
      case MCS251ISR::ISRSlotKind::System:
        Out << " SYSTEM\n";
        break;
      default:
        if (InputSymbol *Sym = SlotSym[Slot])
          Out << " ISR " << Sym->Name << '\n';
        else
          Out << " DEFAULT " << DefaultSym->Name << '\n';
        break;
      }
    }
  }
}

// E5: snapshot every named definition with its final layout address.  Input
// symbols come first (locals, then globals, in input order), followed by the
// synthesised boundary/stack symbols.  STT_SECTION and anonymous symbols
// carry no audit value and are skipped; duplicate names across objects are
// kept because each names a distinct definition in its own object.
void Linker::collectSymbols(std::vector<OutputSymbol> &Out) const {
  auto Emit = [&](uint8_t Bind) {
    for (const auto &F : Files)
      for (const InputSymbol &S : F->Symbols)
        if (S.Defined && !S.Name.empty() && S.Type != ELF::STT_SECTION &&
            S.Bind == Bind)
          Out.push_back({S.Name, S.Address, S.Size, S.Bind, S.Type, false});
  };
  Emit(ELF::STB_LOCAL);
  Emit(ELF::STB_GLOBAL);
  for (const auto &P : Synth)
    Out.push_back({P.first, P.second, 0, ELF::STB_GLOBAL, ELF::STT_NOTYPE,
                   true});
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
  // A3.6: any input containing `.mcs251.isr` triggers IRQ mode; there is no
  // command-line profile string and no board-name policy.
  IrqMode = llvm::any_of(Files,
                         [](const std::unique_ptr<InputFile> &F) {
                           return F->HasIsrMeta;
                         });
  if (IrqMode && !rejectInputVecs())
    return false;
  // T07 step 8: the vector area sits at the frozen base whether or not the
  // command line passes --area-start=VECS; a non-FF0003 value is rejected in
  // validateIRQReservedRangesAndCRT().
  if (IrqMode && !hasAreaStart("VECS"))
    Config.AreaStarts.emplace_back("VECS", MCS251ISR::ISRVectorBase);
  raw_string_ostream InputOS(Result.InputReport);
  printInputs(InputOS);
  InputOS.flush();
  if (Config.PrintInput)
    return true;
  if (!resolveSymbols())
    return false;
  if (IrqMode && (!validateISRIdentitiesAndRegistrations() ||
                  !synthesizeIRQVectors()))
    return false;
  if (!layout())
    return false;
  if (IrqMode && !validateIRQReservedRangesAndCRT())
    return false;
  // The ROM gate runs after layout and before any output is produced, so a
  // rejected image never reaches a firmware file or a map.
  if (!checkFlashGate())
    return false;
  if (!errorUndefined() || !applyRelocations() || !validateXInit())
    return false;
  Result.Entry = llvm::any_of(AllSections,
                              [](const InputSection *S) { return S->Region == "HOME"; })
                     ? areaStart("HOME", 0) : 0;
  // E5: final symbol snapshot with post-layout addresses; the flavor shell
  // decides whether to serialize it into the output ELF.
  collectSymbols(Result.Symbols);
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
