//===-- MCS251RELObjectWriter.cpp - ASxxxx REL object writer -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Serializes the MCAssembler state as an ASxxxx ASCII .rel object
// (Phase 13a, de-SDCC Step 2): after this, `llc -filetype=obj` feeds sdld
// directly and sdas251 leaves the production path entirely.
//
// The emitted record layout mirrors sdas251's asout.c byte-for-byte where
// that matters to sdld, validated against the Phase-13a gold experiments
// (/tmp/mcs251-p13a, especially reloc-consumer.rel):
//
//   XH3                     hex radix, MSB-first, 24-bit addresses
//   H <areas> areas <n> global symbols     counts in minimal-width %X hex
//   M <module>            module name (same sanitization as the asm path)
//   O <signature>         the locked SDCC ABI signature (MCS251ABISignature.h)
//   S <name> Ref%06X / S <name> Def%06X    undefined/defined globals; the
//                                          record order assigns the R-record
//                                          reference indices
//   A <name> size %X flags %X addr %X      _CODE/CSEG plus target sections:
//                                          DSEG reservations (flags 0) and
//                                          XINIT ROM data (flags 0x20)
//   T <addr24> <bytes...> area-relative payload, max 16 bytes per line, never
//                         splitting a relocation field across lines (sdas's
//                         outchk NTXT/NREL rule)
//   R 00 00 <area16> <mode> <t-index> <ref16> ...
//
// Relocation semantics (asout.c outrw/outr3b, relocation-map.tsv):
//
//   * 16-bit fields (fixup_mcs251_16): mode 0x002 with a symbol reference,
//     mode 0x000 with the area reference.  24-bit fields (fixup_mcs251_24):
//     mode 0x082 / 0x080 likewise (R_WORD|R_C24, |R_SYM).
//   * A fixup whose target symbol is defined in this module is area-relative:
//     the T payload carries the symbol's area offset plus the addend and the
//     R record points at the target CSEG/OSEG/DSEG area. Only UNDEFINED globals
//     use the symbol's S-record index (mode | 0x02).  This matches sdas251
//     exactly (measured: `ecall _f` to a defined global emits mode 0x80 with
//     ref 0001, not a symbol reference).
//   * The t-index counts bytes in the T line INCLUDING the three XH3 address
//     bytes, so the first payload byte is index 3.
//   * Mode values above 0xff are escaped as two bytes 0xF0|hi, lo (asout.c
//     write_rmode); the two modes used here (0x00/0x02, 0x80/0x82) never
//     escape.
//
// Scope limits are loud, not silent: more than one non-empty CSEG section,
// unknown allocated sections, symbol-difference fixups and absolute symbols
// are fatal errors. Phase 11
// also supports byte-of24 lo/mid/hi relocations, whose 3-byte T placeholders
// shrink to one code byte at link time (see the payload builder below).
//
//===----------------------------------------------------------------------===//

#include "MCS251RELObjectWriter.h"
#include "MCS251ABISignature.h"
#include "MCS251FixupKinds.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

namespace {

static void hex(raw_ostream &OS, uint64_t V, unsigned Digits) {
  static constexpr char DigitsTable[] = "0123456789ABCDEF";
  for (unsigned I = Digits; I != 0; --I)
    OS << DigitsTable[(V >> ((I - 1) * 4)) & 0xf];
}

// Minimal-width uppercase hex: the %X format sdas uses for the H/A record
// fields (asout.c outgsd/outarea).  Fixed-width fields would also parse in
// sdld, but record-level comparison against sdas251 output is part of the
// validation flow, so match the reference byte-for-byte.
static void hexMin(raw_ostream &OS, uint64_t V) {
  if (V == 0) {
    OS << '0';
    return;
  }
  unsigned Digits = (63 - llvm::countl_zero(V)) / 4 + 1;
  hex(OS, V, Digits);
}

static void byte(raw_ostream &OS, unsigned V) {
  OS << ' ';
  hex(OS, V & 0xff, 2);
}
static void word(raw_ostream &OS, uint64_t V) {
  byte(OS, unsigned(V >> 8));
  byte(OS, unsigned(V));
}
static void addr24(raw_ostream &OS, uint64_t V) {
  byte(OS, unsigned(V >> 16));
  byte(OS, unsigned(V >> 8));
  byte(OS, unsigned(V));
}

struct Relocation {
  const MCFragment *Fragment = nullptr;
  MCFixup Fixup;
  MCValue Target;
  uint64_t Value = 0;
};

struct SectionData {
  const MCSection *Section = nullptr;
  uint64_t Base = 0;
  uint64_t Size = 0;
  unsigned AreaIndex = 1;
  std::string AreaName = "CSEG";
  unsigned Flags = 0x20;
  bool NoLoad = false;
  std::vector<uint8_t> Bytes;
};

// sdas never puts more than NTXT(=16) byte values in one T line, and that
// count INCLUDES the three XH3 address bytes (asout.c outtxt/outchk), so the
// payload limit is 13.  sdld's relocation/listing arrays (rtval/rtflg/rterr
// in lkdata.c) are sized NTXT as well: a longer T line overruns them and
// hangs the -r listing pass.  13 it is.
static constexpr uint64_t MaxTPayload = 13;

class MCS251RELObjectWriter final : public MCObjectWriter {
  raw_pwrite_stream &OS;
  std::vector<Relocation> Relocations;

