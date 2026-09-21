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

// E1 (WP2): the ISR reentrancy check's conflict severity.  This axis decides
// ONLY how a PROVEN static parameter-slot contract violation is reported; it
// never decides whether the analysis runs (the structured report and the
// link-facts interface exist independently) and it can never certify a link:
// Off/Warn are diagnostic experiment modes and their output says so.
enum class IsrReentrancySeverity : uint8_t {
  Off = 0,   // no reentrancy diagnostics; coverage state reports "unchecked"
  Warn = 1,  // report everything, fail nothing (explicitly uncertified)
  Error = 2, // proven slot-conflict violations fail the link (the default)
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
  // E1 (WP2): static parameter-slot reentrancy check.  Conflict severity
  // and coverage policy are SEPARATE axes (IC-014): the severity only
  // governs how a PROVEN address-level slot conflict is reported, while
  // IsrReentrancyCoverageRequire governs whether unknown analysis facts
  // (invisible indirect calls, undecodable instruction boundaries, missing
  // function sizes) fail the link.  The address-level check is on by
  // default and a proven violation fails the link by default; the legacy
  // function-level "shared function" relations remain candidate warnings
  // and are never auto-upgraded to errors by this switch.
  IsrReentrancySeverity IsrReentrancy = IsrReentrancySeverity::Error;
  bool IsrReentrancyCoverageRequire = false;
  // E1: the external preemption contract, as (ISR entry symbol name A,
  // ISR entry symbol name B) pairs the board input declares able to
  // preempt each other.  The flavor shell reads and syntax-validates the
  // input; the core only checks every name resolves to a REGISTERED ISR
  // entry and never guesses board configuration.  The default contract
  // without pairs: a registered ISR may preempt the foreground; nothing is
  // assumed between two registered ISRs.
  //
  // R5-r2: each pair keeps the contract line it came from (lexically
  // absolute file + 1-based PHYSICAL line number), so a diagnostic about a
  // single pair can name where the user wrote it.  Without it the
  // same-name reject in diagnoseIsrReentrancy could only quote the two
  // names, leaving a multi-line contract with no way to locate the
  // offending row (the placement manifest already carries the same
  // provenance for the same reason).
  struct IsrPreemptionPair {
    std::string NameA, NameB; // The two ISR entry names, in file order.
    std::string File;         // Lexically absolute contract path.
    uint32_t Line = 0;        // 1-based physical line within that file.
  };
  std::vector<IsrPreemptionPair> IsrPreemptionPairs;
  // E2 (WP3 feed): collect the versioned link-facts interface into
  // Result.Facts.  Pure collection switch: it never changes layout,
  // diagnostics or the exit code.
  bool EmitLinkFacts = false;
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

