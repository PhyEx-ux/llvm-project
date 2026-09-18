//===- LinkerCore.h - MCS251 ELF link semantics -----------------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This interface deliberately has no dependency on the lld flavor driver.  It
// is shaped so the ELF/Arch MCS251 target handler can adopt it upstream.
//===----------------------------------------------------------------------===//

#ifndef LLD_MCS251_LINKERCORE_H
#define LLD_MCS251_LINKERCORE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lld::mcs251 {

struct Range {
  uint32_t Start = 0;
  uint32_t End = 0;
};

// The flavor shell supplies policy defaults. In particular, this core does
// not presume an Intel HEX serializer or a stack-capacity policy.
struct LinkerConfig {
  uint32_t IramSize = 128;
  uint32_t EdataEnd = 0;
  uint32_t StackSize = 0;
  // E3/M4 Code ROM gate.  Off by default: the build layer owns the decision
  // to pass a flash window, the linker never presumes one.  When on, every
  // occupied CODE-class section (HOME/VECS/BOOT/CSEG/XINIT) must lie entirely
  // within [FlashBase, FlashBase + FlashSize).  Holes inside an area remain
  // legal (sparse layout is a design feature); only occupied sections are
  // checked.  XSEG is XDATA NOBITS in a separate address space (SPEC §4.1:
  // no ROM load bytes) and is not gated.  The window is plain numbers only.
  bool FlashGate = false;
  uint32_t FlashBase = 0;
  uint32_t FlashSize = 0;
  // X3: optional board-level XDATA capacity in bytes.  0 (the default) means
  // "not configured": only the always-on 24-bit range check applies, which
  // keeps legacy layouts linkable.  When set, every allocated XSEG range must
  // lie inside [area-start(XSEG), area-start(XSEG)+N).
  uint32_t XdataSize = 0;
  std::vector<std::pair<std::string, uint32_t>> AreaStarts;
  std::vector<Range> ReservedData;
  std::vector<std::string> Inputs;
  bool PrintInput = false;
  bool EnableStackGate = false;
  // E2: static parameter-slot ABI reentrancy diagnosis.  Default on; the
  // flavor shell owns the --isr-reentrancy / --no-isr-reentrancy policy.  The
  // diagnosis only warns, never fails the link.
  bool IsrReentrancyDiag = true;
  // E5 traceability switch.  Off by default: the frozen release artifacts pin
  // the exact output bytes (realhw-demo/release/manifest.json hashes the
  // linked ELF and the map), so emitting a final symbol table and map
  // function rows must be an explicit opt-in, never a silent default.
  bool KeepSymbols = false;
  // G11 (design §7): the raw lines of the --placement-manifest file, read by
  // the flavor shell during option parsing.  The core never sees the path or
  // the file system; mergePlacement() owns the line grammar and the
  // malformed-entry diagnostic.  G11-D provenance (contract §4.2) additionally
  // needs WHERE a line came from (real path + 1-based physical line number),
  // so each entry carries its origin next to the text.
  struct ManifestEntry {
    std::string Path; // Lexically absolute manifest path (never realpath).
    uint32_t Line = 0; // 1-based physical line number within that file.
    std::string Text;  // The raw line (grammar is mergePlacement's job).
  };
  std::vector<ManifestEntry> PlacementManifest;
  // G11-D2 (design §6.1): audit-mode positioning collection switch.  Off by
  // default: a plain link must not pay for, or depend on, the positioning
  // data.  Set only when an audit option (--placement-report /
  // --verify-placement) requested the positioning carrier in the final ELF.
  bool CollectPositions = false;
};

// E5: one final-ELF symbol collected after layout.  Synth marks the
// synthesised boundary/stack symbols; they resolve to SHN_ABS in the output
// because they belong to no loadable output section.
struct OutputSymbol {
  std::string Name;
  uint32_t Address = 0;
  uint32_t Size = 0;
  uint8_t Bind = 0; // ELF::STB_LOCAL / ELF::STB_GLOBAL
  uint8_t Type = 0; // ELF::STT_NOTYPE / ELF::STT_OBJECT / ELF::STT_FUNC
  bool Synth = false;
};