  // Same rule as the assembly path's getMCS251ModuleName (the AsmPrinter
  // bridges the sanitized name through MCContext::setMainFileName, so the
  // M record matches the .module line of -filetype=asm output).
  static std::string moduleName(StringRef FileName) {
    StringRef Stem = sys::path::stem(FileName);
    std::string Name = Stem.empty() ? "mcs251_module" : Stem.str();
    for (char &C : Name)
      if (!(('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z') ||
            ('0' <= C && C <= '9') || C == '_'))
        C = '_';
    if (!Name.empty() && '0' <= Name.front() && Name.front() <= '9')
      Name.insert(Name.begin(), '_');
    return Name;
  }

  static bool isGlobal(const MCSymbol &S) {
    if (S.isUndefined())
      return !S.isTemporary() && !S.getName().empty();
    auto *E = static_cast<const MCSymbolELF *>(&S);
    unsigned Binding = E->getBinding();
    return !S.isTemporary() && !S.getName().empty() &&
           (Binding == ELF::STB_GLOBAL || Binding == ELF::STB_WEAK);
  }

  static uint64_t symbolOffset(const MCAssembler &A, const MCSymbol &S) {
    if (!S.isInSection())
      return 0;
    return A.getFragmentOffset(*S.getFragment()) + S.getOffset();
  }

  static const SectionData *findSection(const std::vector<SectionData> &Secs,
                                        const MCSection *Section) {
    for (const SectionData &S : Secs)
      if (S.Section == Section)
        return &S;
    return nullptr;
  }

  static unsigned relocationWidth(MCFixupKind Kind) {
    switch (Kind) {
    case FK_Data_2:
    case MCS251::fixup_mcs251_16:
      return 2;
    case MCS251::fixup_mcs251_24:
      return 3;
    default:
      return 1;
    }
  }

  // asout.c write_rmode: modes above one byte are escaped as 0xF0|hi, lo.
  void emitRMode(uint64_t Mode) {
    if (Mode > 0xff) {
      byte(OS, 0xf0 | unsigned(Mode >> 8));
      byte(OS, unsigned(Mode));
    } else {
      byte(OS, unsigned(Mode));
    }
  }