  // E1 (WP2): the single authoritative ISR reentrancy analysis result.
  // Both the human diagnostics and the link-facts interface render from
  // this structure; no consumer re-parses warning text to rebuild
  // relationships.  Identity is (input object ordinal, input section index,
  // symbol table index); names are carried for explanation only.  The
  // section index is part of the identity because a slot area without any
  // sized slot symbol is identified by its section.
  struct ReentrancyID {
    uint32_t ObjectId = 0;
    uint32_t Section = 0xffffffff;  // input section index (shndx)
    uint32_t SymIndex = 0xffffffff; // 0xffffffff = anonymous section area
  };
  // Evidence classes, in decreasing strength.  Only a REAL evidence class
  // may carry a proven conclusion; address references and unconfirmed
  // candidates never do.
  enum ReentrancyEvidence : uint8_t {
    EvAddressRef = 0,     // data relocation to code storage: NOT a call
    EvUnverified = 1,     // a relocation whose instruction/opcode could not
                          // be confirmed at a trusted boundary
    EvVerifiedControl = 2,// relocation on a confirmed control opcode at a
                          // trusted instruction boundary
    EvVerifiedAccess = 3, // relocation on a confirmed slot-access operand
    EvContract = 4,       // derived from an explicit platform/producer
                          // contract already enforced elsewhere
  };
  struct ReentrancyAccess {
    ReentrancyID Accessor;
    std::string AccessorName; // auxiliary explanation only
    std::string AccessorFile; // short file name, auxiliary
    uint8_t Role = 0;      // 0 undetermined, 1 caller-write, 2 callee-read
    uint8_t Evidence = 0;  // ReentrancyEvidence of the slot access
    uint8_t EdgeEvidence = 0; // ReentrancyEvidence of the accessor->owner edge
    uint8_t Context = 0;   // bit0 ISR-reachable, bit1 foreground-reachable
    bool IsrRooted = false; // positively reachable from a registered ISR
    bool FgRooted = false;  // positively reachable from the reset chain
  };
  // Complete per-slot evidence inside a conflict group, so no consumer has
  // to reconstruct which slots and accesses established the group.
  struct ReentrancySlotEvidence {
    ReentrancyID ID;
    std::string Name, SectionName, Region, FileName;
    uint32_t Lo = 0, Hi = 0;
    bool ExtentKnown = true;
    bool SectionGranular = false;
    std::vector<ReentrancyAccess> Accesses;
  };
  struct ReentrancyConflict {
    uint8_t Kind = 0; // 0 = ISR vs foreground, 1 = ISR vs ISR
    bool Proven = false;  // complete evidence chain; otherwise conditional
    bool SameSlot = false; // one slot reached from both contexts
    uint32_t Lo = 0, Hi = 0; // shared byte range; Lo == Hi marks an
                             // address-level hit with unknown extent
    bool ExtentExact = true;
    // The specific slot pair that establishes this group; for SameSlot both
    // identities are the same slot.  When Proven this is the DECIDING pair
    // -- the FIRST pair that established the group (an explicit selection;
    // later established pairs do not overwrite it), never merely the first
    // candidate seen; when conditional it is the representative (first)
    // pair.
    //
    // R5-r2 (semantics, stated so no stronger property is claimed): the
    // selection is "the first established pair in EVALUATION ORDER", i.e.
    // the order the candidate pairs are generated and tested in.  It is NOT
    // a canonical choice by ISR name, slot identity or configuration order,
    // so it is not stable under a REORDERING of the inputs that changes the
    // candidate order: two links of the same program whose objects,
    // sections or roots are permuted may name different -- individually
    // correct -- deciding pairs and print a different (also correct)
    // `slots='.  Consumers must therefore read the deciding pair as "one
    // pair whose evidence chain is complete", never as an identity derived
    // from the input set.  What IS invariant is the VERDICT: any established
    // pair proves the group, so the proven/conditional state, the shared
    // range and the participating slot set do not depend on the order.
    ReentrancyID SlotA{}, SlotB{};
    // R5-2: when Proven, the DECIDING pair's root evidence -- the roots of
    // the ONE pair that established the group, never the union over every
    // established pair (with `_irq1/_irq2' and `_irq2/_irq3' both declared
    // the union would name roots the deciding pair does not have).  Empty
    // when the group is not proven.
    std::vector<std::string> DecidingRootsA, DecidingRootsB;
    // The complete participating set with per-slot access evidence.
    std::vector<ReentrancySlotEvidence> Slots;
    std::vector<std::string> IsrRootsA, IsrRootsB; // explanation only
    // R4-2: why not proven.  This string only describes the pairs that are
    // NOT proven; which pairs those are is enumerated in Pairs.
    std::string MissingConditions;
    // R4-2: per-pair evaluation records.  Every candidate pair keeps its own
    // identities, root evidence and missing conditions, so a partially
    // established group distinguishes its proven pair from the pairs whose
    // conditions are still missing.
    struct PairEvidence {
      ReentrancyID A{}, B{};
      bool Proven = false;
      std::string MissingConditions; // empty when Proven
      std::vector<std::string> RootsA, RootsB;
    };
    std::vector<PairEvidence> Pairs;
  };
  struct ReentrancyUnknown {
    uint8_t Kind = 0; // 0 ECALLr sites, 1 direct ecall w/o relocation,
                      // 2 unresolved control target, 3 undecodable code
                      // section, 4 unattributed slot access,
                      // 5 size-less attributed function,
                      // 6 control relocation without a confirmed control
                      //   instruction (no call evidence), 7 slot storage
                      //   reference without a confirmed access
    uint32_t Count = 0;
  };
  struct ReentrancyCandidate { // legacy function-level relation (IC-014:
                               // candidate only, never auto-error)
    ReentrancyID Function;
    bool Mixed = false, MultiIsr = false;
    std::vector<std::string> IsrRoots, FgCallers;
  };
  struct ReentrancyReport {
    uint32_t Version = 1;
    // 0 undeclared (no IRQ metadata anywhere: NOT "no asynchronous entry"),
    // 1 unchecked (severity Off), 2 open (unknown facts exist),
    // 3 closed (no unknown call evidence)
    uint8_t CoverageState = 0;
    uint32_t RegisteredIsrs = 0, Functions = 0, Slots = 0;
    std::vector<ReentrancyUnknown> Unknowns;
    std::vector<ReentrancyConflict> Conflicts;
    std::vector<ReentrancyCandidate> Candidates;
  };
  ReentrancyReport Reentrancy;