struct LinkerResult {
  uint32_t Entry = 0;
  std::map<uint32_t, uint8_t> Image;
  std::string Map;
  std::string InputReport;
  // E2: complete, newline-terminated warning lines ("mcs251-lld: warning: "
  // prefix included) produced by the ISR reentrancy diagnosis.  Empty unless
  // the diagnosis found a hazard.
  std::string Diagnostics;
  // E5: final symbols with post-layout addresses, always collected so the
  // flavor shell can decide whether to serialize them.
  std::vector<OutputSymbol> Symbols;
  // G11 (design §3.4): the merged placement contract produced by
  // mergePlacement() -- one row per stable-symbol group, including the
  // bind-only rows.  `LayoutHash` is the report hash recomputed over the
  // MERGED fields (rev 7 B1); the per-input source hashes stay in the input
  // objects for the independent verifier and are never copied here.  The
  // report file serialization and the verifier are G11-D consumers.
  struct PlacementRecord {
    std::string Stable, Sym, File, Section;
    uint32_t Address = 0, Size = 0, Align = 1, Flags = 0, LayoutHash = 0;
    uint8_t StorageClass = 0, Entity = 0, Ownership = 0;
    bool BoundOnly = false;
    // G11-D provenance (contract §4.2): the complete set of source
    // descriptions that produced this MERGED row — one entry per original
    // NOTE record and per manifest row, never a single "representative"
    // source.  The report serializes one line per distinct (path, ELF name,
    // stable) triple; the verifier re-reads the original objects and treats
    // this vector as a claim to check, never as ground truth.
    struct Source {
      // 0 = input-object placement NOTE record, 1 = manifest row.
      enum Kind : uint8_t { Note = 0, Manifest = 1 };
      std::string Path;     // Input object path / real manifest path.
      std::string ElfName;  // Actual ELF symbol name of this source.
      std::string Section;  // Input section (empty for bind / manifest).
      uint8_t Kind = Note;
      // NOTE record index within that object's `.mcs251.placement` stream, or
      // the 1-based physical manifest line number.
      uint32_t Index = 0;
    };
    std::vector<Source> Sources;
  };
  std::vector<PlacementRecord> Placement;
  // G11-D2 (design §6.1): the independent positioning data, collected by
  // collectPositions() AFTER the layout is final (G8 migration has already
  // rewritten Region, G13b synthesis has already happened).  Deliberately
  // separate from PlacementRecord: the placement contract describes entities,
  // not every allocated input section.  The final-ELF NOTE serializer and the
  // independent verifier are the consumers; neither may treat this as ground
  // truth without re-reading the artifacts.
  struct PositionSlice {
    uint32_t InputOffset = 0;
    uint32_t FinalAddress = 0;
    uint32_t Length = 0;
  };
  struct PositionSection {
    uint32_t ObjectId = 0;
    uint32_t InputShndx = 0;
    uint8_t StorageSpace = 0; // 0=AS0-DATA, 1=XDATA, 2=CODE (design §5.1)
    uint32_t InputSize = 0;  // The original input section sh_size.
    std::vector<PositionSlice> Slices;
  };
  // SHA-256 of each input object's complete raw file bytes, in Config.Inputs
  // order; object_id is the index into this table.
  std::vector<std::array<uint8_t, 32>> PositionObjects;
  // One row per legal ALLOC input section (design §5.3), sorted by
  // (ObjectId, InputShndx).  Synthesized sections and the DATA_EMPTY_PENDING
  // ("IGNORE") exemption never appear here.
  std::vector<PositionSection> PositionSections;
};

bool linkCore(LinkerConfig Config, LinkerResult &Result,
              llvm::raw_ostream &Err);

} // namespace lld::mcs251

#endif