  void emitRLine(const SectionData &Sec, uint64_t ChunkStart,
                 uint64_t ChunkSize, unsigned AreaIndex,
                 const std::map<const MCSymbol *, unsigned> &SymbolRefs,
                 const std::map<uint64_t, unsigned> &TIndices,
                 const std::vector<SectionData> &Sections) {
    OS << "R";
    byte(OS, 0);
    byte(OS, 0);
    word(OS, AreaIndex);
    for (const Relocation &Rel : Relocations) {
      if (!Rel.Fragment || Rel.Fragment->getParent() != Sec.Section)
        continue;
      uint64_t Offset = Asm->getFragmentOffset(*Rel.Fragment) +
                        Rel.Fixup.getOffset();
      if (Offset < ChunkStart || Offset >= ChunkStart + ChunkSize)
        continue;

      uint64_t Mode;
      unsigned Ref;
      if (Rel.Fixup.getKind() == FK_Data_2 ||
          Rel.Fixup.getKind() == MCS251::fixup_mcs251_16)
        Mode = 0x00;
      else if (Rel.Fixup.getKind() == MCS251::fixup_mcs251_24)
        Mode = 0x80;
      else if (Rel.Fixup.getKind() == MCS251::fixup_mcs251_lo8)
        Mode = 0x101;
      else if (Rel.Fixup.getKind() == MCS251::fixup_mcs251_mid8)
        Mode = 0x181;
      else if (Rel.Fixup.getKind() == MCS251::fixup_mcs251_hi8)
        Mode = 0x381;
      else
        report_fatal_error("MCS251 REL writer: unsupported relocation kind");

      const MCSymbol *Target = Rel.Target.getAddSym();
      // A symbol that is defined in this module is represented by its area
      // plus the initial symbol offset in T. Undefined globals are the only
      // references which must use the S record and make sdld add the final
      // symbol value. This mirrors asout.c's e_flag/e_base.e_ap split.
      if (Target && Target->isUndefined() && isGlobal(*Target)) {
        Mode |= 0x02;
        auto It = SymbolRefs.find(Target);
        if (It == SymbolRefs.end())
          report_fatal_error("MCS251 REL writer: missing symbol reference");
        Ref = It->second;
      } else {
        const SectionData *TargetSec = Target && Target->isInSection()
            ? findSection(Sections, &Target->getSection()) : nullptr;
        if (!TargetSec)
          report_fatal_error("MCS251 REL writer: missing relocation section");
        Ref = TargetSec->AreaIndex;
      }
      emitRMode(Mode);
      // ASxxxx's R index is into the parsed T array, including XH3's three
      // address bytes. The first payload byte is therefore index 3.
      byte(OS, TIndices.at(Offset));
      word(OS, Ref);
    }
    OS << '\n';
  }

public:
  explicit MCS251RELObjectWriter(raw_pwrite_stream &OS) : OS(OS) {}

  void reset() override {
    MCObjectWriter::reset();
    Relocations.clear();
  }

  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override {
    if (Fixup.getKind() != FK_Data_2 &&
        (Fixup.getKind() < MCS251::fixup_mcs251_16 ||
         Fixup.getKind() >= MCS251::NumTargetFixupKinds))
      report_fatal_error("MCS251 REL writer: unsupported relocation kind "
                         "(expected a word/address or byte-of24 ASxxxx relocation)");
    if (Target.getSubSym())
      report_fatal_error("MCS251 REL writer: symbol-difference fixups are "
                         "not supported");
    if (const MCSymbol *Add = Target.getAddSym())
      if (Add->isAbsolute() && !Add->isUndefined())
        report_fatal_error("MCS251 REL writer: absolute-symbol fixups are "
                           "not supported");
    Relocations.push_back({&F, Fixup, Target, FixedValue});
  }