  // E2 (WP3 feed): the versioned link-facts interface.  Minimal on purpose
  // for the first release: final capacities, object/section/symbol identity,
  // ISR registration, the direct call graph, discovered indirect-call sites,
  // per-function execution contexts and the reentrancy report above.  The
  // independent budgeter consumes this instead of parsing diagnostics.
  struct LinkFacts {
    uint32_t Version = 1;
    struct Object {
      std::string Path, Sha256;
    };
    struct Section {
      uint32_t ObjectId = 0, InputShndx = 0;
      std::string Name, Region;
      uint8_t Space = 0; // 0 DATA, 1 XDATA, 2 CODE
      uint32_t Address = 0, Size = 0;
      uint64_t Flags = 0;
    };
    struct Symbol {
      uint32_t ObjectId = 0, SymIndex = 0;
      std::string Name;
      uint32_t Address = 0, Size = 0;
      uint8_t Type = 0, Bind = 0;
    };
    struct IsrEntry {
      uint32_t Slot = 0;
      ReentrancyID Entry;
      std::string Name;
    };
    struct Edge {
      ReentrancyID From, To;
      uint8_t Evidence = 0; // ReentrancyEvidence of this edge
    };
    // One enumerated platform-contract site (frozen-CRT reset/boot chain,
    // contract version 1): the identity a proven foreground chain rests on.
    struct Contract {
      uint8_t Version = 1, Kind = 0;
      uint32_t ObjectId = 0, Shndx = 0, Offset = 0;
      ReentrancyID Target;
      std::string Name;
    };
    struct ICall {
      uint32_t ObjectId = 0, Shndx = 0, Offset = 0;
      ReentrancyID In;
    };
    struct Context {
      ReentrancyID Function;
      bool Fg = false;
      // True when foreground reachability depends on at least one
      // platform-contract edge (the frozen-CRT chain).
      bool FgContract = false;
      std::vector<std::string> IsrRoots;
    };
    struct Capacity {
      uint32_t Spx = 0, CapacityBytes = 0, EdataEnd = 0, IramSize = 0,
               StackHigh = 0;
      std::vector<std::pair<std::string, uint32_t>> Areas;
    };
    std::vector<Object> Objects;
    std::vector<Section> Sections;
    std::vector<Symbol> Symbols;
    std::vector<IsrEntry> Isrs;
    std::vector<Edge> Edges;
    std::vector<ICall> ICalls;
    std::vector<Contract> Contracts;
    std::vector<Context> Contexts;
    Capacity Cap;
    ReentrancyReport Reentrancy;
  };
  LinkFacts Facts;
};

bool linkCore(LinkerConfig Config, LinkerResult &Result,
              llvm::raw_ostream &Err);

} // namespace lld::mcs251

#endif