  uint64_t writeObject() override {
    if (!Asm)
      report_fatal_error("MCS251 REL writer has no assembler");

    std::vector<SectionData> Sections;
    uint64_t TotalSize = 0;
    bool HasSlots = false;
    for (const MCSection &Section : *Asm) {
      StringRef Name = Section.getName();
      bool Overlay = Name.starts_with(".mcs251.OSEG.");
      bool StaticSlot = Name.starts_with(".mcs251.DSEG.");
      bool MutableData = Name == ".mcs251.dseg";
      if (Overlay || StaticSlot || MutableData) {
        SectionData Data;
        Data.Section = &Section;
        Data.Size = Asm->getSectionAddressSize(Section);
        Data.AreaName = Overlay ? "OSEG" : "DSEG";
        Data.Flags = Overlay ? 4 : 0;
        Data.NoLoad = true;
        Sections.push_back(std::move(Data));
        HasSlots |= Overlay || StaticSlot || MutableData;
        continue;
      }
      if (Section.isBssSection())
        report_fatal_error("MCS251 REL writer: unsupported BSS section");
      SmallString<256> Storage;
      raw_svector_ostream DataOS(Storage);
      Asm->writeSectionData(DataOS, &Section);
      if (Storage.empty() && !Section.hasInstructions())
        continue;
      SectionData Data;
      Data.Section = &Section;
      Data.Bytes.assign(Storage.begin(), Storage.end());
      Data.Size = Data.Bytes.size();
      if (Name == ".mcs251.xinit") {
        Data.AreaName = "XINIT";
        Data.Flags = 0x20;
      } else {
        Data.Base = TotalSize;
        TotalSize += Data.Size;
      }
      Sections.push_back(std::move(Data));
    }

    // One CSEG MCSection and one XINIT MCSection are the complete loadable
    // model. Everything else must be an explicitly recognized reservation.
    if (llvm::count_if(Sections, [](const SectionData &S) {
          return !S.NoLoad && S.AreaName == "CSEG";
        }) > 1)
      report_fatal_error("MCS251 REL writer: multiple CSEG sections are "
                         "not supported yet");
    // CSEG stays index 1 for compatibility. XINIT and every parameter/global
    // reservation receive independent A records; same-named DSEG records
    // concatenate in the linker while OSEG records overlay.
    unsigned NextArea = 2;
    for (SectionData &Sec : Sections)
      if (Sec.AreaName != "CSEG")
        Sec.AreaIndex = NextArea++;

    std::vector<const MCSymbol *> Globals;
    for (const MCSymbol &S : Asm->symbols())
      if (isGlobal(S) && S.getName() != ".__.ABS.")
        Globals.push_back(&S);

    // Keep undefined references first, then the synthetic absolute symbol;
    // area-local definitions are emitted after the area record below. The
    // exact order is not semantically significant because R stores indices
    // (sdas itself emits them in hash-table order).
    std::vector<const MCSymbol *> Undefined;
    std::vector<const MCSymbol *> Defined;
    for (const MCSymbol *S : Globals) {
      if (S->isUndefined())
        Undefined.push_back(S);
      else {
        // Validate even a single definition; a sort comparator need not run.
        if (!S->isInSection() || !findSection(Sections, &S->getSection()))
          report_fatal_error("MCS251 REL writer: unsupported symbol section");
        Defined.push_back(S);
      }
    }
    std::sort(Undefined.begin(), Undefined.end(),
              [](const MCSymbol *A, const MCSymbol *B) {
                return A->getName() < B->getName();
              });
    std::sort(Defined.begin(), Defined.end(),
              [&](const MCSymbol *A, const MCSymbol *B) {
                const SectionData *SA = A->isInSection()
                    ? findSection(Sections, &A->getSection()) : nullptr;
                const SectionData *SB = B->isInSection()
                    ? findSection(Sections, &B->getSection()) : nullptr;
                if (SA->AreaIndex != SB->AreaIndex)
                  return SA->AreaIndex < SB->AreaIndex;
                uint64_t OA = symbolOffset(*Asm, *A);
                uint64_t OB = symbolOffset(*Asm, *B);
                if (OA != OB)
                  return OA < OB;
                return A->getName() < B->getName();
              });

    std::map<const MCSymbol *, unsigned> SymbolRefs;
    unsigned NextRef = 0;
    for (const MCSymbol *S : Undefined)
      SymbolRefs[S] = NextRef++;
    // The absolute pseudo symbol is a conventional ASxxxx global and is useful
    // as a stable absolute reference for future area-relative expressions.
    unsigned AbsRef = NextRef++;
    (void)AbsRef;
    for (const MCSymbol *S : Defined)
      SymbolRefs[S] = NextRef++;

    OS << "XH3\n";
    OS << "H ";
    hexMin(OS, NextArea + (HasSlots ? 1 : 0)); // plus REG_BANK_0 reservation
    OS << " areas ";
    hexMin(OS, NextRef);
    OS << " global symbols\n";
    StringRef Main = Asm->getContext().getMainFileName();
    OS << "M " << moduleName(Main) << '\n';
    OS << "O " << MCS251::ABISignaturePayload << '\n';

    for (const MCSymbol *S : Undefined) {
      OS << "S " << S->getName() << " Ref";
      hex(OS, 0, 6);
      OS << '\n';
    }
    OS << "S .__.ABS. Def";
    hex(OS, 0, 6);
    OS << '\n';

    OS << "A _CODE size ";
    hexMin(OS, 0);
    OS << " flags ";
    hexMin(OS, 0);
    OS << " addr ";
    hexMin(OS, 0);
    OS << '\n';
    OS << "A CSEG size ";
    hexMin(OS, TotalSize);
    OS << " flags ";
    hexMin(OS, 0x20);
    OS << " addr ";
    hexMin(OS, 0);
    OS << '\n';

    auto EmitDefinitions = [&](unsigned Index) {
      for (const MCSymbol *S : Defined) {
        const SectionData *Sec = S->isInSection()
            ? findSection(Sections, &S->getSection()) : nullptr;
        if (!Sec || Sec->AreaIndex != Index)
          continue;
        OS << "S " << S->getName() << " Def";
        hex(OS, Sec->Base + symbolOffset(*Asm, *S), 6);
        OS << '\n';
      }
    };
    EmitDefinitions(1);
    for (const SectionData &Sec : Sections) {
      if (Sec.AreaName == "CSEG")
        continue;
      OS << "A " << Sec.AreaName << " size ";
      hexMin(OS, Sec.Size);
      OS << " flags ";
      hexMin(OS, Sec.Flags);
      OS << " addr 0\n";
      EmitDefinitions(Sec.AreaIndex);
    }
    // Keep static slots off the classic register-bank aliases, as SDCC does.
    if (HasSlots)
      OS << "A REG_BANK_0 size 8 flags 4 addr 0\n";

    // sdas emits an initial empty T/R pair when it opens an area.
    OS << "T";
    addr24(OS, 0);
    OS << '\n';
    OS << "R";
    byte(OS, 0);
    byte(OS, 0);
    word(OS, 1);
    OS << '\n';

    for (const SectionData &Sec : Sections) {
      if (Sec.NoLoad)
        continue; // reservations must not become bytes in the ROM image
      std::map<uint64_t, const Relocation *> At;
      for (const Relocation &Rel : Relocations)
        if (Rel.Fragment && Rel.Fragment->getParent() == Sec.Section)
          At[Asm->getFragmentOffset(*Rel.Fragment) + Rel.Fixup.getOffset()] = &Rel;
      uint64_t Pos = 0;
      while (Pos < Sec.Bytes.size()) {
        // Byte-of-24 relocations occupy THREE bytes in T but ONE byte in
        // machine code. Keep all addresses/symbol offsets in machine bytes,
        // and build a separate T-index map after expanding the placeholders.
        SmallVector<uint8_t, 16> Payload;
        std::map<uint64_t, unsigned> TIndices;
        uint64_t End = Pos;
        while (End < Sec.Bytes.size()) {
          auto It = At.find(End);
          const Relocation *Rel = It == At.end() ? nullptr : It->second;
          unsigned Width = Rel ? relocationWidth(Rel->Fixup.getKind()) : 1;
          bool Byte24 = Rel && Rel->Fixup.getKind() >= MCS251::fixup_mcs251_lo8;
          unsigned TWidth = Byte24 ? 3 : Width;
          if (Payload.size() + TWidth > MaxTPayload)
            break;
          if (Rel)
            TIndices[End] = Payload.size() + 3;
          if (Byte24) {
            // Rel.Value is the unshifted area-relative symbol+addend from MC.
            // The linker adds its final base before choosing lo/mid/hi.
            Payload.push_back(Rel->Value >> 16);
            Payload.push_back(Rel->Value >> 8);
            Payload.push_back(Rel->Value);
          } else {
            for (unsigned I = 0; I < Width; ++I)
              Payload.push_back(Sec.Bytes[End + I]);
          }
          End += Width;
        }
        if (End == Pos)
          report_fatal_error("MCS251 REL writer: relocation chunk overflow");
        OS << "T";
        addr24(OS, Pos + Sec.Base);
        for (uint8_t B : Payload)
          byte(OS, B);
        OS << '\n';
        emitRLine(Sec, Pos, End - Pos, Sec.AreaIndex, SymbolRefs, TIndices,
                  Sections);
        Pos = End;
      }
    }
    return OS.tell();
  }
};
} // namespace

std::unique_ptr<MCObjectWriter>
llvm::createMCS251RELObjectWriter(raw_pwrite_stream &OS) {
  return std::make_unique<MCS251RELObjectWriter>(OS);
}
