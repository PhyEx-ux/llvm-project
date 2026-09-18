//===- LinkerCore.cpp - MCS251 ELF link semantics --------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This target semantic core has no dependency on the MCS251 flavor shell.
// It is intentionally shaped for a future lld/ELF/Arch/MCS251.cpp target handler.
//===----------------------------------------------------------------------===//

#include "LinkerCore.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MCS251Attributes.h"
#include "llvm/BinaryFormat/MCS251AttributesReader.h"
#include "llvm/BinaryFormat/MCS251ISR.h"
#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
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
// G8 (design §3, option (i)): the producer's "may be placed in EDATA when the
// low window cannot hold this AS0 section" capability bit.  It lives in the
// MCS251 processor flag range next to SHF_MCS251_OVERLAY and is mirrored in
// llvm/BinaryFormat/ELF.h for the producer and the object tools.
static constexpr uint32_t SHF_MCS251_EDATA_MOVABLE = 0x20000000;
// G13b (design §3.1, D3 ruling): the producer's "this XSEG section is ONE
// logical all-zero object larger than the 16-bit record limit" mark.  Lives
// in the MCS251 processor flag range next to SHF_MCS251_OVERLAY and is
// mirrored in llvm/BinaryFormat/ELF.h for the producer and the object tools.
static constexpr uint32_t SHF_MCS251_XSEG_SPLIT = 0x40000000;
static constexpr uint32_t ABI_FLAGS = 0x00000001;
// W4 (design §4.2): the v2 object-protocol e_flags value.  The low byte is
// the object protocol version (2, agreeing with Tag 4); bit 8 is the
// source/native marker.  "Other flags values are rejected": no mask-and-guess
// path exists, only these two exact words.
static constexpr uint32_t ABI_FLAGS_V2 = MCS251Attributes::EFlagsV2;
static constexpr uint32_t EF_ABI_MASK = 0xff;

// W4 (design §4): the capability bits this linker implements.  This is
// deliberately a literal, never a rebinding of the codec's registered
// required-values: those describe what an *object* may demand, while this
// describes what the *linker* can honour.  Deriving one from the other would
// silently claim support the day a future PM registers a non-zero
// capability.  A4 implements the empty set, so the cross-object union check
// below fails closed on any bit; a future revision must widen this constant
// deliberately, before it registers a capability that an object may require.
static constexpr uint32_t SupportedCapabilitiesLo = 0;
static constexpr uint32_t SupportedCapabilitiesHi = 0;

// Bit-object input contract (lld/MCS251/BIT-OBJECT-CONTRACT.md).  The bit
// relocation numbers 10/11 are an lld-local extension until the public
// MCS251.def header registers them (BT00 owns that llvm/** change); the
// backend stream emits the raw numeric types in the meantime.
static constexpr uint32_t R_MCS251_BIT_REF = 10;    // zero-width identity
static constexpr uint32_t R_MCS251_BITADDR8 = 11;   // 1-byte address field
static constexpr StringRef BitObjectSectionName = ".mcs251.bit";
static constexpr StringRef BitProfileSectionName = ".mcs251.bitprofile";
static constexpr uint32_t BitObjectRecordSize = 8;
// 128 bit addresses 0x00-0x7F map onto bytes 0x20-0x2F.
static constexpr uint32_t BitWindowBase = 0x20;
static constexpr uint32_t BitCount = 128;
static constexpr uint32_t BitByteCount = 16;
// Record field offsets (big-endian; the section is a whole number of records).
namespace BitRec {
static constexpr unsigned Version = 0;       // u8, must be 1
static constexpr unsigned Kind = 1;          // u8, 1=definition 2=reference
static constexpr unsigned InitValue = 2;     // u8, 0 or 1
static constexpr unsigned Capabilities = 3;  // u8, must be 1
static constexpr unsigned SymbolRef = 4;     // u32 zero; R_MCS251_BIT_REF at +4
} // namespace BitRec
static constexpr uint8_t BitRecordVersion = 1;
static constexpr uint8_t BitKindDefinition = 1;
static constexpr uint8_t BitKindReference = 2;
static constexpr uint8_t BitCapabilities = 1;

// BT14: the bit-aware CRT profile section (crt-bit.yaml).  A non-ALLOC 16-byte
// record declaring that this object is the bit CRT whose __mcs251_bit_init
// walker applies the lld-synthesized .mcs251.bittable mask-RMW records, so
// external owners' neighbouring bits are PRESERVED instead of being cleared
// by the old 16-byte window clear (BIT-OBJECT-CONTRACT.md §7's S1 subset).
// The profile is the BT14 asset wire format; every field is frozen here and
// unknown values fail closed (design §6.4 L13).
static constexpr uint32_t BitProfileRecordSize = 16;
static constexpr uint16_t BitProfileVersion = 1;
static constexpr uint16_t BitProfileSizeWord = 16;
static constexpr uint8_t BitProfileKind = 1;          // bit-aware CRT
static constexpr uint8_t BitProfileStrategyMaskRMW = 1; // preserve neighbours
static constexpr uint16_t BitProfileEntryWalker = 1;  // __mcs251_bit_init
// Field offsets in the big-endian 16-byte record.
namespace BitProfileRec {
static constexpr unsigned Version = 0;      // u16, must be 1
static constexpr unsigned Size = 2;         // u16, must be 16
static constexpr unsigned Kind = 4;         // u8, must be 1
static constexpr unsigned Strategy = 5;     // u8, must be 1
static constexpr unsigned PoolBase = 6;     // u16, must be 0x0020
static constexpr unsigned PoolSize = 8;     // u16, must be 0x0010
static constexpr unsigned WindowBits = 10;  // u16, must be 0x0080
static constexpr unsigned EntryKind = 12;   // u16, must be 1 (walker)
static constexpr unsigned Reserved = 14;    // u16, must be 0
} // namespace BitProfileRec
// The walker entry the profile carrier must define in executable code.
static constexpr StringRef BitInitWalkerName = "__mcs251_bit_init";

// G11 (design rev 7 §3.2/§3.3): the fixed-placement object contract.  The
// `.mcs251.placement` SHT_NOTE carrier is whitelisted by its exact name, and
// every record follows the frozen v1 layout (all fields big-endian):
//   u32 record_size (= alignTo(25 + stable_len, 4), canonical)
//   u8  schema_version (=1), storage_class, entity, ownership
//   u32 address (24-bit), size, align, flags, layout_hash
//   u8  stable_len; u8 stable[stable_len]; zero padding to a 4-byte multiple
// The envelope is namesz=7 ("MCS251\0" plus one pad byte, 8 storage bytes),
// type=1.  Unknown encodings, truncation and non-canonical record sizes are
// malformed and fail closed.  layout_hash never participates in any link
// decision (rev 7 B1); the linker judges only by explicit fields.
static constexpr StringRef PlacementNoteSectionName = ".mcs251.placement";
static constexpr StringRef FixedSectionPrefix = ".mcu.fixed.";
// G11-D2 (design §3.1): the versioned positioning carrier.  It is a
// FINAL-OUTPUT-ONLY section: an input object carrying this exact name is
// rejected (no post-copy, no "accept and ignore" metadata entry, and no
// `.mcs251.*` wildcard is introduced by it -- C2 keeps its own scope).
static constexpr StringRef PositionsNoteSectionName =
    ".mcs251.placement.positions";
// G11-C2 (design rev 8 §8.3, PM rulings R-2026-09-17-4 / R-2026-09-18-1): the
// record->symbol association carrier.  `.mcs251.placement` v1 carries no ELF
// name (asm-labels and target mangling break any "stable == declaration name"
// assumption), so the mapping travels in a SEPARATE dedicated NOTE:
//   namesz=7 ("MCS251\0" incl. the NUL, name field padded to 8 storage bytes),
//   type=2 (placement-name association, NOT "schema version 2"),
//   desc = u32 association_version (=1), u32 entry_count, then entry_count
//   entries of { u32 placement_record_index, u32 elf_name_len,
//                u8 elf_name[elf_name_len], zero pad to a 4-byte multiple }.
// All multi-byte fields are big-endian.  SHT_NOTE, flags=0, align=4; the
// section never enters the output image.  It is accepted by its EXACT name
// only (no `.mcs251.*` wildcard) and parsed structurally in loadFile(); every
// semantic association is mergePlacement()'s job.
static constexpr StringRef PlacementNamesNoteSectionName =
    ".mcs251.placement.names";
static constexpr uint32_t PlacementNoteNameSize = 7;
static constexpr uint32_t PlacementNoteType = 1;
static constexpr uint32_t PlacementNamesNoteType = 2;
static constexpr uint32_t PlacementNamesVersion = 1;
static constexpr uint8_t PlacementSchemaVersion = 1;
// storage_class encoding (frozen §3.2 record v1).
enum PlacementStorageClass : uint8_t {
  PSC_AS0_DATA = 0,
  PSC_XDATA = 1,
  PSC_CODE = 2,
};
// entity encoding.
enum PlacementEntity : uint8_t {
  PE_OBJECT = 0,
  PE_FUNCTION = 1,
};
// ownership encoding.
enum PlacementOwnership : uint8_t {
  PO_OWNED = 0,
  PO_BIND = 1,
};
// flags bits (bit0 retain -- owned records only; bit1 noinit).
static constexpr uint32_t PlacementFlagRetain = 1;
static constexpr uint32_t PlacementFlagNoInit = 2;

static StringRef placementClassName(uint8_t C) {
  switch (C) {
  case PSC_AS0_DATA:
    return "data";
  case PSC_XDATA:
    return "xdata";
  case PSC_CODE:
    return "code";
  }
  return "?";
}

// One parsed placement NOTE record.  Structural decoding happens in loadFile
// (so --print-input exercises it); every semantic association -- sections,
// entity symbols, cross-object grouping -- is mergePlacement's job.
struct PlacementNoteRecord {
  uint32_t Offset = 0; // Record base inside the NOTE section.
  // G11-D provenance (contract §4.2): physical record index within this
  // object's `.mcs251.placement` stream, numbered from 0 in stream order.
  // It locates an original record; it is never an entity identity.
  uint32_t Index = 0;
  uint8_t StorageClass = 0;
  uint8_t Entity = 0;
  uint8_t Ownership = 0;
  uint32_t Address = 0;
  uint32_t Size = 0;
  uint32_t Align = 1;
  uint32_t Flags = 0;
  uint32_t LayoutHash = 0; // Carried for the LinkerResult report row only.
  std::string Stable;
  // G11-C2: the `.mcs251.placement.names` association for THIS record, pinned
  // by physical record index during the carrier decode.  `HasAssoc` is false
  // only for an object that carries no association carrier at all (the legacy
  // boundary): an owned record may then recover its name from the dedicated
  // fixed section's unique principal symbol, while a bind record must be
  // refused (design rev 8 §8.3, no guessing).
  bool HasAssoc = false;
  std::string AssocName;
};

// H(F) (design §3.2 rev 7 B1): SHA-256 of the BE-packed
// (schema_version, storage_class, entity, ownership, address, align, flags)
// -- four u8 then three u32 -- truncated to the low 32 bits of the digest
// read big-endian.  `size` and the stable symbol never participate.  The
// link uses this only to annotate the merged report row; acceptance,
// merging and placement decisions read explicit fields only.
static uint32_t placementLayoutHash(uint8_t StorageClass, uint8_t Entity,
                                    uint8_t Ownership, uint32_t Address,
                                    uint32_t Align, uint32_t Flags) {
  const uint8_t F[16] = {
      PlacementSchemaVersion, StorageClass, Entity, Ownership,
      uint8_t(Address >> 24), uint8_t(Address >> 16), uint8_t(Address >> 8),
      uint8_t(Address),
      uint8_t(Align >> 24),   uint8_t(Align >> 16),   uint8_t(Align >> 8),
      uint8_t(Align),
      uint8_t(Flags >> 24),   uint8_t(Flags >> 16),   uint8_t(Flags >> 8),
      uint8_t(Flags)};
  SHA256 Hash;
  Hash.update(ArrayRef<uint8_t>(F, sizeof(F)));
  std::array<uint8_t, 32> Digest = Hash.final();
  return (uint32_t(Digest[28]) << 24) | (uint32_t(Digest[29]) << 16) |
         (uint32_t(Digest[30]) << 8) | uint32_t(Digest[31]);
}

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

// One 8-byte record of a `.mcs251.bit` section (BIT-OBJECT-CONTRACT.md §3).
// The symbol association is carried by exactly one R_MCS251_BIT_REF at
// record base + 4 and is resolved after symbol loading.
struct BitRecord {
  uint32_t Offset = 0;        // Record base inside the bit metadata section.
  uint32_t Kind = 0;          // 1 = definition, 2 = fixed reference.
  uint32_t InitValue = 0;     // 0 or 1 (kind 1 only).
  uint32_t SymIndex = 0;      // Symbol table index of the association.
  InputSymbol *Ref = nullptr; // Exact referenced symbol.
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
  // G8: the producer marked this section as a candidate for the EDATA window
  // (SHF_MCS251_EDATA_MOVABLE, v2 objects only).  The linker prefers the low
  // DSEG window and may migrate the whole section to [max(0x100,DsegStart),
  // --edata-end] only when that window cannot hold it.
  bool EDataMovable = false;
  // G13b: the producer marked this XSEG section as ONE logical all-zero
  // object larger than the 16-bit record limit (SHF_MCS251_XSEG_SPLIT, v2
  // objects only).  The linker places it as one contiguous range -- possibly
  // across a 64K window -- and synthesizes per-window clear-only v1
  // XDATA_INIT records for it.  Unmarked XSEG sections keep the frozen
  // single-window ruling (65535-byte cap, bank-jump placement).
  bool XsegSplit = false;
  // True only for sections lld itself created (vector slots, synthesized bit
  // XINIT): they are trusted owners and are exempt from input-side checks.
  bool Synthesized = false;
  // G11: the placement storage class mergePlacement() pinned onto a FIXED
  // Region section (PSC_*), or 0xff while unset/not a placement section.
  // classifySection deliberately leaves it unset: the NOTE lives in another
  // section of the same object, so only the merge stage can decide.
  uint8_t PlacementClass = 0xff;
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
  // W4 (design §4.2): the ELF header e_flags word.  The header split keeps
  // it because the v2 branch must cross-check Tag 4 against the object-
  // protocol byte, and the group stage must distinguish v1 from v2 inputs.
  uint32_t EFlags = 0;
  // W4 (design §4.2): a decoded `.mcs251.attributes` carrier was accepted.
  bool IsV2 = false;
  // W4: the decoded v2 identity.  The decoded record list -- never the raw
  // bytes -- is the interface: cross-object validation is field-by-field by
  // design, so no consumer may memcmp payloads or the descriptor.
  MCS251Attributes::Decoded V2Identity;
  // A3: at most one `.mcs251.isr` per object; presence triggers IRQ mode.
  bool HasIsrMeta = false;
  InputSection *MetaSection = nullptr;
  std::vector<IsrRecord> IsrRecords;
  // BT12: at most one `.mcs251.bit` per object.
  InputSection *BitSection = nullptr;
  std::vector<BitRecord> BitRecords;
  // BT14: at most one `.mcs251.bitprofile` per object; presence means this
  // object is the bit-aware CRT (see BitProfileRec above).
  InputSection *BitProfileSection = nullptr;
  // G11: at most one `.mcs251.placement` NOTE carrier per object; the parsed
  // records are structural-only until mergePlacement() runs.
  InputSection *PlacementSection = nullptr;
  std::vector<PlacementNoteRecord> PlacementRecords;
  // G11-C2: at most one `.mcs251.placement.names` association carrier per
  // object.  Presence makes every record association authoritative; absence
  // keeps the legacy boundary (owned recovers its principal symbol, bind is
  // refused).
  InputSection *PlacementNamesSection = nullptr;
  // P-4 (freeze 2026-09-14): the decoded Tag 28 signature table.  Present for
  // every v2 object (the codec rejects a v2 identity without it), so
  // HasSignatures distinguishes the pre-P4 object the freeze makes a hard
  // error.  The symbol association and cross-object checks run after the
  // symbol table is loaded (validateV2Identity only sees the section bytes).
  bool HasSignatures = false;
  MCS251Signatures::Table Signatures;
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
         : Type == R_MCS251_BIT_REF || Type == ELF::R_MCS251_NONE ? 0
                                                                  : 1;
}

// BIT-OBJECT-CONTRACT.md §4.2: an R_MCS251_BITADDR8 field is the 8-bit
// bit-address operand of a bit instruction, and it immediately follows that
// instruction's opcode byte (the frozen classic encodes are opcode + operand:
// setb/clr/cpl <bit>, mov c,<bit>, mov <bit>,c and jb/jnb/jbc <bit>,rel).
// This is the link-time legality boundary for a bit-address field: a bit
// relocation may only land on such a field, never on an arbitrary byte of an
// executable section (an opcode, an immediate, or any non-code section).
// Symbol identity being valid is not field-position validity.
static bool isBitFieldOpcode(uint8_t Op) {
  switch (Op) {
  case 0xD2: // setb bit
  case 0xC2: // clr bit
  case 0xB2: // cpl bit
  case 0x92: // mov bit,c
  case 0xA2: // mov c,bit
  case 0x20: // jb bit,rel
  case 0x30: // jnb bit,rel
  case 0x10: // jbc bit,rel
    return true;
  default:
    return false;
  }
}

// Exact encoded length of the MCS251 instruction starting at Off, mirroring
// the MC code emitter (MCTargetDesc/MCS251MCCodeEmitter.cpp) byte-for-byte.
// Returns 0 for an unknown opcode or a truncated variable-length form, which
// makes the whole byte stream undecodable.  The A5 source-mode escape only
// precedes the classic register-move forms (low nibble >= 6 without a native
// spelling), so an escaped instruction is exactly two bytes.
//
// This exists so a BITADDR8 field can be validated against real instruction
// *boundaries* after every relocation has been applied, not merely against the
// numeric value of the preceding byte: 75 D2 00 (D2 is an immediate) and
// 74 D2 00 (D2 is an opcode, not the field's opcode) must both be rejected.
static unsigned mcs251InstrLen(ArrayRef<uint8_t> B, size_t Off) {
  auto At = [&](size_t I) -> int {
    return I < B.size() ? int(B[I]) : -1;
  };
  const int Op = At(Off);
  if (Op < 0)
    return 0;
  if (Op == 0xA5) {
    const int O2 = At(Off + 1);
    if (O2 < 0)
      return 0;
    // mov a,rn / mov rn,a, classic forms E8-EF / F8-FF.
    if ((O2 >= 0xE8 && O2 <= 0xEF) || (O2 >= 0xF8 && O2 <= 0xFF))
      return 2;
    return 0;
  }
  switch (Op) {
  // Single-byte classics.
  case 0x13: // rrc a
  case 0x33: // rlc a
  case 0xC3: // clr c
  case 0xA4: // mul ab
  case 0xAA: // eret
  case 0x32: // reti
  case 0xB3: // cpl c
  case 0xD3: // setb c
  // XDATA channel (AS3): the classic MOVX @DPTR pair `movx a,@dptr` (E0) and
  // `movx @dptr,a` (F0), emitted by MCS251MCCodeEmitter.cpp as bare single
  // bytes (both have low nibble 0, so putOpcode adds no A5 escape).  A bit
  // module's XDATA accesses interleave with its bit instructions, so without
  // them the whole-section decode stops on any such byte.
  case 0xE0: // movx a,@dptr
  case 0xF0: // movx @dptr,a
    return 1;
  // Opcode + one specifier/operand byte.
  case 0x0E: // sra8/sra16
  case 0x1E: // srl8/srl16
  case 0x3E: // sll8/sll16
  case 0x2C: // add8rr
  case 0x2D: // add16rr
  case 0x2F: // add32rr
  case 0x4C: // or8rr / or8a
  case 0x4D: // or16rr
  case 0x5C: // and8rr
  case 0x5D: // and16rr
  case 0x6C: // xor8rr
  case 0x6D: // xor16rr
  case 0x9C: // sub8rr
  case 0x9D: // sub16rr
  case 0x9F: // sub32rr
  case 0x7C: // mov8rr / mov8a / mov8ra (r>=8 forms)
  case 0x7D: // mov16rr
  case 0x7F: // mov32rr / setfp / restoresp
  case 0xBC: // cmp8rr
  case 0xBD: // cmp16rr
  case 0xBF: // cmp32rr
  case 0x92: // mov bit,c
  case 0xA2: // mov c,bit
  case 0xB2: // cpl bit
  case 0xC2: // clr bit
  case 0xD2: // setb bit
  case 0x74: // mov a,#imm8
  case 0x99: // ecall r
  case 0xAD: // mulw
  case 0xC0: // push psw
  case 0xD0: // pop psw
  case 0xCA: // pushfp / push dr
  case 0xDA: // popfp / pop dr
    return 2;
  // Opcode + rel8.
  case 0x08: // jsle
  case 0x18: // jsg
  case 0x28: // jle
  case 0x38: // jg
  case 0x40: // jc
  case 0x48: // jsl
  case 0x50: // jnc
  case 0x58: // jsge
  case 0x68: // je
  case 0x78: // jne
  case 0x80: // sjmp
    return 2;
  // Opcode + bit address + rel8.
  case 0x10: // jbc
  case 0x20: // jb
  case 0x30: // jnb
    return 3;
  // Opcode + specifier + disp16.
  case 0x09: // mov8rmD
  case 0x19: // mov8mrD
  case 0x29: // mov8rm displaced
  case 0x39: // mov8mr displaced
  case 0x69: // mov16rmS displaced
  case 0x79: // mov16mrS displaced
    return 4;
  // Opcode + addr24.
  case 0x8A: // ejmp
  case 0x9A: // ecall
    return 4;
  // incspx/decspx (specifier FC/FD/FE) or the zero-displacement 16-bit
  // stack short form (any other specifier).
  case 0x0B: // incspx / mov16rmS zero
  case 0x1B: // decspx / mov16mrS zero
  {
    const int S = At(Off + 1);
    if (S < 0)
      return 0;
    if (S == 0xFC || S == 0xFD || S == 0xFE)
      return 2;
    return 3;
  }
  // mov dr/wr immediate family: nibble 4 (16-bit imm) and 8 (dr, 16-bit
  // immediate) carry a second immediate word; the rest carry one byte.
  case 0x7E: {
    const int S = At(Off + 1);
    if (S < 0)
      return 0;
    const int Nib = S & 0x0F;
    return (Nib == 4 || Nib == 8) ? 4 : 3;
  }
  // mov hdr immediate (nibble C) carries a 16-bit immediate; the direct/B/
  // memory forms carry one byte.
  case 0x7A: {
    const int S = At(Off + 1);
    if (S < 0)
      return 0;
    return ((S & 0x0F) == 0x0C) ? 4 : 3;
  }
  // add/sub/and/or/xor 8/16 immediate: nibble 4 selects the 16-bit form.
  case 0x2E:
  case 0x9E:
  case 0x5E:
  case 0x4E:
  case 0x6E:
  case 0xBE: { // cmp8ri / cmp16ri
    const int S = At(Off + 1);
    if (S < 0)
      return 0;
    return ((S & 0x0F) == 4) ? 4 : 3;
  }
  default:
    return 0;
  }
}

// BT13/BT15: prove that every required BITADDR8 field is the bit-address
// operand of a real bit instruction, by decoding the *entire* byte stream from
// offset 0 to the section end with mcs251InstrLen.  This is used twice:
//
//   * on the producer's original input bytes, before any relocation is applied
//     (so a field that is not a bit operand in the source cannot be laundered
//     into one by a later relocation rewriting the bytes before it), and
//   * again on the final post-relocation image (so a relocation that rewrites
//     the field's own opcode cannot launder an invalid field the other way).
//
// Three independent rules, all mandatory:
//   1. every instruction must decode (unknown opcode => fail),
//   2. the stream must not end in a truncated instruction (e.g. a jb with a
//      missing rel8 byte, or any trailing partial instruction after the last
//      field),
//   3. a field must sit exactly one byte into a bit-opcode instruction.
// A field merely having a bit-looking preceding byte is not sufficient.
// `Fields` must be sorted ascending and unique.
static bool validateBitAddrStream(ArrayRef<uint8_t> B, StringRef SecName,
                                  const std::vector<size_t> &Fields,
                                  raw_ostream &Err) {
  size_t Next = 0;
  size_t Cursor = 0;
  while (Cursor < B.size()) {
    unsigned Len = mcs251InstrLen(B, Cursor);
    if (Len == 0)
      return fail(Err, "MCS251 bit: " + SecName +
                           " is not a decodable instruction stream at offset "
                           "0x" + Twine::utohexstr(Cursor));
    if (Cursor + Len > B.size())
      return fail(Err, "MCS251 bit: " + SecName +
                           " ends in a truncated instruction at offset 0x" +
                           Twine::utohexstr(Cursor));
    while (Next < Fields.size() && Fields[Next] < Cursor + Len) {
      const size_t Off = Fields[Next++];
      if (Off != Cursor + 1 || !isBitFieldOpcode(B[Cursor]))
        return fail(Err, "MCS251 bit: BITADDR8 field at 0x" +
                             Twine::utohexstr(Off) + " in " + SecName +
                             " is not the bit-address operand of a bit "
                             "instruction");
    }
    Cursor += Len;
  }
  if (Next != Fields.size())
    return fail(Err, "MCS251 bit: BITADDR8 field at 0x" +
                         Twine::utohexstr(Fields[Next]) + " in " + SecName +
                         " lies outside the section stream");
  return true;
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
  // G8: the producer's "this AS0 data section may be placed in EDATA" mark.
  // The bit is a placement capability, never an instruction to migrate: the
  // linker still tries the low window first and migrates only on failure.
  S.EDataMovable = (S.Flags & SHF_MCS251_EDATA_MOVABLE) != 0;
  S.IsLoadable = S.IsAlloc && !S.IsNobits && S.Size != 0;

  if (!S.IsAlloc)
    return true; // validateMetaSection() checks the exact supported set.

  // The mask names every flag a *placement* rule may see.  SHF_MCS251_EDATA_MOVABLE
  // is a capability bit carried by the section's own Region rule below, so it
  // must not be rejected here; likewise SHF_MCS251_XSEG_SPLIT (the G13b
  // logical->64K-object mark) and SHF_GNU_RETAIN (G11: the retain marker is
  // accepted for `.mcu.fixed.*` only, compared with the bit masked out).
  // Every other processor/OS bit stays unsupported.
  const uint64_t Common = ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_EXECINSTR |
                          SHF_MCS251_OVERLAY | SHF_MCS251_EDATA_MOVABLE |
                          SHF_MCS251_XSEG_SPLIT | ELF::SHF_GNU_RETAIN;
  if (S.Flags & ~Common)
    return fail(Err, "unsupported ALLOC section flags for " + N);
  // G11 F1(iii) belt: with SHF_GNU_RETAIN inside the mask, every non-fixed
  // carrier of the bit must be rejected by one uniform frozen message, never
  // by whatever exact-compare a later Region branch happens to perform (and
  // never silently, whatever flags that branch tolerates).  The acceptance
  // direction for fixed sections is NOT here: classifySection only shapes,
  // and mergePlacement() cross-checks the bit against NOTE flags.bit0.
  if ((S.Flags & ELF::SHF_GNU_RETAIN) && !N.starts_with(FixedSectionPrefix))
    return fail(Err, "section " + N + " carries SHF_GNU_RETAIN outside "
                         ".mcu.fixed.*");
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
  // X3: the XDATA initialization records.  A CODE-class ROM area exactly like
  // XINIT (same allocation, same boundary-symbol and flash-gate class) -- it
  // places, parses and validates.  G13b (D4): one bounded exception to the
  // historical "never synthesized" contract -- clear-only v1 records
  // synthesized from an input XSEG section carrying SHF_MCS251_XSEG_SPLIT
  // enter the linker-owned XDATA_INIT output and go through the same
  // structural, single-window, XSEG-containment and non-overlap validation
  // as input records (validateXDATAInit).
  if (N == ".mcs251.xdata_init" || N.starts_with(".mcs251.xdata_init.")) {
    if (S.Type != ELF::SHT_PROGBITS || S.Flags != ELF::SHF_ALLOC)
      return fail(Err, "invalid XDATA_INIT section " + N);
    S.Region = "XDATA_INIT";
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
    // G8: an ordinary AS0 writable NOBITS slice may additionally carry the
    // "may be placed in EDATA on low-window failure" capability bit.  The bit
    // is a v2 placement policy on the MCS251-named slices, so a v1 object (or
    // a generic `.data`/`.bss` name) carrying it is malformed rather than
    // silently tolerated -- keeping every pre-G8 accepted input byte-identical.
    const bool Markable = N.starts_with(".mcs251.");
    if (S.EDataMovable && (!Markable || !S.File ||
                           S.File->EFlags != ABI_FLAGS_V2))
      return fail(Err,
                  Markable
                      ? "DSEG section carries the EDATA-movable flag outside "
                        "its v2 contract: " + N
                      : "generic DSEG name carries the EDATA-movable flag "
                        "(only .mcs251.* slices may): " + N);
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE |
                    (S.EDataMovable ? SHF_MCS251_EDATA_MOVABLE : 0)))
      return fail(Err, "DSEG sections must be writable NOBITS: " + N);
    S.Region = "DSEG";
    return true;
  }
  // G8: the linker-assigned EDATA Region.  S1 does not emit this name (it
  // marks `.mcs251.dseg` slices and lets the failure-fallback re-layout move
  // them), but the Region exists in S0 so the window, the boundary symbols and
  // the XINIT destination whitelist are complete independently of the
  // producer.  A directly emitted slice is placed in EDATA unconditionally.
  if (N == ".mcs251.edata" || N.starts_with(".mcs251.EDATA.")) {
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE) ||
        !S.File || S.File->EFlags != ABI_FLAGS_V2)
      return fail(Err, "EDATA sections must be writable NOBITS in a v2 object: " +
                           N);
    S.Region = "EDATA";
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
    // G13b: an XSEG slice may carry the split capability (one logical
    // all-zero object larger than the 16-bit record limit).  The bit is a
    // v2 placement policy exactly like SHF_MCS251_EDATA_MOVABLE (D3, G8
    // mechanism): a v1 object carrying it is malformed rather than silently
    // tolerated, and the accepted shape stays bit-exact
    // ALLOC|WRITE[|SHF_MCS251_XSEG_SPLIT] NOBITS (SM3) -- any other flag
    // combination keeps the historical rejection.
    S.XsegSplit = (S.Flags & SHF_MCS251_XSEG_SPLIT) != 0;
    if (S.XsegSplit && (!S.File || S.File->EFlags != ABI_FLAGS_V2))
      return fail(Err, "XSEG section carries the XSEG-split flag outside "
                       "its v2 contract: " + N);
    if (S.Type != ELF::SHT_NOBITS ||
        S.Flags != (ELF::SHF_ALLOC | ELF::SHF_WRITE |
                    (S.XsegSplit ? SHF_MCS251_XSEG_SPLIT : 0)))
      return fail(Err, "XSEG must be writable NOBITS: " + N);
    S.Region = "XSEG";
    return true;
  }
  // G11 (design §3.3): the fixed-placement sections.  classifySection only
  // freezes the SHAPES the emitter may produce -- a CODE entity is PROGBITS
  // (functions add EXECINSTR, CODE-space const objects do not), a DATA/XDATA
  // entity is writable NOBITS -- and both may carry SHF_GNU_RETAIN (masked
  // out here; mergePlacement() asserts it against NOTE flags.bit0).  The
  // storage class itself lives in the NOTE record of the same object and is
  // pinned onto the section by mergePlacement().  OVERLAY / EDATA_MOVABLE /
  // XSEG_SPLIT stay rejected: a fixed entity is never an overlay group
  // member, never an EDATA migration candidate (F7) and never a split
  // logical object (§4.1 FIXED x XSEG_SPLIT isolation ruling).
  if (N.starts_with(FixedSectionPrefix)) {
    if (!S.File || S.File->EFlags != ABI_FLAGS_V2)
      return fail(Err, "fixed placement section requires a v2 object "
                       "identity: " + N);
    if (S.Flags & (SHF_MCS251_OVERLAY | SHF_MCS251_EDATA_MOVABLE |
                   SHF_MCS251_XSEG_SPLIT))
      return fail(Err, "unsupported ALLOC section flags for " + N);
    const uint64_t F = S.Flags & ~uint64_t(ELF::SHF_GNU_RETAIN);
    const bool CodeShape =
        S.Type == ELF::SHT_PROGBITS &&
        (F == ELF::SHF_ALLOC || F == (ELF::SHF_ALLOC | ELF::SHF_EXECINSTR));
    const bool DataShape = S.Type == ELF::SHT_NOBITS &&
                           F == (ELF::SHF_ALLOC | ELF::SHF_WRITE);
    if (!CodeShape && !DataShape)
      return fail(Err, "invalid flags or type for " + N);
    S.Region = "FIXED";
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
  // W4 (design §4.2): the v2 identity carrier.  Exact name only, the frozen
  // SHT_LOPROC-range type registered by the codec, no flags, align 1.
  // Whether the section is *allowed* is decided by the e_flags branch in
  // loadFile() (v2 requires exactly one, v1 must not carry one); this check
  // only freezes the shape.  The section loop's name-claim path re-checks
  // the full shape (type/flags plus the raw sh_addralign/entsize/link/info
  // words it still has in hand) unconditionally, so the generic index-0
  // SHT_NULL early return above cannot bypass it (A4W4-R1).
  if (N == MCS251Attributes::SectionName)
    return S.Type == MCS251Attributes::SectionType && S.Flags == 0 &&
               S.Align == 1 ||
           fail(Err, "malformed " + N);
  // A3.2: the ISR metadata section is whitelisted by its exact name only; no
  // `.mcs251.*` wildcard exists anywhere in the non-ALLOC whitelist.
  if (N == MCS251ISR::MetaSectionName)
    return S.Type == ELF::SHT_PROGBITS && S.Flags == 0 &&
               S.Align == MCS251ISR::MetaSectionAlignment ||
           fail(Err, "malformed " + MCS251ISR::MetaSectionName);
  // BT12: the bit-object metadata section, exact name only.
  if (N == BitObjectSectionName)
    return S.Type == ELF::SHT_PROGBITS && S.Flags == 0 && S.Align == 4 ||
           fail(Err, "malformed " + BitObjectSectionName);
  // BT14: the bit-aware CRT profile section is whitelisted by its exact name
  // only, with the frozen 16-byte record shape (the field values are decoded
  // after the section contents are loaded, next to the bit-record parser).
  // Whether the carrier is allowed to *use* the profile (it must be the CRT)
  // is decided later, after symbols resolve.  Unknown shapes stay loud
  // errors, never silently ignored.
  if (N == BitProfileSectionName)
    return S.Type == ELF::SHT_PROGBITS && S.Flags == 0 && S.Align == 4 &&
               S.Size == BitProfileRecordSize ||
           fail(Err, "malformed " + BitProfileSectionName +
                         ": expected a non-ALLOC 16-byte PROGBITS record "
                         "with align 4");
  // G11: the placement NOTE carrier, exact name only, mirroring the
  // .note.mcs251.abi shape (SHT_NOTE, no flags, align 4).  Whether a v1
  // object may carry it is decided by the claim path in loadFile() (V1 EOL:
  // fail closed, PM ruling R-2026-09-16-1); the record decode and every
  // semantic association belong to mergePlacement().
  if (N == PlacementNoteSectionName)
    return S.Type == ELF::SHT_NOTE && S.Flags == 0 && S.Align == 4 ||
           fail(Err, "malformed " + PlacementNoteSectionName);
  // G11-C2 (design rev 8 §8.3): the record->symbol association carrier.  Same
  // frozen envelope shape as the placement NOTE (SHT_NOTE, no flags, align 4),
  // exact name only -- the whitelist still has no `.mcs251.*` wildcard, and
  // the D2 output-only `.positions` name stays rejected by its own earlier
  // rule.  Whether the carrier is allowed on this object (v2 identity, at most
  // one, sh_entsize/link/info 0) is decided by the claim path in loadFile();
  // the structure and every association belong to the decode below and to
  // mergePlacement().
  if (N == PlacementNamesNoteSectionName)
    return S.Type == ELF::SHT_NOTE && S.Flags == 0 && S.Align == 4 ||
           fail(Err, "malformed " + PlacementNamesNoteSectionName);
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

//===----------------------------------------------------------------------===//
// W4 (design §4.2): the v2 identity branch.
//
// e_flags == 0x1 keeps the byte-for-byte v1 note path above, untouched.
// e_flags == 0x102 selects this branch: exactly one `.mcs251.attributes`
// section, decoded through the strict codec (envelope, ULEB shortest form,
// duplicates, missing/unknown Critical tags, vendor/scope, Tag 4..27 value
// registration, Tag 11/13/24 internal agreement).  The linker adds only the
// object-level rules the codec cannot see: carrier cardinality, no v1 note,
// e_flags/tag-4 agreement and the set of memory model profiles this linker
// can actually place.  Any other e_flags word is rejected at the header
// check; there is no fallback between the two identity forms, and the v1
// branch rejects a carrier rather than ignoring it.
//===----------------------------------------------------------------------===//

// Extract one MIX atom from an already schema-validated Tag 24 record.  The
// codec has pinned the shape (exactly two U32 atoms of length 4), so this is
// a read of decoded structure, never a byte-blob comparison.  Returns false
// only for a shape the codec would have rejected.
static bool mixAtomValue(const MCS251Attributes::Record &R, unsigned Index,
                         uint32_t &Out) {
  StringRef V(reinterpret_cast<const char *>(R.Value.data()), R.Value.size());
  size_t Pos = 0;
  for (unsigned I = 0; I <= Index; ++I) {
    if (Pos >= V.size() || uint8_t(V[Pos]) != MCS251Attributes::VT_U32)
      return false;
    ++Pos;
    uint64_t Len = 0;
    unsigned Shift = 0;
    while (true) {
      if (Pos >= V.size() || Shift > 28)
        return false;
      uint8_t B = uint8_t(V[Pos++]);
      Len |= uint64_t(B & 0x7f) << Shift;
      if (!(B & 0x80))
        break;
      Shift += 7;
    }
    if (Len != 4 || V.size() - Pos < 4)
      return false;
    const uint8_t *P = reinterpret_cast<const uint8_t *>(V.data() + Pos);
    Out = (uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
          (uint32_t(P[2]) << 8) | uint32_t(P[3]);
    Pos += 4;
  }
  return true;
}

/// The memory model an object declares, as the (as0_pointer_bits,
/// default_placement) pair of D.5 / N.5.  Tag 11 and Tag 13 carry it
/// directly; Tag 24 repeats it as two MIX atoms and the codec has already
/// rejected any disagreement between the three, so this is a read of
/// validated structure.  A successful decode guarantees every input, so the
/// boolean only protects the function's own contract.
static bool v2ProfileOf(const MCS251Attributes::Decoded &D, uint32_t &AS0Bits,
                        uint32_t &Placement) {
  const MCS251Attributes::Record *Bits =
      D.find(MCS251Attributes::Tag_AS0PointerBits);
  const MCS251Attributes::Record *Place =
      D.find(MCS251Attributes::Tag_DefaultPlacement);
  const MCS251Attributes::Record *MM =
      D.find(MCS251Attributes::Tag_MemoryModelProfile);
  if (!Bits || !Place || !MM)
    return false;
  uint32_t Atom0 = 0, Atom1 = 0;
  if (!mixAtomValue(*MM, 0, Atom0) || !mixAtomValue(*MM, 1, Atom1))
    return false;
  AS0Bits = Bits->Scalar;
  Placement = Place->Scalar;
  return Atom0 == AS0Bits && Atom1 == Placement;
}

/// The set of memory models this linker accepts in a v2 object.  This is the
/// emission set A4 registered (32-bit AS0 Small/XSmall) and nothing else:
/// 16-bit AS0 is refused by the retained Tiny/XTiny object gate on the
/// compiler side, and Large (ExternalData placement) has no slot-placement
/// implementation in this slice, so accepting it would claim support for a
/// profile the toolchain cannot produce or place.  A future registration
/// must be added here deliberately, not inferred.
static bool isSupportedV2Profile(uint32_t AS0Bits, uint32_t Placement) {
  return AS0Bits == MCS251Attributes::AS0PointerBits32 &&
         (Placement == MCS251Attributes::Placement_InternalMovable ||
          Placement == MCS251Attributes::Placement_InternalExtended);
}

/// Validate one v2 object's identity carrier.  The strict codec (W1) has
/// already enforced the envelope, the record encoding, the Critical/tag
/// schema, the ruled scalar values, the A4-registered values and the
/// Tag 11/13/24 agreement; this adds the object-level rules the codec cannot
/// see from inside a section: carrier cardinality, exclusivity against the
/// v1 note, e_flags/tag-4 agreement and profile support.
static bool validateV2Identity(InputFile &F, const InputSection *Attrs,
                               raw_ostream &Err) {
  if (!Attrs)
    return fail(Err, F.Path + ": MCS251 v2 object requires exactly one " +
                         MCS251Attributes::SectionName);
  // Design §3.2/§4.2: a v2 object must not also carry the v1 note, and it
  // must not be repaired from one.  The two identities are exclusive.
  for (const auto &S : F.Sections)
    if (S->Name == ".note.mcs251.abi")
      return fail(Err, F.Path + ": MCS251 v2 object must not carry the v1 "
                           ".note.mcs251.abi note");

  // The strict codec is the single decode path (W1): envelope, vendor/scope,
  // shortest-form ULEB, duplicate/missing/unknown-Critical tags, per-type
  // schema and the A4-registered value set.
  const ArrayRef<uint8_t> D(Attrs->Data);
  if (llvm::Error E =
          MCS251Attributes::decode(StringRef(reinterpret_cast<const char *>(
                                               D.data()),
                                               D.size()),
                                   /*IsBigEndian=*/true, F.V2Identity))
    return fail(Err, F.Path + ": " + toString(std::move(E)));

  // P-4 (freeze 2026-09-14): every v2 object must carry Tag 28.  The codec
  // already decodes the value strictly (version, reserved bits, record
  // length, role combinations, bitmap tail, ret domain, name_off, empty
  // name, duplicate name, trailing blob, call_abi generation), so this stage
  // only lifts the decoded table into the file for the later symbol and
  // cross-object passes.
  if (!F.V2Identity.HasSignatures)
    return fail(Err, F.Path + ": MCS251 v2 object is missing required " +
                         MCS251Attributes::tagName(
                             MCS251Attributes::Tag_FunctionSignatures) +
                         " (Tag 28); objects produced before P-4 must be "
                         "rebuilt with the new toolchain");
  F.HasSignatures = true;
  F.Signatures = F.V2Identity.Signatures;

  // Design §4.2: Tag 4 and the header must agree.  Both are pinned to the
  // registered protocol version; this check makes the agreement explicit and
  // local instead of relying on two separate equality checks.
  const MCS251Attributes::Record *Proto =
      F.V2Identity.find(MCS251Attributes::Tag_ObjectProtocolVersion);
  if (!Proto || (F.EFlags & EF_ABI_MASK) != Proto->Scalar)
    return fail(Err, F.Path +
                         ": MCS251 v2 object_protocol_version disagrees with "
                         "the ELF header e_flags protocol byte");

  // Design §4.2 ("模型内其他约束逐字段核对") and §2.2: the memory model the
  // object declares must be one this linker can place.  The codec guarantees
  // the pair is one of the five frozen profiles, so this narrows the domain
  // rather than re-deriving it.
  uint32_t AS0Bits = 0, Placement = 0;
  if (!v2ProfileOf(F.V2Identity, AS0Bits, Placement))
    return fail(Err, F.Path + ": MCS251 v2 object has an inconsistent memory "
                         "model profile");
  if (!isSupportedV2Profile(AS0Bits, Placement))
    return fail(Err, F.Path + ": MCS251 v2 object declares memory model "
                         "profile (as0_pointer_bits=" +
                         Twine(AS0Bits) + ", default_placement=" +
                         Twine(Placement) +
                         "), which this linker does not support; the "
                         "registered v2 object profiles are the 32-bit "
                         "Small (1) and XSmall (8) models");

  F.IsV2 = true;
  return true;
}

/// One cross-object comparison row: a tag whose value must be identical in
/// every v2 object of a link.  These are the ABI/protocol compatibility
/// fields of design §4.2.  Tag 13 (default_placement) and the placement
/// atom of Tag 24 are deliberately absent: per D.5 those may differ between
/// objects as long as every object's own placement constraint is honoured.
struct V2ComparedTag {
  uint32_t Tag;
  const char *Name;
};

static const V2ComparedTag V2ComparedTags[] = {
    {MCS251Attributes::Tag_ObjectProtocolVersion, "object_protocol_version"},
    {MCS251Attributes::Tag_CallABIMajor, "call_abi_major"},
    {MCS251Attributes::Tag_CallABIMinor, "call_abi_minor"},
    {MCS251Attributes::Tag_RegisterParameterVariant,
     "register_parameter_variant"},
    {MCS251Attributes::Tag_GeneralRegisterSet, "general_register_set"},
    {MCS251Attributes::Tag_IntBits, "int_bits"},
    {MCS251Attributes::Tag_LongBits, "long_bits"},
    {MCS251Attributes::Tag_AS0PointerBits, "as0_pointer_bits"},
    {MCS251Attributes::Tag_ASLayoutVersion, "as_layout_version"},
    {MCS251Attributes::Tag_InitProtocolVersion, "init_protocol_version"},
    {MCS251Attributes::Tag_PlacementProtocolVersion,
     "placement_protocol_version"},
    {MCS251Attributes::Tag_StackContractVersion, "stack_contract_version"},
    {MCS251Attributes::Tag_FunctionContractVersion,
     "function_contract_version"},
    {MCS251Attributes::Tag_RequiredCapabilitiesLo,
     "required_capabilities_lo"},
    {MCS251Attributes::Tag_RequiredCapabilitiesHi,
     "required_capabilities_hi"},
    {MCS251Attributes::Tag_ABIOptions, "abi_options"},
    {MCS251Attributes::Tag_Reserved0, "reserved0"},
    {MCS251Attributes::Tag_Reserved1, "reserved1"},
    {MCS251Attributes::Tag_Reserved2, "reserved2"},
    {MCS251Attributes::Tag_CodeModelProfile, "code_model_profile"},
    {MCS251Attributes::Tag_CodePointerBits, "code_pointer_bits"},
    {MCS251Attributes::Tag_ObjectProtocolMinor, "object_protocol_minor"},
};

static bool loadFile(StringRef Path, InputFile &F, raw_ostream &Err) {
  F.Path = Path.str();
  auto MB = MemoryBuffer::getFile(Path);
  if (!MB)
    return fail(Err, Path + ": cannot read input");
  F.Buffer = std::move(*MB);
  // G11 P-3 (design §8): archive inputs -- classic `!<arch>\n` and thin
  // `!<thin>\n` alike -- are rejected with one frozen message before the
  // generic object-file sniff, because placement objects must be linked
  // directly (this linker has no archive member extraction, probe 2[C]).
  {
    StringRef Head = F.Buffer->getBuffer().take_front(8);
    if (Head == "!<arch>\n" || Head == "!<thin>\n")
      return fail(Err, "archive inputs are not supported: " + Path +
                           " (link placement objects directly)");
  }
  Expected<std::unique_ptr<ObjectFile>> Obj =
      ObjectFile::createObjectFile(F.Buffer->getMemBufferRef());
  if (!Obj)
    return fail(Err, Path + ": " + toString(Obj.takeError()));
  F.Object = std::move(*Obj);
  auto *ELFObj = dyn_cast<ELF32BEObjectFile>(F.Object.get());
  if (!ELFObj)
    return fail(Err, Path + ": expected ELF32 big-endian object");
  const auto &H = ELFObj->getELFFile().getHeader();
  // W4 (design §4): the identity branch is selected by e_flags alone.  0x1 is
  // the v1 note word, 0x102 the v2 attributes word; every other word is
  // rejected here, so no later stage can disagree about which identity form
  // an object claims.
  if (H.e_flags != ABI_FLAGS && H.e_flags != ABI_FLAGS_V2)
    return fail(Err, Path + ": invalid MCS251 ELF header");
  if (H.e_type != ELF::ET_REL || H.e_machine != EM_MCS251 ||
      H.e_version != ELF::EV_CURRENT)
    return fail(Err, Path + ": invalid MCS251 ELF header");
  F.EFlags = H.e_flags;
  // SPEC §3.1: strict identity and ET_REL structural checks.
  // G11-C integration note (2026-09-17): the generic ELF writer upgrades
  // EI_OSABI from NONE to ELFOSABI_GNU for any object whose sections carry
  // GNU-ABI markers -- SHF_GNU_RETAIN (the G11 retain carrier) does exactly
  // that -- so a legal retain placement object arrives as OSABI=GNU.  GNU
  // is therefore accepted CONDITIONALLY: the object must actually carry a
  // retain-flagged section (checked after the section loop), which keeps
  // the P2-4 frozen negative -- a GNU-marked object without any GNU
  // section -- rejected with this same message.  Every other byte of the
  // identity contract is unchanged (flagged for Alice's review).
  bool GnuOsabiPending = false;
  if (H.e_ident[ELF::EI_OSABI] != ELF::ELFOSABI_NONE) {
    if (H.e_ident[ELF::EI_OSABI] != ELF::ELFOSABI_GNU)
      return fail(Err, Path + ": invalid MCS251 ELF identity");
    GnuOsabiPending = true;
  }
  if (H.e_ident[ELF::EI_ABIVERSION] != 0 ||
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
  InputSection *V2AttrsSection = nullptr;
  bool SawGnuRetain = false; // G11-C: justifies a pending OSABI=GNU mark.
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
    // G11-D2 (design §3.1): the positioning carrier is output-only.  Reject
    // the exact name before any classification so neither the ALLOC nor the
    // non-ALLOC path can accept it under a generic message; the dedicated
    // diagnostic names the output-only contract.
    if (S->Name == PositionsNoteSectionName)
      return fail(Err, Path + ": " + PositionsNoteSectionName +
                           " is a final-output-only section and cannot appear "
                           "in an input object");
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
    if (S->Name == BitObjectSectionName) {
      // BT12: record-only, no entry size, no trailing padding, one per file.
      if (S->Size == 0)
        return fail(Err, Path + ": MCS251 bit: empty .mcs251.bit section");
      if (H.sh_entsize != 0)
        return fail(Err, Path + ": MCS251 bit: .mcs251.bit must have sh_entsize 0");
      if (S->Size % BitObjectRecordSize != 0)
        return fail(Err, Path + ": MCS251 bit: size must be a multiple of 8 "
                             "with no trailing padding");
      if (F.BitSection)
        return fail(Err, Path + ": MCS251 bit: at most one .mcs251.bit per object");
      F.BitSection = S.get();
    }
    if (S->Name == BitProfileSectionName) {
      // BT14: one profile per object; the header shape (non-ALLOC 16-byte
      // PROGBITS, align 4, entsize 0) is frozen here, and the field decode
      // runs right after this loop, once the section contents are loaded.
      // The record itself carries no relocation: the walker entry is
      // discovered by symbol name (the __mcs251_globals_init discovery
      // precedent), and the set-level "is this really the CRT" check runs
      // after resolveSymbols.
      if (H.sh_entsize != 0)
        return fail(Err, Path + ": MCS251 bitprofile: .mcs251.bitprofile "
                             "must have sh_entsize 0");
      if (F.BitProfileSection)
        return fail(Err, Path + ": MCS251 bitprofile: at most one "
                             ".mcs251.bitprofile per object");
      F.BitProfileSection = S.get();
    }
    if (S->Name == PlacementNoteSectionName) {
      // G11: one placement NOTE carrier per object.  V1 EOL (PM ruling
      // R-2026-09-16-1): a v1-contract module carrying the placement NOTE
      // fails closed -- no compatibility branch is written.  The record
      // decode runs after the symbol table is loaded (the structural pass
      // below); --print-input exercises it.
      if (F.EFlags != ABI_FLAGS_V2)
        return fail(Err, Path + ": " + PlacementNoteSectionName +
                             " requires a v2 object identity (the v1 memory "
                             "contract is EOL)");
      if (H.sh_entsize != 0 || H.sh_link != 0 || H.sh_info != 0)
        return fail(Err, Path + ": malformed " + PlacementNoteSectionName);
      if (F.PlacementSection)
        return fail(Err, Path + ": at most one " +
                             PlacementNoteSectionName + " per object");
      F.PlacementSection = S.get();
    }
    if (S->Name == PlacementNamesNoteSectionName) {
      // G11-C2: one association carrier per object, mirroring the placement
      // NOTE's own object-level rules.  The carrier only has meaning next to
      // a v1 placement table, and the v1 contract is EOL (PM ruling
      // R-2026-09-16-1), so it requires the same v2 identity as the table it
      // associates.  The structural decode runs after the record table is
      // parsed (so record indices can be range-checked).
      if (F.EFlags != ABI_FLAGS_V2)
        return fail(Err, Path + ": " + PlacementNamesNoteSectionName +
                             " requires a v2 object identity (the v1 memory "
                             "contract is EOL)");
      // G11-C2 MAJOR-1 fix: the claim path validates the *complete* frozen
      // carrier shape itself, unconditionally.  validateMetaSection() gives
      // the generic index-0 SHT_NULL header an early pass (the ELF null
      // section has no shape of its own to freeze), so a mutated object whose
      // section 0 both is SHT_NULL and carries the carrier name would reach
      // this branch without ever meeting the type/flags/align checks there.
      // Requiring the SHT_NOTE envelope and the raw sh_addralign word keeps
      // the carrier rules decoupled from that generic early return, exactly
      // like the A4W4-R1 precedent for `.mcs251.attributes`.
      //
      // Section index 0 is the ELF null section header and must stay SHT_NULL
      // (all-zero); a carrier parked there is a malformed ELF, not an
      // alternative spelling, so it is refused outright.  This also means the
      // four SHT_NULL shapes are rejected before any envelope decoding.
      if (S->Index == 0)
        return fail(Err, Path + ": " + PlacementNamesNoteSectionName +
                             " must not occupy the ELF null section index 0");
      if (S->Type != ELF::SHT_NOTE || S->Flags != 0)
        return fail(Err, Path + ": malformed " +
                             PlacementNamesNoteSectionName);
      // sh_addralign is checked on the *raw* header word: the usable field
      // S->Align is normalized to at least 1, so it cannot stand in for the
      // frozen raw value.  (A raw 0 does not become 4 through normalization --
      // max(1,0) is 1; the earlier gap was that this comparison was not
      // executed on the claim path at all.)
      if (H.sh_addralign != 4)
        return fail(Err, Path + ": " + PlacementNamesNoteSectionName +
                             " must have sh_addralign 4");
      if (H.sh_entsize != 0 || H.sh_link != 0 || H.sh_info != 0)
        return fail(Err, Path + ": malformed " +
                             PlacementNamesNoteSectionName);
      if (F.PlacementNamesSection)
        return fail(Err, Path + ": at most one " +
                             PlacementNamesNoteSectionName + " per object");
      F.PlacementNamesSection = S.get();
    }
    if (S->Name == MCS251Attributes::SectionName) {
      // A4W4-R1 fix: the claim path validates the *complete* frozen carrier
      // shape itself, unconditionally.  validateMetaSection() gives the
      // generic index-0 SHT_NULL header an early pass (the ELF null section
      // has no shape of its own to freeze), so a mutated object whose
      // section 0 both is SHT_NULL and carries the carrier name would reach
      // the codec without ever meeting the type/flags checks there.  Requiring
      // the registered type and zero flags on this path keeps the carrier
      // rules decoupled from that generic early return.
      if (S->Type != MCS251Attributes::SectionType || S->Flags != 0)
        return fail(Err, Path + ": malformed " + MCS251Attributes::SectionName);
      // W4 (design §3.1/§4.2): the carrier's remaining frozen shdr fields.
      // sh_entsize/link/info are 0; a second carrier is always malformed --
      // v2 requires exactly one and v1 must carry none.
      //
      // sh_addralign is checked here on the *raw* header word rather than
      // through S->Align: that field is the section's usable alignment
      // (normalized to at least 1), so a raw 0 would be silently accepted by
      // an `Align == 1` test even though the frozen layout says align 1.  A
      // conforming producer can only write 1, so require exactly that.
      if (H.sh_addralign != 1)
        return fail(Err, Path + ": " + MCS251Attributes::SectionName +
                             " must have sh_addralign 1");
      if (H.sh_entsize != 0 || H.sh_link != 0 || H.sh_info != 0)
        return fail(Err, Path + ": malformed " + MCS251Attributes::SectionName);
      if (V2AttrsSection)
        return fail(Err, Path + ": at most one " +
                             MCS251Attributes::SectionName + " per object");
      V2AttrsSection = S.get();
    }
    // G11: a `.mcu.fixed.*` section legitimately carries the entity's
    // alignment (the emitter emits sh_addralign == the placement align, e.g.
    // 4 for an aligned function); the layout-time check is `A % align == 0`
    // in layoutFixed*, not this generic ALLOC rule.
    if (S->IsAlloc && S->Align != 1 &&
        !StringRef(S->Name).starts_with(FixedSectionPrefix))
      return fail(Err, Path + ": ALLOC section alignment must be 1: " + S->Name);
    if (!S->IsNobits) {
      Expected<ArrayRef<uint8_t>> Contents = ELF.getSectionContents(H);
      if (!Contents) {
        consumeError(Contents.takeError());
        return fail(Err, Path + ": truncated section " + S->Name);
      }
      S->Data.assign(Contents->begin(), Contents->end());
    }
    SawGnuRetain |= (H.sh_flags & ELF::SHF_GNU_RETAIN) != 0;
    F.Sections[I] = std::move(S);
  }
  // G11-C: an OSABI=GNU object must justify the mark (see the identity
  // note above); classifySection already confines SHF_GNU_RETAIN to
  // `.mcu.fixed.*`, so this is exactly "the object carries a retain
  // placement section".
  if (GnuOsabiPending && !SawGnuRetain)
    return fail(Err, Path + ": invalid MCS251 ELF identity");
  // W4 (design §4): the two identity forms are exclusive and neither is
  // repaired from the other.  e_flags picks the branch and the branch is
  // total: a v2 object must carry exactly one decoded carrier, a v1 object
  // must carry the frozen 52-byte note and no carrier at all.  There is no
  // "carrier present, treat as v1" and no "note present, treat as v2" path.
  if (F.EFlags == ABI_FLAGS_V2) {
    if (!validateV2Identity(F, V2AttrsSection, Err))
      return false;
  } else {
    if (V2AttrsSection)
      return fail(Err, Path + ": v1 object must not carry " +
                           MCS251Attributes::SectionName);
    if (!validateNote(F, Err))
      return false;
  }

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
        // G1: only the 109 legal slots are user-assignable; reserved, system
        // and out-of-profile numbers (including FFFF) are rejected.  The
        // bound comes from the shared profile so it cannot drift.
        if (R.Slot == MCS251ISR::NoSlot || !MCS251ISR::isLegalISRSlot(R.Slot))
          return fail(Err, Path + ": MCS251 ISR: vector is not a legal slot "
                             "in profile 0-" +
                             Twine(MCS251ISR::ISRVectorMaxSlot));
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

  // BT14: decode the bit-profile record fields now that the section contents
  // are loaded (the claim path above only froze the header shape, so
  // --print-input exercises this decode).  Every field is a frozen constant;
  // unknown values fail closed and name the field.
  if (F.BitProfileSection) {
    const ArrayRef<uint8_t> P(F.BitProfileSection->Data);
    if (P.size() != BitProfileRecordSize)
      return fail(Err, Path + ": MCS251 bitprofile: section must be exactly "
                           "16 bytes");
    if (read16BE(P, BitProfileRec::Version) != BitProfileVersion)
      return fail(Err, Path + ": MCS251 bitprofile: unsupported protocol "
                           "version");
    if (read16BE(P, BitProfileRec::Size) != BitProfileSizeWord)
      return fail(Err, Path + ": MCS251 bitprofile: unsupported record size");
    if (P[BitProfileRec::Kind] != BitProfileKind)
      return fail(Err, Path + ": MCS251 bitprofile: unknown profile kind");
    if (P[BitProfileRec::Strategy] != BitProfileStrategyMaskRMW)
      return fail(Err, Path + ": MCS251 bitprofile: unsupported init "
                           "strategy");
    if (read16BE(P, BitProfileRec::PoolBase) != BitWindowBase ||
        read16BE(P, BitProfileRec::PoolSize) != BitByteCount ||
        read16BE(P, BitProfileRec::WindowBits) != BitCount)
      return fail(Err, Path + ": MCS251 bitprofile: profile window does not "
                           "match the 0x20..0x2f bit pool");
    if (read16BE(P, BitProfileRec::EntryKind) != BitProfileEntryWalker)
      return fail(Err, Path + ": MCS251 bitprofile: unsupported entry kind");
    if (read16BE(P, BitProfileRec::Reserved) != 0)
      return fail(Err, Path + ": MCS251 bitprofile: reserved fields must be "
                           "zero");
  }

  // BT12: parse the fixed 8-byte bit-object records. Field-level structure is
  // validated here (so --print-input exercises it); the symbol association and
  // the definition/reference payload checks need resolved symbols and are done
  // in the RELA loop below and in buildBitIdentities().
  if (F.BitSection) {
    const InputSection &M = *F.BitSection;
    const ArrayRef<uint8_t> B(M.Data);
    for (uint32_t Off = 0; M.Size != 0 && Off <= M.Size - BitObjectRecordSize;
         Off += BitObjectRecordSize) {
      BitRecord R;
      R.Offset = Off;
      if (B[Off + BitRec::Version] != BitRecordVersion)
        return fail(Err, Path + ": MCS251 bit: unsupported record version");
      R.Kind = B[Off + BitRec::Kind];
      if (R.Kind != BitKindDefinition && R.Kind != BitKindReference)
        return fail(Err, Path + ": MCS251 bit: unknown record kind");
      R.InitValue = B[Off + BitRec::InitValue];
      if (R.InitValue > 1)
        return fail(Err, Path + ": MCS251 bit: init_value must be 0 or 1");
      if (B[Off + BitRec::Capabilities] != BitCapabilities)
        return fail(Err, Path + ": MCS251 bit: unsupported capabilities");
      if (read32BE(B, Off + BitRec::SymbolRef) != 0)
        return fail(Err, Path + ": MCS251 bit: symbol_reference must be zero; "
                             "the association is carried by the bit RELA");
      F.BitRecords.push_back(R);
    }
  }

  // G11: decode the placement NOTE records (structural pass only; the
  // section<->record<->symbol association is mergePlacement's).  Everything
  // here fails closed with the frozen `malformed placement NOTE` wording:
  // envelope, canonical record_size arithmetic, unknown encodings, unknown
  // flag bits, 24-bit address, power-of-two align, bind-record retain,
  // zero-size bind objects (P-4) and non-zero bind function sizes.
  if (F.PlacementSection) {
    const InputSection &P = *F.PlacementSection;
    const ArrayRef<uint8_t> B(P.Data);
    auto Bad = [&](const Twine &Reason) {
      return fail(Err, "malformed placement NOTE in " + Path + ": " +
                           Reason.str());
    };
    if (B.size() < 20) // 12-byte header + the 8-byte name storage.
      return Bad("truncated note header");
    if (read32BE(B, 0) != PlacementNoteNameSize ||
        memcmp(B.data() + 12, "MCS251\0", 7) != 0)
      return Bad("note name is not MCS251\\0 with namesz 7");
    if (B[19] != 0)
      return Bad("note name padding is not zero");
    if (read32BE(B, 8) != PlacementNoteType)
      return Bad("unknown placement note type");
    const uint32_t DescSz = read32BE(B, 4);
    // The name occupies 8 storage bytes (7 + one pad), so the record table
    // starts at 20 and descsz covers exactly the rest of the section.
    if (DescSz != B.size() - 20)
      return Bad("descsz does not cover the record table");
    size_t Off = 20;
    while (Off != B.size()) {
      if (B.size() - Off < 29) // 4 + 25 fixed payload bytes minimum.
        return Bad("truncated record at offset " + Twine(Off));
      const uint32_t RecSize = read32BE(B, Off);
      PlacementNoteRecord R;
      R.Offset = Off;
      R.Index = static_cast<uint32_t>(F.PlacementRecords.size());
      const uint8_t *P4 = B.data() + Off + 4;
      const uint8_t Ver = P4[0];
      R.StorageClass = P4[1];
      R.Entity = P4[2];
      R.Ownership = P4[3];
      R.Address = read32BE(B, Off + 8);
      R.Size = read32BE(B, Off + 12);
      R.Align = read32BE(B, Off + 16);
      R.Flags = read32BE(B, Off + 20);
      R.LayoutHash = read32BE(B, Off + 24);
      const uint8_t StableLen = B[Off + 28];
      if (B.size() - Off - 29 < StableLen)
        return Bad("truncated stable symbol at offset " + Twine(Off));
      R.Stable.assign(reinterpret_cast<const char *>(B.data() + Off + 29),
                      StableLen);
      if (Ver != PlacementSchemaVersion)
        return Bad("unsupported schema version " + Twine(unsigned(Ver)));
      if (R.StorageClass > PSC_CODE)
        return Bad("unknown storage_class " + Twine(unsigned(R.StorageClass)));
      if (R.Entity > PE_FUNCTION)
        return Bad("unknown entity " + Twine(unsigned(R.Entity)));
      if (R.Ownership > PO_BIND)
        return Bad("unknown ownership " + Twine(unsigned(R.Ownership)));
      if (R.Address > 0xffffff)
        return Bad("address 0x" + Twine::utohexstr(R.Address) +
                   " exceeds the 24-bit space");
      if (R.Flags & ~(PlacementFlagRetain | PlacementFlagNoInit))
        return Bad("unknown flags 0x" + Twine::utohexstr(R.Flags));
      if (R.Ownership == PO_BIND && R.Flags != 0)
        // N7 ruling R-2026-09-17-1 (option A): the owned record is the
        // single authority for the flags of a group, so a legal bind record
        // carries all-zero known flags -- retain AND noinit alike.
        return Bad("bind record for " + R.Stable +
                   " carries non-zero flags");
      if (R.Ownership == PO_BIND && R.Entity == PE_OBJECT && R.Size == 0)
        return Bad("bind object " + R.Stable + " has size 0");
      if (R.Ownership == PO_BIND && R.Entity == PE_FUNCTION && R.Size != 0)
        return Bad("bind function " + R.Stable + " has a non-zero size");
      if (R.Align == 0 || (R.Align & (R.Align - 1)) != 0 || R.Align > 0x1000000)
        return Bad("align " + Twine(R.Align) + " is not a power of two");
      if (StableLen == 0)
        return Bad("empty stable symbol");
      // Canonical record arithmetic: the payload after the record_size word
      // is 25 fixed bytes + stable_len + padding to the next 4-byte multiple
      // (G11-B review [建议]: non-canonical zero padding is malformed).
      const uint32_t Canonical =
          (25u + StableLen + 3u) & ~uint32_t(3);
      if (RecSize != Canonical)
        return Bad("record_size " + Twine(RecSize) + " is not the canonical "
                   "alignTo(25+stable_len,4) value " + Twine(Canonical));
      // B1 (review 2026-09-17): validate the FULL record span before the
      // cursor moves.  Passing the fixed-field and stable-length checks is
      // not enough: the canonical record_size includes the zero padding, and
      // a last record truncated inside its padding would otherwise advance
      // Off past B.size(), underflow the next `B.size() - Off` (unsigned)
      // and parse fields outside the section.  The comparison is written as
      // a subtraction so it can never overflow.
      if (B.size() - Off < size_t(4) + RecSize)
        return Bad("truncated record at offset " + Twine(Off) +
                   " (record_size " + Twine(RecSize) +
                   " does not fit the remaining NOTE data)");
      // B1: the canonical padding after the stable symbol must be all zero
      // (the span check above proved these bytes exist).
      for (uint32_t I = 25 + StableLen; I != RecSize; ++I)
        if (B[Off + 4 + I] != 0)
          return Bad("record at offset " + Twine(Off) +
                     " has non-zero padding");
      if (StringRef(R.Stable).contains('\0'))
        return Bad("stable symbol contains a NUL");
      F.PlacementRecords.push_back(std::move(R));
      Off += 4 + RecSize;
    }
  }

  // G11-C2 (design rev 8 §8.3): decode the `.mcs251.placement.names`
  // association carrier and pin each entry onto its physical v1 record.  The
  // carrier is accepted by its exact name only; every structural violation is
  // a link error naming the object and the offending entry, never a skip:
  // envelope (namesz 7, name "MCS251\0", zero name padding), type==2,
  // association_version==1, descsz covering exactly the entry table, entry
  // bounds, non-empty NUL-free names, canonical zero padding, record indices
  // in range and unique, and exactly one entry per v1 record.  The caller
  // (mergePlacement) then binds each name to the record's real symbol.
  if (F.PlacementNamesSection) {
    const InputSection &P = *F.PlacementNamesSection;
    const ArrayRef<uint8_t> B(P.Data);
    auto Bad = [&](const Twine &Reason) {
      return fail(Err, "malformed placement names NOTE in " + Path + ": " +
                           Reason.str());
    };
    // The carrier only describes a v1 table, so a carrier without one has
    // nothing to associate and is rejected rather than silently ignored.
    if (!F.PlacementSection)
      return Bad("the object carries no " + PlacementNoteSectionName.str() +
                 " to associate");
    if (B.size() < 20) // 12-byte header + the 8-byte name storage.
      return Bad("truncated note header");
    if (read32BE(B, 0) != PlacementNoteNameSize ||
        memcmp(B.data() + 12, "MCS251\0", 7) != 0)
      return Bad("note name is not MCS251\\0 with namesz 7");
    if (B[19] != 0)
      return Bad("note name padding is not zero");
    if (read32BE(B, 8) != PlacementNamesNoteType)
      return Bad("unknown association note type " +
                 Twine(read32BE(B, 8)) + " (expected 2)");
    const uint32_t DescSz = read32BE(B, 4);
    // The name occupies 8 storage bytes (7 + one pad), so the entry table
    // starts at 20 and descsz covers exactly the rest of the section.
    if (DescSz != B.size() - 20)
      return Bad("descsz does not cover the association table");
    if (DescSz < 8)
      return Bad("truncated association header");
    if (read32BE(B, 20) != PlacementNamesVersion)
      return Bad("unsupported association_version " +
                 Twine(read32BE(B, 20)));
    const uint32_t Count = read32BE(B, 24);
    size_t Off = 28;
    std::vector<bool> Seen(F.PlacementRecords.size(), false);
    for (uint32_t I = 0; I != Count; ++I) {
      // Every read is bounds-checked against the section before use, and the
      // cursor advances by an amount that was proved to fit, so no unsigned
      // subtraction can underflow.
      if (B.size() - Off < 8)
        return Bad("truncated association entry " + Twine(I));
      const uint32_t RecIdx = read32BE(B, Off);
      const uint32_t NameLen = read32BE(B, Off + 4);
      Off += 8;
      if (uint64_t(Off) + NameLen > B.size())
        return Bad("association entry " + Twine(I) +
                   " name out of bounds");
      if (NameLen == 0)
        return Bad("association entry " + Twine(I) + " has an empty name");
      StringRef Name(reinterpret_cast<const char *>(B.data() + Off), NameLen);
      if (Name.contains('\0'))
        return Bad("association entry " + Twine(I) + " name contains a NUL");
      if (RecIdx >= F.PlacementRecords.size())
        return Bad("association entry " + Twine(I) +
                   " placement_record_index " + Twine(RecIdx) +
                   " is out of range (" +
                   Twine(F.PlacementRecords.size()) + " v1 records)");
      if (Seen[RecIdx])
        return Bad("duplicate association for placement record index " +
                   Twine(RecIdx));
      Seen[RecIdx] = true;
      Off += NameLen;
      // Canonical padding: alignTo(8 + len, 4) -- the two u32 fields are
      // already a 4-byte multiple, so this is alignTo(len,4) measured from
      // the entry base.  Zero bytes are required.
      const uint32_t Pad = (4u - (NameLen & 3u)) & 3u;
      if (B.size() - Off < Pad)
        return Bad("association entry " + Twine(I) +
                   " padding out of bounds");
      for (uint32_t K = 0; K != Pad; ++K)
        if (B[Off + K] != 0)
          return Bad("association entry " + Twine(I) +
                     " padding is not zero");
      Off += Pad;
      F.PlacementRecords[RecIdx].HasAssoc = true;
      F.PlacementRecords[RecIdx].AssocName = Name.str();
    }
    if (Off != B.size())
      return Bad("trailing bytes after the association table");
    // Exactly one entry per v1 record (the writer emits one per record; a
    // missing entry is as malformed as a duplicate).
    for (uint32_t I = 0; I != F.PlacementRecords.size(); ++I)
      if (!Seen[I])
        return Bad("missing association for placement record index " +
                   Twine(I));
  }

  unsigned MetaRelaCount = 0;
  unsigned BitRelaCount = 0;
  // BT13/BT15: BITADDR8 field offsets per executable section, collected while
  // parsing the RELA tables; decoded against the pre-relocation bytes once the
  // whole section's relocations are known.
  std::map<InputSection *, std::vector<uint32_t>> BitFieldsAtLoad;
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
    if (TS == F.BitSection) {
      // BT12: the bit-object association RELA, under the frozen exact name.
      ++BitRelaCount;
      if (F.Sections[I]->Name != ".rela.mcs251.bit")
        return fail(Err, Path + ": MCS251 bit: bit RELA must be named "
                             ".rela.mcs251.bit");
      if (Relocs->size() != F.BitRecords.size())
        return fail(Err, Path + ": MCS251 bit: metadata needs exactly one "
                             "BIT_REF relocation per record");
      std::vector<bool> Covered(F.BitRecords.size(), false);
      for (const ELF32BE::Rela &RelaEntry : *Relocs) {
        Relocation R{static_cast<uint32_t>(RelaEntry.r_offset),
                     RelaEntry.getType(false), RelaEntry.getSymbol(false),
                     static_cast<int32_t>(RelaEntry.r_addend)};
        if (R.Type != R_MCS251_BIT_REF)
          return fail(Err, Path + ": MCS251 bit: only R_MCS251_BIT_REF is "
                             "allowed in the bit RELA");
        if (R.Addend != 0)
          return fail(Err, Path + ": MCS251 bit: BIT_REF addend must be zero");
        if (R.Sym >= F.Symbols.size())
          return fail(Err, Path + ": relocation symbol index out of range");
        // r_offset = record base + 4; subtraction-style bound first.
        if (R.Offset < BitRec::SymbolRef)
          return fail(Err, Path + ": MCS251 bit: BIT_REF offset must be "
                             "record base + 4");
        const uint32_t Into = R.Offset - BitRec::SymbolRef;
        if (Into % BitObjectRecordSize != 0 ||
            Into / BitObjectRecordSize >= F.BitRecords.size())
          return fail(Err, Path + ": MCS251 bit: BIT_REF offset must be "
                             "record base + 4");
        const uint32_t Rec = Into / BitObjectRecordSize;
        if (Covered[Rec])
          return fail(Err, Path + ": MCS251 bit: each record carries exactly "
                             "one BIT_REF relocation");
        Covered[Rec] = true;
        const InputSymbol &Sym = F.Symbols[R.Sym];
        if (Sym.Name.empty() || Sym.Type != ELF::STT_OBJECT)
          return fail(Err, Path + ": MCS251 bit: BIT_REF must name an "
                             "STT_OBJECT symbol (never a section+addend fold)");
        F.BitRecords[Rec].SymIndex = R.Sym;
      }
      for (size_t Rec = 0; Rec != Covered.size(); ++Rec)
        if (!Covered[Rec])
          return fail(Err, Path + ": MCS251 bit: record " + Twine(Rec) +
                             " has no BIT_REF association");
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
      if (R.Type == R_MCS251_BIT_REF)
        return fail(Err, Path + ": MCS251 bit: R_MCS251_BIT_REF is only "
                           "allowed in the bit metadata RELA");
      uint32_t Width = relocWidth(R.Type);
      if ((R.Type > ELF::R_MCS251_J11 && R.Type != R_MCS251_BITADDR8) ||
          R.Offset > TS->Size || Width > TS->Size - R.Offset)
        return fail(Err, Path + ": relocation offset/type out of range");
      if (R.Type == R_MCS251_BITADDR8) {
        if (R.Addend != 0)
          return fail(Err, Path + ": MCS251 bit: BITADDR8 addend must be zero");
        // BT13/BT15: the field must be the bit-address operand of a bit
        // instruction in an executable PROGBITS section, and the producer must
        // have zero-filled it (BIT-OBJECT-CONTRACT.md §4.2).  The *position*
        // proof is not a single-byte peek: the whole section is decoded below,
        // on these original (pre-relocation) bytes, so a field that is not a
        // bit operand in the producer's output cannot be laundered into one by
        // a later relocation rewriting the bytes before it.  Symbol identity is
        // validated later; target legitimacy never authorises a write location.
        if (!TS->IsCode || TS->Type != ELF::SHT_PROGBITS)
          return fail(Err, Path + ": MCS251 bit: BITADDR8 field must live in an "
                             "executable PROGBITS section, not " + TS->Name);
        const ArrayRef<uint8_t> TB(TS->Data);
        if (R.Offset >= TB.size())
          return fail(Err, Path + ": relocation offset/type out of range");
        if (TB[R.Offset] != 0)
          return fail(Err, Path + ": MCS251 bit: BITADDR8 placeholder byte at "
                             "offset 0x" + Twine::utohexstr(R.Offset) +
                             " in " + TS->Name + " must be zero");
        BitFieldsAtLoad[TS].push_back(R.Offset);
      }
      TS->RelocIndex = I;
      TS->Relocs.push_back(R);
    }
  }
  // BT13/BT15: prove every BITADDR8 field is the bit-address operand of a bit
  // instruction in the *producer's original* byte stream, before any
  // relocation is applied.  The final post-relocation image is re-checked
  // separately in validateBitAddrFields(); both steps are required, because a
  // later relocation may rewrite an earlier byte (laundering an invalid field
  // into a valid-looking one) while the field itself stays put.
  for (auto &E : BitFieldsAtLoad) {
    InputSection *S = E.first;
    std::vector<size_t> Offs(E.second.begin(), E.second.end());
    llvm::sort(Offs);
    Offs.erase(std::unique(Offs.begin(), Offs.end()), Offs.end());
    if (!validateBitAddrStream(ArrayRef<uint8_t>(S->Data), S->Name, Offs, Err))
      return false;
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
  if (F.BitSection) {
    if (BitRelaCount == 0)
      return fail(Err, Path + ": MCS251 bit: records have no "
                         ".rela.mcs251.bit association section");
    if (BitRelaCount > 1)
      return fail(Err, Path + ": MCS251 bit: more than one RELA targets "
                         ".mcs251.bit");
  }
  if (!F.BitSection && BitRelaCount)
    return fail(Err, Path + ": MCS251 bit: bit RELA targets a non-bit section");
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

  // BT13: cross-TU bit slot allocation state.  BitOf maps a resolved bit
  // symbol to its allocated bit address (0..0x7F for objects, 0x00..0xFF for
  // fixed references).  BitOwner records, per backing byte, the bit mask owned
  // by automatic kind-1 objects and the value mask of bits initialized to 1.
  // BitPoolByte marks bytes already physically reserved by a BSEG_BYTES slice
  // (the CRT pool): they must not be reserved a second time.
  std::map<InputSymbol *, uint32_t> BitOf;
  std::map<uint32_t, uint8_t> BitMask;
  std::map<uint32_t, uint8_t> BitValue;
  std::set<uint32_t> BitPoolByte;
  std::set<uint32_t> BitExclusiveByte;
  // BT13 auditability: per backing byte, where its physical reservation came
  // from ("crt-pool" for a BSEG_BYTES slice, else the first reserving owner).
  std::map<uint32_t, std::string> BitByteOrigin;
  // BT13 auditability: RAM bytes owned by a fixed reference (no automatic bit)
  // and their owner names, so the map can report owner/init policy for a
  // fixed-only byte instead of omitting it.  BitInputXInitDest records which
  // destination bytes an *input* XINIT record covers, so the user's own
  // initialization is distinguishable from "never initialized".
  std::map<uint32_t, std::string> BitFixedByte;
  std::map<uint32_t, std::string> BitInputXInitDest;
  // BT13/BT15: every applied BITADDR8 field (section, offset), revalidated
  // against instruction boundaries after all relocations are applied.
  std::set<std::pair<InputSection *, uint32_t>> BitAddrFields;

  uint32_t areaStart(StringRef Name, uint32_t Default) const;
  bool hasAreaStart(StringRef Name) const;
  bool validateIdentitySet();

  // P-4 (freeze 2026-09-14): the object-internal symbol association and the
  // cross-object consistency passes.  declared here (next to the other
  // per-set validators) because they run as a pair after resolveSymbols().
  bool validateFileSignatures(InputFile &F);
  bool validateSignatureSet();

  /// The set of names this object references through a CODE-target
  /// relocation (R_MCS251_24 / R_MCS251_J16 / R_MCS251_J11).  This is the
  /// freeze's "被函数重定位引用" boundary that distinguishes a NOTYPE
  /// function entry from NOTYPE data (a parameter slot, a stack/range
  /// marker, a bit handle), all of which are reached through the data-address
  /// channels R_MCS251_16 and MID8/LO8/HI8.
  static std::set<StringRef> functionReferencedNames(const InputFile &F);
  bool rejectInputVecs();
  bool resolveSymbols();
  bool buildBitIdentities();
  // BT14: set-level bit-profile rules.  At most one carrier in the link, and
  // the carrier must be the CRT (defines the __mcs251_bit_init walker in
  // executable code and owns the 16-byte BSEG_BYTES pool).  This is what
  // rejects the old CRT + new-profile mix and the duplicate-owner cases
  // (design §6.4 L13).
  bool validateBitProfileSet();

  /// The link-wide set of names reached through a CODE-target relocation
  /// (R_MCS251_24 / R_MCS251_J16 / R_MCS251_J11) in ANY input object.  Built
  /// once by validateSignatureSet's caller before the per-file signature
  /// checks, because the freeze's NOTYPE function-entry boundary is a
  /// property of the link (a callee may be referenced only by another
  /// object).
  std::set<StringRef> LinkFunctionReferenced;
  bool allocateBitSlots();
  // BT14: the link's single bit-profile carrier (the bit-aware CRT), or null.
  InputFile *BitProfileFile = nullptr;
  bool validateISRIdentitiesAndRegistrations();
  bool synthesizeIRQVectors();
  bool layout();
  bool checkFlashGate();
  bool validateIRQReservedRangesAndCRT();
  bool layoutCode();
  bool layoutData();
  bool reserveCode(uint32_t Start, uint32_t Size, StringRef What);
  bool tryAllocate(InputSection &S, uint32_t Lo, uint32_t Hi);
  bool allocate(InputSection &S, uint32_t Lo, uint32_t Hi);
  bool reserve(uint32_t Start, uint32_t Size, StringRef What);
  bool applyRelocations();
  // The P-6(b) write-counting core (P6Writes == nullptr in production).
  bool applyRelocationsImpl(
      std::map<std::pair<InputSection *, uint32_t>, unsigned> *P6Writes);
  bool validateBitAddrFields();
  bool applyVectorJumps();
  bool validateIRQFinalAssets();
  bool validateXInit();
  bool validateXDATAInit();
  void diagnoseIsrReentrancy(LinkerResult &Result);
  void buildMap(raw_ostream &Out) const;
  void collectSymbols(std::vector<OutputSymbol> &Out) const;
  void printInputs(raw_ostream &Out) const;
  // G11-D2 (design §6.2): read-only positioning snapshot, taken after every
  // layout, G8 retry, G13b synthesis and layout validation succeeded and
  // before the result image is moved out.  Never mutates addresses, layout
  // cursors, Synth or the allocation ledgers.  Returns false only for an
  // internal ordering bug (a fixed section that never got its class pinned).
  bool collectPositions(LinkerResult &Result);
  InputSymbol *findSymbol(InputFile &F, uint32_t Index);
  bool errorUndefined();

  // IRQ mode (A3.6): triggered by any input carrying `.mcs251.isr`.
  bool IrqMode = false;
  InputFile *CrtFile = nullptr;
  InputSymbol *DefaultSym = nullptr;
  InputSymbol *ResetSym = nullptr;
  std::set<InputSymbol *> IsrSymbols;    // Exact registered ISR identities.
  // Registered handler per legal slot.  Sized by the shared profile count and
  // zero-initialized, so a change to the profile can never leave a stale
  // literal here and a high slot is never an out-of-bounds access.
  std::array<InputSymbol *, MCS251ISR::ISRVectorCount> SlotSym{};
  std::vector<std::unique_ptr<InputSection>> OwnedSynth;
  std::vector<InputSection *> SynthSections;
  std::vector<std::pair<InputSection *, uint32_t>> SynthExpect;
  std::vector<std::pair<InputSection *, InputSymbol *>> VectorJumps;

  // G11 (design §2.2 N2 / §3.3): the placement-name table for entities with
  // no owned definition in the link (bind-only NOTE records and manifest-only
  // rows).  The KEY is the ELF symbol name -- never assumed equal to the
  // stable symbol (C++ mangling and asm-labels diverge; rev 5 ruling); the
  // stable identity is only carried for the report.  Owned<->bind groups
  // never enter this table (they resolve through Globals), and
  // validatePlacementResolution() rejects a name that collides with an
  // unplaced definition.
  struct PlacementNameEntry {
    uint32_t Address = 0;
    uint32_t Size = 0;
    uint8_t StorageClass = 0;
    uint8_t Entity = 0;
    std::string Stable;
    std::string File; // First source file, for diagnostics.
  };
  std::map<std::string, PlacementNameEntry> PlacementNames;

  // G11: the merged placement contract rows (LinkerResult::Placement),
  // produced by mergePlacement().
  std::vector<LinkerResult::PlacementRecord> PlacementRows;

  // G11: per-owned-FIXED-section placement facts pinned by mergePlacement()
  // (the NOTE record's address/align plus the entity symbol), consumed by
  // the three layoutFixed* passes and the CODE-window diagnostics.
  struct FixedPlacementInfo {
    uint32_t Address = 0;
    uint32_t Align = 1;
    const InputSymbol *Sym = nullptr;
    const InputFile *File = nullptr;
  };
  std::map<InputSection *, FixedPlacementInfo> FixedInfo;

  // N7 single decision point (PM ruling R-2026-09-17-1, option A: the owned
  // record is authoritative): the merged noinit bit of a group is the owned
  // record's value when one exists, and 0 otherwise.  No owned<->bind
  // equality comparison is performed (the rev-7 merge-table row is
  // replaced); any future policy change lands in this one function.  The
  // merged retain bit follows the same authority shape.
  static bool placementMergedFlag(bool GroupHasOwned, bool OwnedFlag) {
    return GroupHasOwned && OwnedFlag;
  }

  bool mergePlacement(LinkerResult &Result);
  bool validatePlacementResolution();
  bool validatePlacementSymbolNames();
  bool layoutFixedCode();
  bool layoutFixedData();
  bool layoutFixedXdata();
  // F9/F10/F11 shared target classification (design §2.2): PSC_* when the
  // target's storage class is decidable, -1 when unknown.
  int placementTargetClass(const InputSymbol *Target) const;
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
  // This set covers every boundary symbol layoutData() can synthesize.  Since
  // BT14-R1 some members are conditional (s_/l_BITINIT exist only for a
  // profile link or an explicit --area-start=BITINIT); the names stay
  // reserved in every link so a user definition can never alias a boundary
  // symbol that a profile link would inject.
  if (Name == "l_IRAM")
    return true;
  if (Name == "__mcs251_stack_base")
    return true;
  if (Name.starts_with("s_") || Name.starts_with("l_")) {
    StringRef Area = Name.substr(2);
    static const char *Areas[] = {
        "DSEG",       "EDATA",      "OSEG",        "ISEG",
        "SSEG",       "HOME",       "VECS",        "BOOT",
        "CSEG",       "XINIT",      "XDATA_INIT",  "BSEG_BYTES",
        "BIT_BANK",   "XSEG",       "REG_BANK_0",  "REG_BANK_1",
        "REG_BANK_2", "REG_BANK_3", "BITINIT"};
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
        // G11 (§3.3 insertion 1): a bind-only placement name carries its
        // address directly -- the table was fully built by mergePlacement()
        // before this loop, and groups with an owned definition never enter
        // it (they resolve through Globals above).
        if (It != Globals.end())
          S.Address = It->second->Address;
        else {
          auto PIt = PlacementNames.find(S.Name);
          if (PIt != PlacementNames.end())
            S.Address = PIt->second.Address;
        }
      }
  return true;
}

// BT13: resolve the bit-object records to exact symbol identities and validate
// the definition/reference payload against the loaded symbols. Runs after
// resolveSymbols() so global references have their final resolution.
bool Linker::buildBitIdentities() {
  for (auto &F : Files)
    for (BitRecord &R : F->BitRecords) {
      InputSymbol *IS = &F->Symbols[R.SymIndex];
      if (R.Kind == BitKindDefinition) {
        // A definition names a symbol defined by this same object's
        // `.mcs251.bit` record; a cross-TU use is a BITADDR8 reference, never
        // a definition record, so no global resolution applies here.
        R.Ref = IS;
        if (!IS->Defined || IS->Sec != F->BitSection)
          return fail(Err, F->Path + ": MCS251 bit: definition " + IS->Name +
                               " is not defined in this object's .mcs251.bit");
        if (IS->Type != ELF::STT_OBJECT || IS->Size != 1)
          return fail(Err, F->Path + ": MCS251 bit: definition " + IS->Name +
                               " must be an STT_OBJECT of size 1");
        // BIT-OBJECT-CONTRACT.md §3: a kind-1 symbol's st_value IS the byte
        // offset of its record.  Enforcing the exact identity makes the
        // record<->symbol association unambiguous: a symbol whose value names
        // a different record (or a second record naming the same symbol) is a
        // malformed object, not a second slot for one logical bit object.
        if (IS->Value != R.Offset)
          return fail(Err, F->Path + ": MCS251 bit: definition " + IS->Name +
                               " st_value 0x" + Twine::utohexstr(IS->Value) +
                               " does not name its record offset 0x" +
                               Twine::utohexstr(R.Offset));
      } else {
        // A fixed reference names a defined SHN_ABS object whose value is the
        // bit address; several aliases of one address are idempotent.  An
        // undefined global resolves to its single definition.
        InputSymbol *Def = IS;
        if (!IS->Defined && IS->Bind != ELF::STB_LOCAL) {
          auto It = Globals.find(IS->Name);
          if (It != Globals.end() && It->second->Defined)
            Def = It->second;
        }
        R.Ref = Def;
        if (!Def->Defined || Def->Section != ELF::SHN_ABS ||
            Def->Type != ELF::STT_OBJECT)
          return fail(Err, F->Path + ": MCS251 bit: fixed reference " +
                               IS->Name +
                               " must be a defined SHN_ABS STT_OBJECT");
        if (Def->Value > 0xff)
          return fail(Err, F->Path + ": MCS251 bit: fixed reference " +
                               IS->Name + " bit address 0x" +
                               Twine::utohexstr(Def->Value) + " exceeds 0xff");
      }
    }
  return true;
}

// BT14: the set-level bit-profile rules.  Exactly one carrier; the carrier is
// the bit CRT.  "Old CRT plus a new-profile object" is rejected here (the
// profile carrier is not the CRT: no walker, no pool), as is any second
// profile carrier.  Without a profile the link keeps the S1 semantics
// (owned 16B all-zero clear + synthesized whole-byte XINIT), byte-identical
// to the pre-BT14 links.
bool Linker::validateBitProfileSet() {
  BitProfileFile = nullptr;
  for (auto &F : Files) {
    if (!F->BitProfileSection)
      continue;
    if (BitProfileFile)
      return fail(Err, "MCS251 bitprofile: duplicate profile carrier: " +
                           BitProfileFile->Path + " and " + F->Path +
                           "; only one bit-aware CRT may own the window "
                           "initialization");
    BitProfileFile = F.get();
  }
  if (!BitProfileFile)
    return true;
  InputFile &Crt = *BitProfileFile;
  // The carrier must define the walker entry in executable code of its own:
  // a profile whose values can never be applied must not link.
  bool HasWalker = false;
  for (const InputSymbol &S : Crt.Symbols)
    if (S.Name == BitInitWalkerName && S.Defined && S.Sec &&
        S.Sec->IsCode && S.Sec->File == &Crt)
      HasWalker = true;
  if (!HasWalker)
    return fail(Err, "MCS251 bitprofile: the profile carrier " + Crt.Path +
                         " does not define " + BitInitWalkerName +
                         " in executable code; the bit table could never be "
                         "applied (old CRT + new-profile mixes are rejected "
                         "by this rule)");
  // The carrier must own the 16-byte physical pool: the profile declares
  // pool_base/pool_size for exactly this window, and a carrier that reserves
  // nothing is not the CRT that clears or preserves it.
  bool OwnsPool = false;
  for (const auto &S : Crt.Sections)
    if (S->Region == "BSEG_BYTES" && S->Size == BitByteCount)
      OwnsPool = true;
  if (!OwnsPool)
    return fail(Err, "MCS251 bitprofile: the profile carrier " + Crt.Path +
                         " does not own the 16-byte BSEG_BYTES pool");
  return true;
}

// W4 (design §4.2): the cross-object v2 identity rules.
//
// The per-object half already ran in loadFile(), so every file here is a
// well-formed object of one of the two identity forms.  What remains is the
// property of the set:
//
//  1. v1 and v2 objects must never be linked together (D.5 "v1 与 v2 默认拒绝
//     裸混链").  A bare mix is rejected outright; there is no bridge and no
//     flag that ignores an identity.
//  2. Every v2 object must declare the same ABI/protocol fields.  The
//     comparison is field-by-field on the decoded records -- never a memcmp
//     of the payload or the descriptor (D.5: "逐字段比较", DESIGN.md N.6) --
//     so a future object that legitimately differs in an unrelated byte
//     cannot be rejected by accident, and a differing value names the field.
//  3. default_placement (Tag 13) and the placement atom of Tag 24 are
//     deliberately excluded from that comparison: D.5 allows Small and
//     XSmall objects to be mixed as long as each object's own placement
//     constraint is honoured, and the linker places each input by its own
//     section flags.  as0_pointer_bits (Tag 11) IS compared, because mixing
//     two default pointer widths is the D.5 "默认指针 16/32 混链" case that
//     must be rejected.
//  4. Required capabilities are unioned across the link and every bit must be
//     one this linker implements.  A4 registers the empty set, so any
//     non-zero capability is a hard error today; the union form is what
//     matters, because a future capability must be supported by the linker
//     before an object requiring it may enter a link.
bool Linker::validateIdentitySet() {
  const InputFile *V1 = nullptr;
  const InputFile *V2 = nullptr;
  for (const auto &F : Files) {
    if (F->IsV2) {
      if (!V2)
        V2 = F.get();
      continue;
    }
    if (!V1)
      V1 = F.get();
  }
  if (V1 && V2)
    return fail(Err, V2->Path + ": cannot link a v2 identity object (" +
                         MCS251Attributes::SectionName +
                         ") with a v1 identity object (" +
                         V1->Path + ", .note.mcs251.abi)");
  if (!V2)
    return true;

  // The reference identity: the first v2 object in command-line order, so the
  // diagnostic always names a stable pair.
  const InputFile &Ref = *V2;
  for (const auto &F : Files) {
    if (!F->IsV2 || F.get() == V2)
      continue;
    for (const V2ComparedTag &T : V2ComparedTags) {
      const MCS251Attributes::Record *A = Ref.V2Identity.find(T.Tag);
      const MCS251Attributes::Record *B = F->V2Identity.find(T.Tag);
      // Compare the *effective* value.  Every required compared tag is
      // present in both objects (the codec rejects a missing required tag),
      // so absence can only mean an optional reserved word, whose registered
      // value is zero whether it is written out or omitted: presence is a
      // serialization choice, not an ABI statement, and must not be read as
      // a disagreement.
      const uint32_t AV = A ? A->Scalar : 0u;
      const uint32_t BV = B ? B->Scalar : 0u;
      // U32-valued compared tags carry their value in Scalar; the codec
      // rejects any other type for these tags, so Scalar is authoritative.
      if (AV != BV) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << F->Path << ": MCS251 v2 object " << T.Name << " is 0x"
           << format_hex_no_prefix(BV, 0) << ", but " << Ref.Path
           << " declares 0x" << format_hex_no_prefix(AV, 0)
           << "; objects of a v2 link must agree on every ABI/protocol "
              "field (default_placement may differ between Small and XSmall)";
        return fail(Err, OS.str());
      }
    }
  }

  // Capability union: every required bit, over every object, must be one this
  // linker implements.  Tag 18/19 are the only capability words in this
  // revision; the registered link-time support set is empty.
  uint32_t Lo = 0, Hi = 0;
  for (const auto &F : Files)
    if (F->IsV2) {
      const MCS251Attributes::Record *L =
          F->V2Identity.find(MCS251Attributes::Tag_RequiredCapabilitiesLo);
      const MCS251Attributes::Record *H =
          F->V2Identity.find(MCS251Attributes::Tag_RequiredCapabilitiesHi);
      Lo |= L ? L->Scalar : 0;
      Hi |= H ? H->Scalar : 0;
    }
  if ((Lo & ~SupportedCapabilitiesLo) || (Hi & ~SupportedCapabilitiesHi)) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    OS << "MCS251 v2 link requires capabilities 0x"
       << format_hex_no_prefix(Hi, 8) << format_hex_no_prefix(Lo, 8)
       << ", but this linker implements 0x"
       << format_hex_no_prefix(SupportedCapabilitiesHi, 8)
       << format_hex_no_prefix(SupportedCapabilitiesLo, 8)
       << "; a required capability must be supported before an object that "
          "needs it can be linked";
    return fail(Err, OS.str());
  }
  return true;
}

//===----------------------------------------------------------------------===//
// P-4 (freeze 2026-09-14): function-signature object-internal and cross-object
// validation.
//
// Three stages, exactly the freeze's reader split:
//   1. strict Tag 28 value decoder        -- inside MCS251Attributes::decode,
//      reached from validateV2Identity at load time;
//   2. object-internal symbol association -- this file, after the symbol table
//      is loaded (loadFile parses symbols before it returns);
//   3. cross-object consistency           -- validateSignatureSet(), next to
//      (and after) validateIdentitySet and before layout.
//===----------------------------------------------------------------------===//

/// Object-internal rule set: a role=definition record must name a definition
/// of THIS object (st_shndx != UND), identified as a function entry either by
/// STT_FUNC or -- because the MCS251 producer emits CRT entries as NOTYPE --
/// by a NOTYPE symbol that is actually referenced as a function.  A
/// role=declaration record may have no symbol-table entry at all (an
/// unreferenced or optimised-away declaration), but if an entry exists it must
/// be an undefined external function.
std::set<StringRef>
Linker::functionReferencedNames(const InputFile &F) {
  std::set<StringRef> Names;
  for (const auto &S : F.Sections)
    for (const Relocation &R : S->Relocs) {
      if (R.Sym >= F.Symbols.size())
        continue;
      const InputSymbol &IS = F.Symbols[R.Sym];
      if (IS.Name.empty())
        continue;
      // Function-entry references are the CODE-target channels:
      // R_MCS251_24 (the 24-bit code address used by `ecall`/EJMP and code
      // pointers) and the jump relocations R_MCS251_J16/J11.  The 16-bit
      // R_MCS251_16 and the MID8/LO8/HI8 byte trio address DATA (a range
      // boundary, a stack base, an XINIT/XSEG slot), so they must never let
      // a NOTYPE data symbol masquerade as a function entry.
      if (R.Type == ELF::R_MCS251_24 || R.Type == ELF::R_MCS251_J16 ||
          R.Type == ELF::R_MCS251_J11)
        Names.insert(IS.Name);
    }
  // G11-C2 (design rev 8 §8.3): a bind placement record with entity=function
  // is an EXPLICIT function declaration even when no code relocation
  // references it -- the association carrier names the undefined external
  // directly, and §8.3 states the carrier must exist "even without an
  // ordinary code reference".  Without this, the link-wide NOTYPE function
  // boundary would classify such a bind target as data and refuse the very
  // object the carrier was introduced for.
  for (const PlacementNoteRecord &R : F.PlacementRecords)
    if (R.Ownership == PO_BIND && R.Entity == PE_FUNCTION && R.HasAssoc)
      Names.insert(R.AssocName);
  return Names;
}

bool Linker::validateFileSignatures(InputFile &F) {
  if (!F.IsV2 || !F.HasSignatures)
    return true;

  // The freeze's NOTYPE boundary is "名字在签名域内且被函数重定位引用":
  // the name must be referenced as a code target.  The reference may live in
  // ANOTHER object (the CRT's `ecall _main` targets a `_main` defined in the
  // demo object), so the set is the link-wide union, not this file's own
  // relocations.
  const std::set<StringRef> &FunctionReferenced = LinkFunctionReferenced;

  for (const MCS251Signatures::Record &Rec : F.Signatures.Records) {
    const std::string &Name = Rec.Name;

    // Find every symbol-table entry with this exact name.  Duplicate
    // definitions are rejected by resolveSymbols; here we only need enough
    // evidence to classify the name.
    std::vector<const InputSymbol *> Entries;
    for (const InputSymbol &IS : F.Symbols)
      if (IS.Name == Name)
        Entries.push_back(&IS);

    if (MCS251Signatures::isDefinitionRole(Rec.Role)) {
      // A definition record must name a function definition in THIS object.
      bool FoundDef = false;
      bool SeenUndefined = false;
      bool SeenNonFunction = false;
      for (const InputSymbol *IS : Entries) {
        if (!IS->Defined) {
          SeenUndefined = true;
          continue;
        }
        if (IS->Type != ELF::STT_FUNC && IS->Type != ELF::STT_NOTYPE) {
          SeenNonFunction = true;
          continue;
        }
        if (IS->Type == ELF::STT_NOTYPE &&
            !FunctionReferenced.count(IS->Name)) {
          // A NOTYPE symbol that is never a code-relocation target in the
          // whole link is data (a stack/range boundary, a section marker)
          // and cannot be the function a definition record names.
          SeenNonFunction = true;
          continue;
        }
        FoundDef = true;
      }
      if (!FoundDef) {
        if (SeenUndefined)
          return fail(Err, F.Path + ": MCS251 signature '" + Name +
                               "' claims a definition, but the symbol is only "
                               "declared (undefined) in this object");
        if (SeenNonFunction)
          return fail(Err, F.Path + ": MCS251 signature '" + Name +
                               "' claims a definition, but the symbol is not a "
                               "function definition in this object");
        return fail(Err, F.Path + ": MCS251 signature '" + Name +
                             "' claims a definition, but this object has no "
                             "such symbol");
      }
    } else {
      // A declaration/reference record may name a name with no symbol-table
      // entry at all.  When an entry exists it must be an external undefined
      // FUNCTION: a definition here would be a mislabelled role, and a
      // non-function definition cannot carry a function signature.  The type
      // is checked as well as the binding: a `STB_GLOBAL + SHN_UNDEF +
      // STT_OBJECT` symbol is valid object input but is DATA, so a signature
      // that declares it as a function must be refused.  STT_FUNC is a
      // function, and STT_NOTYPE is one when the link references it through a
      // code-target relocation (the same boundary validateFileSignatures and
      // the coverage pass use) -- otherwise it is data too.
      for (const InputSymbol *IS : Entries) {
        if (!IS->Defined) {
          if (IS->Bind != ELF::STB_GLOBAL)
            return fail(Err, F.Path + ": MCS251 signature '" + Name +
                                 "' is declared, but its symbol is not an "
                                 "external (global) undefined symbol");
          const bool IsFunction =
              IS->Type == ELF::STT_FUNC ||
              (IS->Type == ELF::STT_NOTYPE &&
               LinkFunctionReferenced.count(IS->Name));
          if (!IsFunction)
            return fail(Err, F.Path + ": MCS251 signature '" + Name +
                                 "' is declared as a function, but its symbol "
                                 "is not a function type in this object");
          continue;
        }
        return fail(Err, F.Path + ": MCS251 signature '" + Name +
                             "' is a declaration/reference, but this object "
                             "defines the symbol");
      }
    }
  }
  return true;
}

/// The freeze's per-record comparison semantics applied across every v2
/// object of the link, plus the per-object coverage rule: each object must
/// carry a record for every external function definition and every
/// recognisable external function declaration/reference of its own; a record
/// in another object must not stand in for a missing one here.
bool Linker::validateSignatureSet() {
  const InputFile *V2 = nullptr;
  for (const auto &F : Files)
    if (F->IsV2) {
      V2 = F.get();
      break;
    }
  if (!V2)
    return true; // no v2 object: nothing to check.

  // Cross-object: for one name, every record found in the link must agree
  // under compareRecords().  compareRecords is deliberately NOT transitive:
  // the zero-parameter exemption (PM ruling 2026-09-15) accepts a differing
  // bit2 when BOTH sides have param_count 0, so the same name can carry
  // records A (`int f();`, bit2=1/count=0), B (a K&R definition with a real
  // parameter list, bit2=1/count=1) and C (`int f(void);`, bit2=0/count=0)
  // where A-B and A-C are compatible while B-C is a genuine conflict --
  // compatibility is a property of a PAIR, not of the name.  Keeping only
  // the first record as the comparison reference would never compare B
  // against C whenever an A-compatible record comes first, making the link
  // verdict depend on command-line order.  Group every record per name and
  // compare ALL pairs; same-name groups hold at most one record per v2
  // object (the decoder rejects duplicate names within a Tag 28), so the
  // O(k^2) sweep over each group is bounded by the object count.  Entries
  // are pushed in command-line order, so the first conflicting pair in that
  // order is the one diagnosed -- a stable pair, exactly like
  // validateIdentitySet.
  struct SigEntry {
    const MCS251Signatures::Record *Rec;
    const InputFile *File;
  };
  std::map<std::string, std::vector<SigEntry>> ByName;
  for (const auto &F : Files) {
    if (!F->IsV2)
      continue;
    for (const MCS251Signatures::Record &Rec : F->Signatures.Records)
      ByName[Rec.Name].push_back({&Rec, F.get()});
  }
  for (const auto &KV : ByName) {
    const std::vector<SigEntry> &Group = KV.second;
    for (size_t I = 0; I != Group.size(); ++I)
      for (size_t J = I + 1; J != Group.size(); ++J)
        if (llvm::Error E = MCS251Signatures::compareRecords(
                *Group[I].Rec, *Group[J].Rec,
                Group[I].File->Path + " vs " + Group[J].File->Path))
          return fail(Err, Group[J].File->Path + ": " +
                               toString(std::move(E)));
  }

  // Per-object coverage.  The record-count zero case is legal only when the
  // object's own external-function domain is empty.
  for (const auto &F : Files) {
    if (!F->IsV2)
      continue;
    std::set<std::string> Covered;
    for (const MCS251Signatures::Record &Rec : F->Signatures.Records)
      Covered.insert(Rec.Name);
    // Every STT_FUNC symbol is a function.  A STT_NOTYPE symbol is a function
    // exactly when the LINK references it through a code-target relocation
    // (the freeze's "被函数重定位引用"); this holds for a DEFINITION too, so
    // the test must not be restricted to undefined symbols.  The reference may
    // live in another object (the CRT's `ecall _main` targets a `_main`
    // defined in the demo object), hence the link-wide set -- otherwise a
    // defined NOTYPE function could hide behind another object's call.
    //
    // The binding test is the only name-based exemption: a local symbol is
    // outside the signature domain, while a `.L`-prefixed GLOBAL
    // STT_FUNC/function-referenced NOTYPE symbol (an explicit asm label) is
    // NOT, so it must not be skipped by prefix.
    for (const InputSymbol &IS : F->Symbols) {
      if (IS.Name.empty() || IS.Bind == ELF::STB_LOCAL)
        continue;
      const bool IsFunc = IS.Type == ELF::STT_FUNC;
      // A static parameter slot (`<callee>_PARM_<n>`) and a symbolic bit
      // handle are STT_NOTYPE but are DATA: they are addressed as memory /
      // bit-address operands, never as direct call targets, so they are not
      // in LinkFunctionReferenced and carry no signature requirement.
      const bool IsFunctionNotype =
          IS.Type == ELF::STT_NOTYPE && LinkFunctionReferenced.count(IS.Name);
      if (!IsFunc && !IsFunctionNotype)
        continue;
      if (!Covered.count(IS.Name))
        return fail(Err, F->Path + ": MCS251 v2 object has an external "
                             "function '" +
                             IS.Name +
                             "' with no function-signature record in its own "
                             ".mcs251.attributes Tag 28; another object's "
                             "record must not stand in for a missing one");
    }
  }
  return true;
}

// T07 step 7: the synthesized table is the only VECS in IRQ mode. Any input
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

//===----------------------------------------------------------------------===//
// G11 (design rev 7 §3.3, plus PM rulings R-2026-09-16-1 and
// R-2026-09-17-1): fixed placement -- merge, resolution, layout.
//
// mergePlacement() runs between validateIdentitySet() and resolveSymbols():
// every input it needs (per-file NOTE records, FIXED sections and their
// symbols, the manifest lines) already exists, and PlacementNames must be
// complete before resolveSymbols' second loop reads it.  The N4 carrier
// (.mcs251.placement.names) was decoded structurally in loadFile() and is
// consumed here: each record's association is bound to its real symbol (owned
// -> the fixed section's unique principal symbol; bind -> an undefined
// external of the same object), and a bind record WITHOUT a carrier is
// refused rather than guessed (design rev 8 §8.2/§8.3).
//===----------------------------------------------------------------------===//

// One manifest row (design §7): `place <stable> <A> [align=N] [size=N]
// [retain] [noinit] [bind] [data|xdata|code]`, `#` comments.  A row carries
// no entity field; a manifest-only row therefore classifies as an object
// (the fail-closed side of the F11 function gate).
struct ManifestPlacement {
  std::string Stable;
  uint32_t Address = 0;
  uint32_t Align = 1;
  uint32_t Size = 0;
  bool HasAlign = false, HasSize = false;
  bool Retain = false, NoInit = false, Bind = false;
  bool HasClass = false;
  uint8_t StorageClass = 0;
  // G11-D provenance (contract §4.2): the real manifest path and 1-based
  // physical line number this row was parsed from.
  std::string OriginPath;
  uint32_t OriginLine = 0;
};

// G11-D: provenance paths are lexically absolutized under the link working
// directory, never resolved through symlinks (contract §2 来源行粒度).
// G11-D review R2-6 (B8): `.`/`..` components are also removed lexically, so
// the report's `file` column uses the SAME normalization the Driver hands the
// verifier (`lexicalAbsolute` in Driver.cpp).  Without this, an input spelled
// `./r.o` produced provenance `/cwd/./r.o` while the verifier was told
// `/cwd/r.o`, and the source row could never be matched -- `./r.o` was
// rejected where `r.o` linked and verified cleanly.
static std::string lexicalAbsolutePath(StringRef P) {
  SmallString<256> Buf(P);
  if (sys::fs::make_absolute(Buf))
    return P.str();
  sys::path::remove_dots(Buf, /*remove_dot_dot=*/true);
  return Buf.str().str();
}

static bool parseManifestLine(StringRef Line, ManifestPlacement &Out,
                              raw_ostream &Err) {
  auto Bad = [&](const std::string &Sym) {
    return fail(Err, "malformed placement manifest entry for " + Sym);
  };
  Line = Line.trim();
  if (Line.empty() || Line.starts_with("#"))
    return true; // Comment / blank: not an entry.
  SmallVector<StringRef, 8> Toks;
  SplitString(Line, Toks, " \t");
  std::string Stable = Toks.size() > 1 ? Toks[1].str() : Line.str();
  if (Toks.size() < 3 || Toks[0] != "place")
    return Bad(Stable);
  Out.Stable = Toks[1].str();
  if (Out.Stable.empty())
    return Bad(Stable);
  if (Toks[2].getAsInteger(0, Out.Address) || Out.Address > 0xffffff)
    return Bad(Stable);
  for (unsigned I = 3; I < Toks.size(); ++I) {
    const StringRef T = Toks[I];
    if (T == "retain")
      Out.Retain = true;
    else if (T == "noinit")
      Out.NoInit = true;
    else if (T == "bind")
      Out.Bind = true;
    else if (T == "data" || T == "xdata" || T == "code") {
      Out.HasClass = true;
      Out.StorageClass = T == "data"   ? uint8_t(PSC_AS0_DATA)
                         : T == "xdata" ? uint8_t(PSC_XDATA)
                                        : uint8_t(PSC_CODE);
    } else if (T.starts_with("align=")) {
      if (T.substr(6).getAsInteger(0, Out.Align) || Out.Align == 0 ||
          (Out.Align & (Out.Align - 1)) != 0)
        return Bad(Stable);
      Out.HasAlign = true;
    } else if (T.starts_with("size=")) {
      if (T.substr(5).getAsInteger(0, Out.Size))
        return Bad(Stable);
      Out.HasSize = true;
    } else
      return Bad(Stable);
  }
  // N7 ruling R-2026-09-17-1 (manifest side): a [bind] row is a pure name
  // declaration -- combining it with [noinit] or [retain] is malformed.
  if (Out.Bind && (Out.NoInit || Out.Retain))
    return Bad(Stable);
  return true;
}

// The `section %name disagrees with placement NOTE for %sym` family: every
// structural/semantic disagreement between a FIXED section, its NOTE record
// and its entity symbol lands here (§2.2 consistency, main-symbol
// constraints, entity counting, owned span identity).
static bool placementDisagrees(raw_ostream &Err, StringRef SectionName,
                               const Twine &Sym) {
  return fail(Err, "section " + SectionName +
                       " disagrees with placement NOTE for " + Sym.str());
}

bool Linker::mergePlacement(LinkerResult &Result) {
  // ---- Stage 1: manifest rows ---------------------------------------- //
  std::vector<ManifestPlacement> Manifest;
  for (const LinkerConfig::ManifestEntry &ME : Config.PlacementManifest) {
    ManifestPlacement M;
    if (!parseManifestLine(ME.Text, M, Err))
      return false;
    if (!M.Stable.empty()) {
      M.OriginPath = ME.Path;
      M.OriginLine = ME.Line;
      Manifest.push_back(std::move(M));
    }
  }

  // ---- Stage 2: per-file owned section <-> record association --------- //
  struct GroupEntry {
    // Source provenance.
    std::string FileRef;
    InputFile *File = nullptr;
    // Fields (NOTE authoritative; manifest rows may leave class/size unset).
    uint8_t StorageClass = 0;
    uint8_t Entity = PE_OBJECT;
    uint8_t Ownership = PO_OWNED;
    uint32_t Address = 0;
    uint32_t Size = 0;
    uint32_t Align = 1;
    uint32_t Flags = 0;
    bool HasClass = false;
    bool HasSize = false;
    bool HasAlign = false;   // Manifest rows only.
    bool Retain = false;     // Manifest rows only.
    bool NoInit = false;     // Manifest rows only.
    bool IsManifest = false;
    std::string Stable;
    std::string SymName; // ELF symbol name (the PlacementNames key).
    InputSection *Sec = nullptr; // Owned entries only.
    // G11-D provenance: this entry's own source description (contract §4.2).
    // `SourcePath` is the lexically absolute input object / manifest path;
    // `FileRef` above stays the raw (diagnostic) spelling so existing
    // diagnostics keep their bytes.  `IsManifest` selects Index = physical
    // manifest line number; otherwise Index = the record's position in the
    // file's NOTE stream (from 0).
    std::string SourcePath;
    uint32_t SourceIndex = 0;
    // The physical input section this NOTE record's entity lives in; for a
    // bind record this is empty unless the same file also owns the entity.
    std::string SourceSection;
  };
  std::map<std::string, std::vector<GroupEntry>> Groups;
  // G11-C2 reverse uniqueness (design rev 8 §8.2): the stable group that
  // claimed each external (bind) ELF name.  Two DIFFERENT stable groups
  // claiming one external ELF name is a conflict at symbol-map fill time --
  // never a silent map overwrite or de-duplication.
  std::map<std::string, std::string> ExternalNameOwner;

  for (auto &F : Files) {
    // Bind records: the ELF name comes from the association carrier ONLY
    // (G11-C2 / design rev 8 §8.3).  The pre-C2 "discover the undefined
    // symbol whose name happens to be stable or "_"+stable" recovery is
    // exactly the identity assumption the carrier replaces, so an object
    // without one is refused rather than guessed.
    for (const PlacementNoteRecord &R : F->PlacementRecords) {
      GroupEntry E;
      E.File = F.get();
      E.FileRef = F->Path;
      E.SourcePath = lexicalAbsolutePath(F->Path);
      E.SourceIndex = R.Index;
      E.StorageClass = R.StorageClass;
      E.Entity = R.Entity;
      E.Ownership = R.Ownership;
      E.Address = R.Address;
      E.Size = R.Size;
      E.Align = R.Align;
      E.Flags = R.Flags;
      E.HasClass = true;
      E.HasSize = true;
      E.Stable = R.Stable;
      if (R.Ownership == PO_BIND) {
        if (!R.HasAssoc)
          // Legacy bind objects carry no association and must not be
          // guessed; the object has to be regenerated (design §8.3).
          return fail(Err, "missing placement name association for " +
                               R.Stable + " in " + F->Path +
                               " (recompile placement object)");
        // The associated name must be an UNDEFINED EXTERNAL symbol of THIS
        // object: bind reserves no storage, and the undefined global is how
        // the reference reaches the output symbol table.  Exactly one match
        // is required; the association may never be paired positionally.
        const InputSymbol *Carrier = nullptr;
        unsigned Hits = 0;
        for (const InputSymbol &S : F->Symbols)
          if (!S.Defined && S.Bind == ELF::STB_GLOBAL &&
              S.Name == R.AssocName) {
            Carrier = &S;
            ++Hits;
          }
        (void)Carrier;
        if (Hits != 1)
          return fail(Err, "malformed placement NOTE in " + F->Path +
                               ": bind record " + Twine(R.Index) +
                               " (stable '" + R.Stable +
                               "') associates ELF symbol '" + R.AssocName +
                               "' which is not an undefined external symbol "
                               "of this input");
        E.SymName = R.AssocName;
        // Reverse uniqueness: register the claim; a different stable already
        // owning this name is a conflict (the same stable repeating is not).
        auto Ins = ExternalNameOwner.emplace(R.AssocName, R.Stable);
        if (!Ins.second && Ins.first->second != R.Stable)
          return fail(Err, "conflicting placement for " + R.Stable +
                               ": external ELF symbol " + R.AssocName +
                               " is already claimed by stable " +
                               Ins.first->second);
      }
      Groups[R.Stable].push_back(std::move(E));
    }

    // Owned sections: every `.mcu.fixed.*` section of this file must pair
    // with exactly one owned record of this file, hold exactly one entity
    // symbol, and agree on shape, span and retain.  Records appearing twice
    // for one section are left to the group-level P-5 checks (duplicate
    // owned vs identity collision); a section with no record, or a record
    // with no section, is a disagreement.
    for (auto &SP : F->Sections) {
      InputSection *S = SP.get();
      if (S->Region != "FIXED")
        continue;
      const StringRef Stable =
          StringRef(S->Name).substr(FixedSectionPrefix.size());
      const PlacementNoteRecord *Rec = nullptr;
      unsigned OwnedRecordCount = 0;
      for (const PlacementNoteRecord &R : F->PlacementRecords)
        if (R.Stable == Stable && R.Ownership == PO_OWNED) {
          if (!OwnedRecordCount)
            Rec = &R; // Deterministic first record for the section facts.
          ++OwnedRecordCount;
        }
      if (OwnedRecordCount == 0)
        return placementDisagrees(Err, S->Name, Stable);
      // Entity counting (rev 6 frozen boundary): every defined STT_FUNC /
      // STT_OBJECT counts, whatever its st_size and binding; the count must
      // be exactly 1.  The auxiliary-symbol allowance is bounded (review
      // B4, 2026-09-17): a local STT_NOTYPE label is admitted only in the
      // LJTI/fixup shape -- no size, strictly inside the payload region
      // (a position label at offset 0 would alias the entity entry, and the
      // frozen LJTI labels always sit in the payload after it); a local
      // STT_SECTION symbol is admitted only as the CANONICAL section anchor
      // the ELF object writer emits -- empty name, offset 0, size 0, this
      // section as its subject.  A named, offset, or sized symbol is an
      // entity-shaped claim, never writer metadata, and fails closed.
      const InputSymbol *Main = nullptr;
      unsigned Entities = 0;
      for (const InputSymbol &Sym : F->Symbols)
        if (Sym.Defined && Sym.Sec == S) {
          if (Sym.Type == ELF::STT_FUNC || Sym.Type == ELF::STT_OBJECT) {
            ++Entities;
            Main = &Sym;
          } else if (Sym.Bind == ELF::STB_LOCAL &&
                     Sym.Type == ELF::STT_NOTYPE && Sym.Size == 0 &&
                     Sym.Value > 0 && Sym.Value < S->Size) {
            continue; // LJTI/fixup position label (no name/size contract).
          } else if (Sym.Bind == ELF::STB_LOCAL &&
                     Sym.Type == ELF::STT_SECTION && Sym.Size == 0 &&
                     Sym.Value == 0 && Sym.Name.empty()) {
            continue; // Canonical section anchor (structural ELF metadata).
          } else {
            return placementDisagrees(Err, S->Name, Sym.Name);
          }
        }
      if (Entities != 1 || !Main)
        return placementDisagrees(Err, S->Name, Stable);
      // G11-C2 (design rev 8 §8.2): when the association carrier is present
      // it is authoritative, never a hint -- every owned record naming this
      // section must associate the section's unique principal symbol's
      // actual ELF name.  Without a carrier the legacy boundary stands: the
      // principal symbol itself is the association (§8.3 old-object rule).
      for (const PlacementNoteRecord &R : F->PlacementRecords)
        if (R.Stable == Stable && R.Ownership == PO_OWNED && R.HasAssoc &&
            R.AssocName != Main->Name)
          return fail(Err, "placement name association mismatch for " +
                               R.Stable + " in " + F->Path +
                               ": associated ELF symbol '" + R.AssocName +
                               "' is not the principal symbol '" +
                               Main->Name + "' of section " + S->Name);
      // B4(iii): the NOTE entity must correspond to the main symbol's ELF
      // type -- a NOTE "function" over an STT_OBJECT (or the reverse) is a
      // producer contract violation, never a silently accepted alias.
      if ((Rec->Entity == PE_FUNCTION) != (Main->Type == ELF::STT_FUNC))
        return placementDisagrees(Err, S->Name, Main->Name);
      if (Main->Value != 0)
        return placementDisagrees(Err, S->Name, Main->Name);
      if (Rec->Entity == PE_OBJECT ? Main->Size != S->Size
                                   : Main->Size > S->Size)
        return placementDisagrees(Err, S->Name, Main->Name);
      // Entity span identity: NOTE.size == sh_size (the symbol-difference
      // fixup the emitter wrote; a function's st_size may be smaller).
      if (Rec->Size != S->Size)
        return placementDisagrees(Err, S->Name, Main->Name);
      // §2.2 storage-class consistency.
      const uint64_t FNoRetain = S->Flags & ~uint64_t(ELF::SHF_GNU_RETAIN);
      if (Rec->StorageClass == PSC_CODE) {
        if (S->Type != ELF::SHT_PROGBITS)
          return placementDisagrees(Err, S->Name, Main->Name);
      } else {
        if (S->Type != ELF::SHT_NOBITS ||
            FNoRetain != (ELF::SHF_ALLOC | ELF::SHF_WRITE) ||
            (S->Flags & ELF::SHF_EXECINSTR))
          return placementDisagrees(Err, S->Name, Main->Name);
      }
      // F1(ii) RETAIN double-direction assertion.
      const bool NoteRetain = (Rec->Flags & PlacementFlagRetain) != 0;
      const bool SecRetain = (S->Flags & ELF::SHF_GNU_RETAIN) != 0;
      if (NoteRetain && !SecRetain)
        return fail(Err, "fixed section " + S->Name +
                             " declares retain but lacks SHF_GNU_RETAIN");
      if (SecRetain && !NoteRetain)
        return fail(Err, "fixed section " + S->Name +
                             " carries SHF_GNU_RETAIN but placement NOTE "
                             "flags.bit0=0");
      // B3 (review 2026-09-17): the fixed section's own sh_addralign is a
      // second, independent alignment claim.  The emitter writes the same
      // entity alignment into the section header and the NOTE, so a
      // disagreement means the object carries two contradicting contracts
      // (a large sh_addralign was previously never cross-checked at all).
      // The comparison uses S->Align (sh_addralign normalized to at least
      // 1): the emitter can only write a real alignment, and a raw 0 is
      // unspecified-1 by the frozen layout, so a conforming producer is
      // accepted byte for byte.
      if (S->Align != Rec->Align)
        return placementDisagrees(Err, S->Name, Main->Name);
      // Pin the class (F9/F10/F11 consume it; F11 also needs IsCode on
      // CODE-class sections, EXECINSTR or not).
      S->PlacementClass = Rec->StorageClass;
      if (Rec->StorageClass == PSC_CODE)
        S->IsCode = true;
      FixedInfo[S] = {Rec->Address, Rec->Align, Main, F.get()};
      // Annotate EVERY owned NOTE entry of this file that names this
      // section.  B7 (review 2026-09-17): a duplicate record used to skip
      // the association entirely and leave SymName empty for this file,
      // manufacturing a bogus identity difference against other inputs
      // (base,other,base reported "conflicting" instead of the duplicate
      // verdict its identity set deserves).
      for (auto &E : Groups[Rec->Stable])
        if (E.File == F.get() && E.Ownership == PO_OWNED && !E.IsManifest) {
          E.Sec = S;
          E.SymName = Main->Name;
          E.SourceSection = S->Name;
        }
    }
    // An owned record whose derived section does not exist in this file.
    for (const PlacementNoteRecord &R : F->PlacementRecords)
      if (R.Ownership == PO_OWNED) {
        bool Paired = llvm::any_of(
            F->Sections, [&](const std::unique_ptr<InputSection> &SP) {
              return SP->Region == "FIXED" &&
                     StringRef(SP->Name).substr(FixedSectionPrefix.size()) ==
                         StringRef(R.Stable);
            });
        if (!Paired)
          return placementDisagrees(
              Err, std::string(".mcu.fixed.") + R.Stable, R.Stable);
      }
  }

  // Manifest rows join the stable groups.
  for (const ManifestPlacement &M : Manifest) {
    GroupEntry E;
    E.FileRef = "(manifest)";
    E.Stable = M.Stable;
    E.SymName = M.Stable; // Manifest-only fallback naming (rev 5 ruling).
    E.Address = M.Address;
    E.Align = M.HasAlign ? M.Align : 1;
    E.HasAlign = M.HasAlign;
    E.Retain = M.Retain;
    E.NoInit = M.NoInit;
    E.Size = M.Size;
    E.HasSize = M.HasSize;
    E.HasClass = M.HasClass;
    E.StorageClass = M.StorageClass;
    E.Entity = PE_OBJECT; // No entity field in the grammar.
    E.Ownership = M.Bind ? uint8_t(PO_BIND) : uint8_t(PO_OWNED);
    E.Flags = (M.Retain ? PlacementFlagRetain : 0) |
              (M.NoInit ? PlacementFlagNoInit : 0);
    E.IsManifest = true;
    E.SourcePath = M.OriginPath;
    E.SourceIndex = M.OriginLine;
    Groups[M.Stable].push_back(std::move(E));
  }

  // ---- Stage 3: per-group merge (P-5 order) --------------------------- //
  auto Conflict = [&](const GroupEntry &A, const GroupEntry &B) {
    return fail(Err, "conflicting placement for " + A.Stable + ": 0x" +
                         Twine::utohexstr(A.Address) + " (" + A.FileRef +
                         ") vs 0x" + Twine::utohexstr(B.Address) + " (" +
                         B.FileRef + ")");
  };
  auto ManifestClash = [&](const std::string &Sym, uint32_t A, uint32_t B) {
    return fail(Err, "conflicting placement constraints (NOTE vs manifest) "
                     "for " +
                         Sym + ": 0x" + Twine::utohexstr(A) + " vs 0x" +
                         Twine::utohexstr(B));
  };
  // B3 (review 2026-09-17): the merged align is a binding contract, not a
  // report annotation.  One wording serves the layout passes and this
  // merge-time check for the storage-less groups.
  auto AlignmentViolation = [&](StringRef Sym, uint32_t A, uint32_t Al) {
    return fail(Err, "fixed placement for " + Sym.str() + " at 0x" +
                         Twine::utohexstr(A) + " violates alignment " +
                         Twine(Al));
  };
  for (auto &G : Groups) {
    const std::vector<GroupEntry> &Ents = G.second;
    const std::string &Stable = G.first;
    // P-5(1)/(2), rebuilt per the review's B7: FIRST build the complete
    // owned identity set, THEN decide.  The identity-collision verdict is
    // taken over every owned pair before any duplicate-owned verdict, so
    // the diagnostic no longer depends on input order (base,base,other and
    // base,other,base both report the identity collision); only a group
    // whose owned identities are ALL equal is a duplicate record.
    std::vector<const GroupEntry *> Owned;
    for (const GroupEntry &E : Ents)
      if (E.Ownership == PO_OWNED && !E.IsManifest)
        Owned.push_back(&E);
    for (size_t I = 1; I < Owned.size(); ++I)
      if (Owned[0]->StorageClass != Owned[I]->StorageClass ||
          Owned[0]->Entity != Owned[I]->Entity ||
          Owned[0]->SymName != Owned[I]->SymName)
        return Conflict(*Owned[0], *Owned[I]);
    // G11-C2 (design rev 8 §8.2): within one stable group the external
    // owned/bind records must name ONE ELF symbol.  With the association
    // carrier the names are explicit, so a mismatch is a conflicting
    // placement -- never an alias, and never silently split across rows.
    if (!Owned.empty())
      for (const GroupEntry &E : Ents)
        if (!E.IsManifest && !E.SymName.empty() &&
            E.SymName != Owned[0]->SymName)
          return fail(Err, "conflicting placement for " + Stable +
                               ": the group's records name different ELF "
                               "entities");
    if (Owned.size() > 1)
      return fail(Err, "placement NOTE duplicate owned record for " + Stable +
                       " in " + Owned[0]->FileRef + " and " +
                       Owned[1]->FileRef);
    const GroupEntry *FirstOwned = Owned.empty() ? nullptr : Owned[0];
    // B2 (review 2026-09-17): the seed has THREE explicit sources -- the
    // owned NOTE record, the first bind NOTE record, or (when no NOTE names
    // this stable at all) the explicit manifest fields.  The old
    // unconditional "first non-manifest record" dereferenced find_if's
    // end() for a manifest-only group.
    const GroupEntry *Seed = FirstOwned;
    if (!Seed)
      for (const GroupEntry &E : Ents)
        if (!E.IsManifest) {
          Seed = &E; // First bind NOTE record.
          break;
        }
    const bool HasOwned = FirstOwned != nullptr;
    const bool ManifestOnly = Seed == nullptr;
    uint8_t MClass = 0, MEntity = PE_OBJECT;
    uint32_t MAddress = 0, MSize = 0, MAlign = 1;
    bool MNoInit = false, MRetain = false;
    if (!ManifestOnly) {
      MClass = Seed->StorageClass;
      MEntity = Seed->Entity;
      MAddress = Seed->Address;
      MSize = Seed->Size;
      MAlign = Seed->Align;
      MNoInit = (Seed->Flags & PlacementFlagNoInit) != 0;
      MRetain = (Seed->Flags & PlacementFlagRetain) != 0;
    } else {
      // Manifest-only group: the contract is built from the explicit
      // fields alone.  storage_class has no NOTE fallback here, so a group
      // with no explicit class is malformed (§7 缺字段语义: "两者皆无 →
      // 拒"); two explicit classes and two explicit sizes must agree among
      // themselves, and the align merge keeps the stricter value.
      const GroupEntry *ClassRow = nullptr;
      const GroupEntry *SizeRow = nullptr;
      for (const GroupEntry &E : Ents) {
        if (!E.IsManifest)
          continue;
        // Retain/noinit need an owned definition as their carrier; a
        // manifest-only group has none (§2.2 retain scope), and this covers
        // the seed row too (the merge loop below skips it).
        if (E.Retain || E.NoInit)
          return fail(Err, "malformed placement manifest entry for " + Stable);
        if (E.HasClass) {
          if (ClassRow && ClassRow->StorageClass != E.StorageClass)
            return ManifestClash(Stable, ClassRow->Address, E.Address);
          if (!ClassRow)
            ClassRow = &E;
        }
        if (E.HasSize) {
          if (SizeRow && SizeRow->Size != E.Size)
            return ManifestClash(Stable, SizeRow->Address, E.Address);
          if (!SizeRow)
            SizeRow = &E;
        }
        if (E.Address != Ents.front().Address)
          return ManifestClash(Stable, Ents.front().Address, E.Address);
      }
      if (!ClassRow)
        return fail(Err, "malformed placement manifest entry for " + Stable);
      Seed = &Ents.front();
      MClass = ClassRow->StorageClass;
      MEntity = PE_OBJECT; // The manifest grammar has no entity field.
      MAddress = Seed->Address;
      MSize = SizeRow ? SizeRow->Size : 0;
      MAlign = Seed->Align; // HasAlign ? Align : 1 on every row.
    }
    const bool HasNote = !ManifestOnly && !Seed->IsManifest;
    for (const GroupEntry &E : Ents) {
      if (&E == Seed || (E.Ownership == PO_OWNED && !E.IsManifest))
        continue; // Seed and (already-checked) owned duplicates.
      if (E.IsManifest) {
        // NOTE-vs-manifest: explicit fields must agree; absent fields
        // inherit.  bit1/bit0 follow the owned-authoritative policy (the
        // manifest's declaration is never an equality constraint).
        if (E.HasClass && E.StorageClass != MClass)
          return ManifestClash(Stable, MAddress, E.Address);
        if (E.Address != MAddress)
          return ManifestClash(Stable, MAddress, E.Address);
        if (E.HasSize && HasNote && E.Size != MSize)
          return ManifestClash(Stable, MAddress, E.Address);
        if (E.HasAlign && E.Align != 1 && MAlign != 1 && E.Align != MAlign)
          return ManifestClash(Stable, MAddress, E.Address);
        if (E.Align != 1)
          MAlign = std::max(MAlign, E.Align);
        if (!HasOwned && (E.Retain || E.NoInit))
          // Retain/noinit need an owned definition as their carrier; a
          // manifest-only group has none (§2.2 retain scope).
          return fail(Err, "malformed placement manifest entry for " +
                               Stable);
        continue;
      }
      // NOTE bind entries: field-semantic merge against the merged value.
      if (E.StorageClass != MClass || E.Entity != MEntity)
        return Conflict(*Seed, E);
      if (E.Address != MAddress)
        return Conflict(*Seed, E);
      // Size: a bind function's 0 means "no constraint"; a bind object's
      // sizeof must equal the merged size.
      if (E.Entity == PE_OBJECT && E.Size != MSize)
        return Conflict(*Seed, E);
      // Align: take the stricter value.
      if (E.Align != 1 && MAlign != 1 && E.Align != MAlign)
        return Conflict(*Seed, E);
      if (E.Align != 1)
        MAlign = std::max(MAlign, E.Align);
      // N7 option A: no owned<->bind bit1 comparison; the merged flags are
      // owned-authoritative (bind records carry all-zero flags by the
      // structural gate).
    }
    // Every manifest size/align declaration against a NOTE-backed group was
    // compared field by field in the loop above (the seed itself carries the
    // NOTE value); a manifest-only group was fully cross-checked in the
    // scan that built its merged contract (two explicit sizes must agree).

    const uint32_t MFlags =
        (placementMergedFlag(HasOwned, MNoInit) ? PlacementFlagNoInit : 0) |
        (placementMergedFlag(HasOwned, MRetain) ? PlacementFlagRetain : 0);
    // G11-D provenance (contract §4.2): describe EVERY original source that
    // produced this merged row.  The report writer collapses equal
    // (path, ELF name, stable) triples; the verifier re-derives the set from
    // the objects and cross-checks.  A manifest source borrows the group's
    // NOTE-associated ELF name (manifest-only groups fall back to stable).
    auto GroupElfName = [&]() -> std::string {
      for (const GroupEntry &E : Ents)
        if (!E.IsManifest && !E.SymName.empty())
          return E.SymName;
      return Stable;
    };
    auto BuildSources = [&](LinkerResult::PlacementRecord &Row) {
      const std::string GName = GroupElfName();
      for (const GroupEntry &E : Ents) {
        LinkerResult::PlacementRecord::Source S;
        S.Path = E.SourcePath;
        S.ElfName = E.IsManifest ? GName : E.SymName;
        S.Section = E.SourceSection;
        S.Kind = E.IsManifest
                     ? LinkerResult::PlacementRecord::Source::Manifest
                     : LinkerResult::PlacementRecord::Source::Note;
        S.Index = E.SourceIndex;
        Row.Sources.push_back(std::move(S));
      }
    };
    if (HasOwned) {
      // B3 (review 2026-09-17): the FINAL MERGED align is the executed
      // contract.  bind/manifest may have tightened it after stage 2 read
      // the owned NOTE's value, so validate A % MAlign here and back-write
      // the merged value into FixedInfo: the layout passes consume exactly
      // what this row reports (previously the report could claim align=4
      // while the layout only ever saw the owned align=1 and accepted A).
      InputSection *Sec = Seed->Sec;
      if (MAddress % MAlign != 0)
        return AlignmentViolation(Seed->SymName.empty() ? Stable
                                                        : Seed->SymName,
                                  MAddress, MAlign);
      if (Sec)
        FixedInfo[Sec].Align = MAlign;
      // Owned group: emit the merged contract row.  The section's shape,
      // span and symbol constraints were validated in stage 2.
      LinkerResult::PlacementRecord Row;
      Row.Stable = Stable;
      Row.Sym = Seed->SymName;
      Row.File = Seed->FileRef;
      Row.Section = Seed->Sec ? Seed->Sec->Name : "";
      Row.Address = MAddress;
      Row.Size = MSize;
      Row.Align = MAlign;
      Row.Flags = MFlags;
      Row.StorageClass = MClass;
      Row.Entity = MEntity;
      Row.Ownership = PO_OWNED;
      Row.BoundOnly = false;
      Row.LayoutHash = placementLayoutHash(MClass, MEntity, PO_OWNED, MAddress,
                                           MAlign, MFlags);
      BuildSources(Row);
      PlacementRows.push_back(std::move(Row));
    } else {
      // Bind-only / manifest-only group: enters PlacementNames (key = ELF
      // symbol name; the rev 5 naming ruling with the manifest stable
      // fallback) and never resolves through Globals.
      //
      // B3 (review 2026-09-17): a bind-only group reserves no storage, but
      // its merged A/align contract is still binding -- the bind target's
      // entry address must satisfy the very align the group merged to
      // (otherwise the report would assert an alignment no acceptance ever
      // checked).  This is the same unified check as the owned path.
      if (MAddress % MAlign != 0)
        return AlignmentViolation(Stable, MAddress, MAlign);
      PlacementNameEntry P;
      P.Address = MAddress;
      P.Size = MSize;
      P.StorageClass = MClass;
      P.Entity = MEntity;
      P.Stable = Stable;
      P.File = Seed->FileRef;
      std::string Key;
      for (const GroupEntry &E : Ents)
        if (!E.IsManifest) {
          Key = E.SymName; // The NOTE bind carrier's ELF name wins.
          break;
        }
      if (Key.empty())
        Key = Stable; // Manifest-only fallback.
      // B5 (review 2026-09-17): the report row must read the SUCCESSFULLY
      // INSERTED map key, never the moved-from local.  The symtab channel
      // iterates PlacementNames' keys, so a moved-from `Key` in Row.Sym was
      // invisible to the green symtab tests while losing the identity in
      // the very contract C owes D.
      auto Inserted = PlacementNames.emplace(Key, std::move(P));
      if (!Inserted.second)
        return fail(Err, "conflicting placement for " + Stable +
                             ": duplicate bind-only symbol name");
      LinkerResult::PlacementRecord Row;
      Row.Stable = Stable;
      Row.Sym = Inserted.first->first;
      Row.File = Seed->FileRef;
      Row.Section = "";
      Row.Address = MAddress;
      Row.Size = MSize;
      Row.Align = MAlign;
      Row.Flags = 0; // N7 option A: a bind-only group merges to flags 0.
      Row.StorageClass = MClass;
      Row.Entity = MEntity;
      Row.Ownership = PO_BIND;
      Row.BoundOnly = true;
      Row.LayoutHash = placementLayoutHash(MClass, MEntity, PO_BIND, MAddress,
                                           MAlign, 0);
      BuildSources(Row);
      PlacementRows.push_back(std::move(Row));
    }
  }
  Result.Placement = PlacementRows;
  // G11-C test instrument (review B5, 2026-09-17; same discipline as the
  // P-6 trace hook): MCS251_PLACEMENT_DUMP=<path> writes the
  // LinkerResult.Placement rows in the design §3.4 report column order
  // (stable | sym | file | section | class | entity | ownership | A | size |
  // align | flags | layout_hash | bound_only), so a test can assert the
  // C->D contract directly -- the name, the MERGED align/flags and
  // H_report -- instead of inferring it from the final symtab (which reads
  // the PlacementNames map keys and demonstrably hid the moved-from
  // Row.Sym).  Unset, no file operation happens; the production report
  // serializer belongs to G11-D.
  //
  // G11-D (C re-review suggestion 2, 2026-09-17): the instrument walks
  // `Result.Placement` -- the delivered interface -- not the internal
  // PlacementRows vector.  The two are currently equal, so the bytes are
  // unchanged, but the promise "the instrument checks the C->D interface"
  // now holds by construction instead of by coincidence.  The dupe
  // environment read happens only when the variable is set.
  if (const char *Dump = std::getenv("MCS251_PLACEMENT_DUMP")) {
    std::error_code EC;
    raw_fd_ostream OS(Dump, EC);
    if (!EC)
      for (const LinkerResult::PlacementRecord &R : Result.Placement)
        OS << R.Stable << " | " << R.Sym << " | " << R.File << " | "
           << R.Section << " | " << unsigned(R.StorageClass) << " | "
           << unsigned(R.Entity) << " | " << unsigned(R.Ownership) << " | 0x"
           << Twine::utohexstr(R.Address) << " | " << R.Size << " | "
           << R.Align << " | " << R.Flags << " | 0x"
           << Twine::utohexstr(R.LayoutHash) << " | "
           << (R.BoundOnly ? 1 : 0) << "\n";
  }
  return true;
}

// G11 §3.3 (post-resolve): a bind-only name that meets a *defined* global
// without placement is a contradiction -- the reference would silently
// resolve to the unplaced definition's address instead of A.  Owned<->bind
// groups never reach here (they hold no PlacementNames entry).
bool Linker::validatePlacementResolution() {
  for (const auto &P : PlacementNames) {
    auto It = Globals.find(P.first);
    if (It != Globals.end() && It->second->Defined)
      return fail(Err, "conflicting placement for " + P.first +
                           ": bind-only reference collides with a definition "
                           "without placement");
  }
  return true;
}

// G11 §3.3 (the unique collision checkpoint): before collectSymbols()
// appends the bind-only Synth rows, every output name must be disjoint
// from (1) the defined output set and (2) the COMPLETE Synth name set --
// the static reserved-boundary forms ∪ this link's live Synth keys.  The
// static enumeration and the live map are checked as two channels so a
// future Synth key cannot drift out of the check (rev 6 ruling).
bool Linker::validatePlacementSymbolNames() {
  if (PlacementNames.empty())
    return true;
  std::set<StringRef> Defined;
  for (const auto &F : Files)
    for (const InputSymbol &S : F->Symbols)
      if (S.Defined && !S.Name.empty() && S.Type != ELF::STT_SECTION)
        Defined.insert(S.Name);
  for (const auto &P : PlacementNames) {
    if (Defined.count(P.first) || isReservedBoundarySymbol(P.first) ||
        Synth.count(P.first))
      return fail(Err, "bind-only placement symbol " + P.first +
                           " collides with a defined or reserved output "
                           "symbol name");
  }
  return true;
}

int Linker::placementTargetClass(const InputSymbol *Target) const {
  if (Target->Sec) {
    if (Target->Sec->Region == "XSEG")
      return PSC_XDATA;
    if (Target->Sec->Region == "FIXED" &&
        Target->Sec->PlacementClass != 0xff)
      return Target->Sec->PlacementClass;
    return -1;
  }
  auto It = PlacementNames.find(Target->Name);
  if (It != PlacementNames.end())
    return It->second.StorageClass;
  return -1;
}

// G11 §2.2/F6: FIXED-CODE requires an explicit board-level window -- the
// linker never presumes a chip model.  Then every owned FIXED-CODE entity
// reserves its full section span (sh_size, jump tables included) through
// the unified CodeUsed ledger, so fixed-by-fixed and fixed-by-dynamic
// collisions both surface through reserveCode's existing overlap path.
bool Linker::layoutFixedCode() {
  std::vector<InputSection *> Fixed;
  for (InputSection *S : AllSections)
    if (S->Region == "FIXED" && S->PlacementClass == PSC_CODE)
      Fixed.push_back(S);
  if (Fixed.empty())
    return true;
  // B7 (review 2026-09-17): the frozen wording carries %sym -- the first
  // fixed CODE entity names the missing-window condition (the check is a
  // link-level property, but the diagnostic must stay actionable and match
  // the design §8 row exactly).
  InputSection *FirstFixed = Fixed.front();
  if (!Config.FlashGate) {
    auto First = FixedInfo.find(FirstFixed);
    const std::string Sym = First != FixedInfo.end() && First->second.Sym
                                ? First->second.Sym->Name
                                : FirstFixed->Name;
    return fail(Err, "fixed CODE entity " + Sym +
                         " requires an explicit CODE window "
                         "(--flash-base/--flash-size)");
  }
  const uint64_t Lo = Config.FlashBase;
  const uint64_t Hi = Lo + uint64_t(Config.FlashSize);
  for (InputSection *S : Fixed) {
    auto It = FixedInfo.find(S);
    if (It == FixedInfo.end())
      return fail(Err, "internal: fixed section without merge info: " +
                           S->Name);
    const FixedPlacementInfo &Info = It->second;
    const uint32_t A = Info.Address;
    const uint32_t Size = static_cast<uint32_t>(S->Size);
    if (A % Info.Align != 0)
      return fail(Err, "fixed placement for " +
                           (Info.Sym ? Info.Sym->Name : S->Name) +
                           " at 0x" + Twine::utohexstr(A) +
                           " violates alignment " + Twine(Info.Align));
    if (Size == 0)
      return fail(Err, "zero-size entity " +
                           (Info.Sym ? Info.Sym->Name : S->Name) +
                           " at 0x" + Twine::utohexstr(A) +
                           " is not placeable");
    if (A < Lo || A + Size > Hi)
      return fail(Err, "fixed CODE entity " +
                           (Info.Sym ? Info.Sym->Name : S->Name) + " at 0x" +
                           Twine::utohexstr(A) +
                           " is outside the CODE window");
    if (!reserveCode(A, Size, S->Name))
      return false;
    S->Address = A;
    if (S->IsLoadable)
      for (size_t I = 0; I != S->Data.size(); ++I) {
        uint32_t Byte = S->Address + I;
        if (Image.count(Byte))
          return fail(Err, "duplicate CODE byte at 0x" +
                               Twine::utohexstr(Byte));
        Image[Byte] = S->Data[I];
      }
  }
  return true;
}

// G11 §1.3: FIXED AS0-DATA enters the 16-bit DATA ledger before DATA_ABS,
// so a conflict names the fixed interval and every later first-fit class
// (BSEG_BYTES, bit slots, DSEG/OSEG, the G8 seed snapshot) sees it.
bool Linker::layoutFixedData() {
  for (InputSection *S : AllSections) {
    if (S->Region != "FIXED" || S->PlacementClass != PSC_AS0_DATA)
      continue;
    auto It = FixedInfo.find(S);
    if (It == FixedInfo.end())
      return fail(Err, "internal: fixed section without merge info: " +
                           S->Name);
    const FixedPlacementInfo &Info = It->second;
    const uint32_t A = Info.Address;
    const uint32_t Size = static_cast<uint32_t>(S->Size);
    if (A % Info.Align != 0)
      return fail(Err, "fixed placement for " +
                           (Info.Sym ? Info.Sym->Name : S->Name) +
                           " at 0x" + Twine::utohexstr(A) +
                           " violates alignment " + Twine(Info.Align));
    if (Size == 0)
      return fail(Err, "zero-size entity " +
                           (Info.Sym ? Info.Sym->Name : S->Name) +
                           " at 0x" + Twine::utohexstr(A) +
                           " is not placeable");
    if (!reserve(A, Size, S->Name))
      return false;
    S->Address = A;
  }
  return true;
}

// G11 §1.3/F3: FIXED-XDATA enters the 24-bit XDATA ledger before the XSEG
// cursor loop.  The G13b relaxations never apply to a fixed entity: one
// object stays <= 65535 bytes and never straddles a 64K window; the
// --area-start=XSEG declaration and the --xdata-size capacity gate cover
// it exactly like a dynamic XSEG slice.
bool Linker::layoutFixedXdata() {
  bool Any = false;
  for (InputSection *S : AllSections) {
    if (S->Region != "FIXED" || S->PlacementClass != PSC_XDATA)
      continue;
    Any = true;
    auto It = FixedInfo.find(S);
    if (It == FixedInfo.end())
      return fail(Err, "internal: fixed section without merge info: " +
                           S->Name);
    const FixedPlacementInfo &Info = It->second;
    const uint32_t A = Info.Address;
    const uint32_t Size = static_cast<uint32_t>(S->Size);
    const std::string Sym =
        Info.Sym ? Info.Sym->Name : S->Name;
    if (A % Info.Align != 0)
      return fail(Err, "fixed placement for " + Sym + " at 0x" +
                           Twine::utohexstr(A) + " violates alignment " +
                           Twine(Info.Align));
    if (Size == 0)
      return fail(Err, "zero-size entity " + Sym + " at 0x" +
                           Twine::utohexstr(A) + " is not placeable");
    if (Size > 0xffff)
      return fail(Err, "fixed XDATA entity " + Sym + " (" + Twine(Size) +
                           " bytes) exceeds the 64K window: XDATA objects "
                           "are limited to 65535 bytes and never straddle a "
                           "64K window boundary");
    if (!rangeFits(A, Size))
      return fail(Err, "XDATA address overflow in " + S->Name);
    if (Size && ((A ^ (A + Size - 1)) & 0xff0000) != 0)
      return fail(Err, "fixed XDATA entity " + Sym + " at 0x" +
                           Twine::utohexstr(A) + " straddles a 64K window "
                           "boundary");
    Range R{A, A + Size};
    for (const Range &U : XDataUsed)
      if (R.Start < U.End && U.Start < R.End)
        return fail(Err, "XDATA overlap for " + S->Name);
    S->Address = A;
    if (Size)
      XDataUsed.push_back(R);
  }
  if (Any && !hasAreaStart("XSEG"))
    return fail(Err, "missing --area-start=XSEG");
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

// G8: the silent half of the first-fit placement.  Returns true and commits
// the reservation when a slot exists; returns false without touching the
// ledger or emitting a diagnostic otherwise.  `allocate()` below is this plus
// the actionable failure report, and the EDATA failure-fallback re-layout uses
// this form to probe candidate placements before committing to a re-run.
bool Linker::tryAllocate(InputSection &S, uint32_t Lo, uint32_t Hi) {
  if (!S.Size)
    return true;
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
  return false;
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
  if (tryAllocate(S, Lo, Hi))
    return true;

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

bool Linker::reserveCode(uint32_t Start, uint32_t Size, StringRef What) {
  if (!rangeFits(Start, Size))
    return fail(Err, "CODE address overflow in " + What);
  Range R{Start, Start + Size};
  for (const Range &U : CodeUsed)
    if (R.Start < U.End && U.Start < R.End)
      return fail(Err, "CODE overlap for " + What);
  if (Size)
    CodeUsed.push_back(R);
  return true;
}

bool Linker::layoutCode() {
  CodeUsed.clear();
  auto reserveCode = [&](uint32_t Start, uint32_t Size, StringRef What) {
    return this->reserveCode(Start, Size, What);
  };
  // G11 §1.3: the FIXED-CODE reservations enter the unified CODE ledger
  // after the clear and before the dynamic cursor loop, so both collision
  // directions (fixed x fixed, fixed x dynamic) surface through the
  // reserveCode overlap path.
  if (!layoutFixedCode())
    return false;
  struct Cursor { uint32_t V; };
  std::map<std::string, Cursor> C;
  C["HOME"].V = areaStart("HOME", 0);
  C["VECS"].V = areaStart("VECS", 0);
  C["BOOT"].V = areaStart("BOOT", 0);
  C["CSEG"].V = areaStart("CSEG", 0);
  C["XINIT"].V = areaStart("XINIT", 0);
  C["XDATA_INIT"].V = areaStart("XDATA_INIT", 0);
  for (InputSection *S : AllSections)
    if ((S->Region == "HOME" || S->Region == "VECS" ||
         S->Region == "BOOT" || S->Region == "CSEG" ||
         S->Region == "XINIT" || S->Region == "XDATA_INIT") &&
        !hasAreaStart(S->Region))
      return fail(Err, "missing --area-start=" + S->Region);
  for (InputSection *S : AllSections) {
    if (S->Region != "HOME" && S->Region != "VECS" &&
        S->Region != "BOOT" && S->Region != "CSEG" && S->Region != "XINIT" &&
        S->Region != "XDATA_INIT")
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

// BT13: cross-TU bit slot allocation (BIT-OBJECT-CONTRACT.md §5).  Runs inside
// layoutData() after the BSEG_BYTES pool and the legacy BIT_BANK overlay are
// placed, and before any ordinary RAM class.  Its DATA-ledger reservations are
// what exclude a bit byte from DSEG/OSEG/ISEG/SSEG.
bool Linker::allocateBitSlots() {
  BitOf.clear();
  BitMask.clear();
  BitValue.clear();
  BitPoolByte.clear();
  BitExclusiveByte.clear();
  BitByteOrigin.clear();
  BitFixedByte.clear();

  // The physical pool is the byte range already reserved by BSEG_BYTES slices
  // (the CRT owns 16 bytes).  Those bytes are never reserved twice: they are
  // only sub-allocated.  A legacy BIT_BANK byte is overlay storage and is not
  // part of the pool, so automatic objects never land in it.
  for (InputSection *S : AllSections)
    if (S->Region == "BSEG_BYTES" && S->Size)
      for (uint32_t B = S->Address; B != S->Address + S->Size; ++B) {
        BitPoolByte.insert(B);
        BitByteOrigin[B] = "crt-pool " + S->Name;
      }

  auto InPool = [&](uint32_t Byte) { return BitPoolByte.count(Byte) != 0; };
  auto Reserved = [&](uint32_t Byte) {
    for (const DataUse &U : DataUsed)
      if (U.R.Start <= Byte && Byte < U.R.End)
        return true;
    return false;
  };

  // Deterministic order: input-file order, then record order inside each
  // file's single `.mcs251.bit` section.
  std::vector<std::pair<InputFile *, BitRecord *>> Recs;
  for (auto &F : Files)
    for (BitRecord &R : F->BitRecords)
      Recs.push_back({F.get(), &R});

  // 1. Fixed references reserve their whole backing byte exclusively.  A byte
  //    already in the pool needs no second reservation; SFR addresses
  //    (>= 0x80) have no RAM backing at all.  Aliases of one address are
  //    idempotent by construction.
  for (auto &E : Recs) {
    BitRecord *R = E.second;
    if (R->Kind != BitKindReference)
      continue;
    const uint32_t Bit = R->Ref->Value;
    BitOf[R->Ref] = Bit;
    if (Bit > 0x7f)
      continue; // SFR bit reference: no window byte.
    const uint32_t Byte = BitWindowBase + (Bit >> 3);
    if (BitExclusiveByte.count(Byte))
      continue;
    if (!InPool(Byte)) {
      if (Reserved(Byte))
        return fail(Err, "MCS251 bit: fixed reference " + R->Ref->Name +
                             " needs backing byte 0x" + Twine::utohexstr(Byte) +
                             " which is already occupied");
      if (!reserve(Byte, 1, "bit fixed " + R->Ref->Name))
        return false;
    }
    BitExclusiveByte.insert(Byte);
    if (!BitByteOrigin.count(Byte))
      BitByteOrigin[Byte] = "fixed " + R->Ref->Name;
    // First fixed alias of this byte names it for the auditability rows.
    if (!BitFixedByte.count(Byte))
      BitFixedByte[Byte] = R->Ref->Name;
  }

  // 2. Automatic objects first-fit the lowest free bit of a non-exclusive
  //    byte.  Different translation units share a byte's free bits.
  uint32_t DefCount = 0;
  for (auto &E : Recs)
    if (E.second->Kind == BitKindDefinition)
      ++DefCount;
  std::set<uint32_t> ReservedAuto; // Non-pool bytes reserved by this pass.
  uint32_t NextBit = 0;
  uint32_t Placed = 0;
  for (auto &E : Recs) {
    BitRecord *R = E.second;
    if (R->Kind != BitKindDefinition)
      continue;
    InputSymbol *Sym = R->Ref;
    bool Found = false;
    for (uint32_t B = NextBit; B < BitCount; ++B) {
      const uint32_t Byte = BitWindowBase + (B >> 3);
      if (BitExclusiveByte.count(Byte))
        continue;
      // A byte reserved by a non-pool DATA use is blocked, but a byte this
      // pass reserved for an earlier bit of the same pool is not.
      if (!InPool(Byte) && ReservedAuto.count(Byte) == 0 && Reserved(Byte))
        continue;
      const uint32_t Idx = B & 7;
      BitOf[Sym] = B;
      BitMask[Byte] |= uint8_t(1u << Idx);
      if (R->InitValue)
        BitValue[Byte] |= uint8_t(1u << Idx);
      if (!InPool(Byte) && !ReservedAuto.count(Byte)) {
        if (!reserve(Byte, 1, "bit object " + Sym->Name))
          return false;
        ReservedAuto.insert(Byte);
        BitByteOrigin[Byte] = "bit " + Sym->Name;
      }
      NextBit = B + 1;
      Found = true;
      ++Placed;
      break;
    }
    if (!Found) {
      // BT13 auditability: report why each byte in the window is unavailable,
      // naming every occupied source without truncation, so the exhausted owner
      // is fully accounted for from the message alone.  A byte is unavailable
      // when a fixed reference reserves it exclusively, when an ordinary DATA
      // reservation (or --reserve-data) covers it, or when automatic bit
      // objects have already claimed all eight of its bits (the full-window
      // case, where no byte is *blocked* yet no bit is free).
      std::vector<std::pair<uint32_t, std::string>> Blocked;
      for (uint32_t B = 0; B < BitCount; ++B) {
        const uint32_t Byte = BitWindowBase + (B >> 3);
        if (BitExclusiveByte.count(Byte)) {
          Blocked.push_back({Byte, BitByteOrigin.count(Byte)
                                      ? BitByteOrigin[Byte]
                                      : std::string("fixed reference")});
          continue;
        }
        if (!InPool(Byte) && ReservedAuto.count(Byte) == 0 && Reserved(Byte)) {
          std::string What = "ordinary DATA reservation";
          for (const DataUse &U : DataUsed)
            if (U.R.Start <= Byte && Byte < U.R.End) {
              What = U.What;
              break;
            }
          Blocked.push_back({Byte, What});
          continue;
        }
        auto M = BitMask.find(Byte);
        if (M != BitMask.end() && M->second == 0xFF)
          Blocked.push_back({Byte, "bit objects (full mask 0xff)"});
      }
      llvm::sort(Blocked);
      Blocked.erase(std::unique(Blocked.begin(), Blocked.end()), Blocked.end());
      std::string Occ;
      for (const auto &P : Blocked)
        Occ += " 0x" + Twine::utohexstr(P.first).str() + " from " + P.second;
      // List every allocated automatic bit with its owner so a full window is
      // fully attributable.
      std::vector<std::pair<uint32_t, std::string>> AutoSlots;
      for (const auto &P : BitOf)
        if (P.second <= 0x7f)
          AutoSlots.push_back({P.second, P.first->Name});
      llvm::sort(AutoSlots);
      std::string Allocd;
      for (const auto &P : AutoSlots)
        Allocd += " 0x" + Twine::utohexstr(P.first).str() + "=" + P.second;
      return fail(Err, "MCS251 bit: bit slot exhaustion: cannot allocate bit "
                       "object " + Sym->Name + " (placed " + Twine(Placed) +
                           " of " + Twine(DefCount) +
                           " objects in the 128-bit window [0x00,0x7f]); no "
                           "byte-RAM fallback exists; blocked bytes:" +
                           (Occ.empty() ? std::string(" none") : Occ) +
                           "; allocated bits:" +
                           (Allocd.empty() ? std::string(" none") : Allocd));
    }
  }

  // 3. Initialize the owned bits.  Two strategies, mutually exclusive:
  //
  //  BT14 bit profile (crt-bit.yaml, BitProfileFile != null): synthesize a
  //  `.mcs251.bittable` in the BITINIT CODE area with one 3-byte record per
  //  automatic backing byte -- {addr, and_mask = ~mask, or_value = value &
  //  mask} -- applied by the CRT's __mcs251_bit_init walker as
  //  IRAM[addr] = (IRAM[addr] & and_mask) | or_value.  Neighbour bits (mask
  //  0 in the owned mask) keep their power-on value, a byte with mask 0
  //  (fixed-only or untouched) gets NO record and is never accessed, and
  //  zero-valued owned bits are cleared through the mask explicitly -- never
  //  through a blanket window clear and never through QEMU-default zero RAM.
  //  A full mask (0xff) makes and_mask 0x00, i.e. the walker writes the
  //  declared value outright.  No XINIT record is synthesized for a bit byte
  //  on this path (a whole-byte XINIT payload would clobber the neighbours).
  //
  //  S1 (no profile, the pre-BT14 frozen behaviour): the old CRT clears the
  //  whole [0x20,0x30) window and one v1 XINIT record per byte with a
  //  nonzero value is synthesized after every input XINIT section.  This
  //  branch is kept byte-identical for links without the profile.
  if (BitProfileFile) {
    if (!BitMask.empty()) {
      if (!hasAreaStart("BITINIT"))
        return fail(Err, "MCS251 bit: bit objects under the bit-aware CRT "
                         "profile require --area-start=BITINIT");
      uint32_t Start = areaStart("BITINIT", 0);
      for (InputSection *S : AllSections)
        if (S->Region == "BITINIT" && S->Size)
          Start = std::max(Start, S->Address + uint32_t(S->Size));
      std::vector<uint8_t> Data;
      for (const auto &P : BitMask) {
        const uint8_t Mask = P.second;
        auto V = BitValue.find(P.first);
        const uint8_t Value = V == BitValue.end() ? 0 : V->second;
        Data.push_back(static_cast<uint8_t>(P.first));
        Data.push_back(static_cast<uint8_t>(~Mask));
        Data.push_back(Value & Mask);
      }
      auto S = std::make_unique<InputSection>();
      S->Name = ".mcs251.bittable";
      S->Type = ELF::SHT_PROGBITS;
      S->Flags = ELF::SHF_ALLOC;
      S->Size = Data.size();
      S->Align = 1;
      S->Address = Start;
      S->Region = "BITINIT";
      S->IsAlloc = true;
      S->IsLoadable = true;
      S->Synthesized = true;
      S->Data = Data;
      // The table is a CODE-class ROM object exactly like the synthesized
      // XINIT records: the always-on 24-bit range bound and the overlap
      // check apply; the optional board flash gate is not a substitute.
      if (!reserveCode(S->Address, static_cast<uint32_t>(S->Size), S->Name))
        return false;
      for (size_t I = 0; I != Data.size(); ++I) {
        const uint32_t A = S->Address + static_cast<uint32_t>(I);
        if (Image.count(A))
          return fail(Err, "MCS251 bit: synthesized BITINIT byte overlaps "
                           "CODE at 0x" + Twine::utohexstr(A));
        Image[A] = Data[I];
      }
      AllSections.push_back(S.get());
      OwnedSynth.push_back(std::move(S));
    }
    return true;
  }

  // S1 path (frozen): aggregate one v1 XINIT record per byte with a nonzero
  // initial value.
  //    Every such byte is either pool storage (the CRT owns the whole window)
  //    or a byte lld reserved exclusively for bits (this pass rejects any
  //    other kind of collision), so a whole-byte payload cannot clobber an
  //    unrelated ordinary object.  Zero-initialized bits are produced by the
  //    CRT's explicit window clear, never by NOBITS.  validateXInit() accepts
  //    these allocator-owned bytes as destinations.
  if (!BitValue.empty()) {
    if (!hasAreaStart("XINIT"))
      return fail(Err, "MCS251 bit: a nonzero bit initial value requires "
                       "--area-start=XINIT");
    uint32_t XinitEnd = areaStart("XINIT", 0);
    for (InputSection *S : AllSections)
      if (S->Region == "XINIT" && S->Size)
        XinitEnd = std::max(XinitEnd, S->Address + uint32_t(S->Size));
    std::vector<uint8_t> Data;
    for (const auto &P : BitValue) {
      Data.push_back(static_cast<uint8_t>(P.first >> 8));
      Data.push_back(static_cast<uint8_t>(P.first));
      Data.push_back(0);
      Data.push_back(1); // object_size
      Data.push_back(0);
      Data.push_back(1); // payload_size
      Data.push_back(P.second);
    }
    auto S = std::make_unique<InputSection>();
    S->Name = ".mcs251.xinit.bit";
    S->Type = ELF::SHT_PROGBITS;
    S->Flags = ELF::SHF_ALLOC;
    S->Size = Data.size();
    S->Align = 1;
    S->Address = XinitEnd;
    S->Region = "XINIT";
    S->IsAlloc = true;
    S->IsLoadable = true;
    S->Synthesized = true;
    S->Data = Data;
    // BT13/BT15: the synthesized section obeys exactly the CODE range and
    // occupancy rules every ordinary XINIT section obeys (XINIT is a CODE-class
    // ROM area selected by Region, like an input .mcs251.xinit).  reserveCode()
    // enforces the always-on 24-bit address limit (rangeFits) and rejects any
    // overlap with an already occupied CODE range; the optional board-level
    // flash gate is not a substitute for the architectural address check.
    if (!reserveCode(S->Address, static_cast<uint32_t>(S->Size), S->Name))
      return false;
    for (size_t I = 0; I != Data.size(); ++I) {
      const uint32_t A = S->Address + static_cast<uint32_t>(I);
      if (Image.count(A))
        return fail(Err, "MCS251 bit: synthesized XINIT byte overlaps CODE at "
                         "0x" + Twine::utohexstr(A));
      Image[A] = Data[I];
    }
    AllSections.push_back(S.get());
    OwnedSynth.push_back(std::move(S));
  }
  return true;
}

bool Linker::layoutData() {
  DataUsed.clear();
  StackH = 0x100;
  for (const Range &R : Config.ReservedData)
    if (!reserve(R.Start, R.End - R.Start, "--reserve-data"))
      return false;

  // G11 §1.3: FIXED AS0-DATA enters the 16-bit ledger before DATA_ABS, so
  // a conflict names the fixed interval and every later first-fit class --
  // BSEG_BYTES, the bit slots, DSEG/OSEG and the G8 seed snapshot below --
  // is constructively aware of it.
  if (!layoutFixedData())
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

  // BT13: cross-TU bit slot allocation. Runs after the BSEG_BYTES pool and the
  // legacy BIT_BANK overlay are placed, and before any ordinary RAM class, so
  // a byte owned by an automatic bit object is excluded from DSEG/OSEG/...
  if (!allocateBitSlots())
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

  // G8 (design §3 "migration mechanism"): the EDATA window is
  // [max(0x100, DsegStart), --edata-end].  A board that narrows the window
  // below its start (e.g. --edata-end=0xff) declares it empty; that is legal
  // and simply makes every migration attempt fail closed.
  const uint32_t EdataLo = std::max<uint32_t>(0x100, DsegStart);
  const uint32_t EdataHi = Config.EdataEnd;

  // The failure-fallback re-layout restarts the low-window sequence from the
  // same ledger state on every attempt, so snapshot the state as it stands
  // before any DSEG/OSEG placement.
  const std::vector<DataUse> SeedUsed = DataUsed;
  const uint32_t SeedH = StackH;

  // Sections moved out of the low window, in migration order (deterministic).
  // They become Region "EDATA" once the layout succeeds.
  std::vector<InputSection *> Migrated;
  auto AlreadyMigrated = [&](const InputSection *S) {
    return llvm::is_contained(Migrated, S);
  };
  // A section is a migration candidate only when the producer marked it
  // (SHF_MCS251_EDATA_MOVABLE on an ordinary AS0 DSEG slice).  Unmarked
  // slices, `__data` (AS1) objects, v1 objects, XINIT/absolute/overlay
  // slices never enter the candidate set.
  auto IsCandidate = [](const InputSection *S) {
    return S->Region == "DSEG" && S->EDataMovable && S->Size != 0;
  };
  auto EdataUnavailable = [&](const InputSection &S) {
    return fail(Err, "cannot allocate " + S.Name +
                         " in the EDATA window [0x" +
                         Twine::utohexstr(EdataLo) + ",0x" +
                         Twine::utohexstr(EdataHi) +
                         "] (widen --edata-end to hold " +
                         Twine::utohexstr(S.Size) + " more bytes)");
  };
  // Silenceable OSEG overlay placement, the counterpart of
  // allocateOverlayGroup() for a provisional attempt.
  auto tryOverlayGroup = [&](StringRef Group, uint32_t Lo, uint32_t Hi) {
    uint32_t Size = 0;
    InputSection *Representative = nullptr;
    for (InputSection *S : AllSections)
      if (S->Group == Group && S->Size >= Size) {
        Size = S->Size;
        Representative = S;
      }
    if (!Representative || !Size)
      return true;
    if (!tryAllocate(*Representative, Lo, Hi))
      return false;
    for (InputSection *S : AllSections)
      if (S->Group == Group)
        S->Address = Representative->Address;
    return true;
  };

  // Deterministic failure-fallback re-layout: place the migrated set in the
  // EDATA window, then every remaining DSEG slice in input order and the OSEG
  // overlay in the low window.  On failure migrate one candidate (the failing
  // slice itself when it is a candidate, else the largest remaining candidate,
  // ties by input order) and restart the whole sequence.  The candidate set
  // exhausted means the low window cannot be freed for an unmovable slice:
  // that stays a plain link failure, never a silent degradation.
  bool Placed = false;
  while (!Placed) {
    DataUsed = SeedUsed;
    StackH = SeedH;

    for (InputSection *S : Migrated)
      if (EdataHi < EdataLo || !tryAllocate(*S, EdataLo, EdataHi))
        return EdataUnavailable(*S);
    // A section the producer emitted directly into the EDATA Region (an
    // explicit `.mcs251.edata` slice) is placed there unconditionally; it
    // never competes for the low window.  No backend emitter uses this in G8
    // (S1 marks DSEG slices instead), but the Region is part of the S0
    // infrastructure and is exercised by the linker's own tests.
    for (InputSection *S : AllSections)
      if (S->Region == "EDATA" &&
          (EdataHi < EdataLo || !tryAllocate(*S, EdataLo, EdataHi)))
        return EdataUnavailable(*S);

    InputSection *Failed = nullptr;
    for (InputSection *S : AllSections)
      if (S->Region == "DSEG" && !AlreadyMigrated(S) &&
          !tryAllocate(*S, DsegStart, DsegEnd - 1)) {
        Failed = S;
        break;
      }
    bool OverlayFailed = false;
    if (!Failed && !tryOverlayGroup("OSEG", DsegStart, DsegEnd - 1))
      OverlayFailed = true;
    if (!Failed && !OverlayFailed) {
      Placed = true;
      break;
    }

    // Pick the migration victim.
    InputSection *Victim = nullptr;
    if (Failed && IsCandidate(Failed)) {
      Victim = Failed;
    } else {
      for (InputSection *S : AllSections)
        if (IsCandidate(S) && !AlreadyMigrated(S) &&
            (!Victim || S->Size > Victim->Size))
          Victim = S;
    }
    if (!Victim) {
      // No candidate can free the window: fail closed on the *live* ledger.
      //
      // The ledger at this point holds every reservation this attempt
      // committed (the migrated set in EDATA plus the DSEG/OSEG slices already
      // placed in the low window), and it is exactly the occupancy that made
      // `Failed`/the OSEG overlay fail.  Re-running the diagnostic form here
      // must therefore reuse that state unchanged.
      //
      // Resetting to SeedUsed (or replaying only `Migrated`) would erase the
      // unmarked or already-migrated slices that hold the window and let the
      // retried slice land on top of them: a hard allocation failure would be
      // reported as success with two sections at the same address.
      // `tryAllocate` is deterministic and the ledger is unchanged since the
      // failing probe, so the re-probe below fails again and only emits the
      // established actionable report.
      if (Failed) {
        if (allocate(*Failed, DsegStart, DsegEnd - 1))
          return fail(Err, "internal: DSEG re-allocation of " + Failed->Name +
                               " unexpectedly succeeded on the failure ledger");
        return false;
      }
      if (allocateOverlayGroup("OSEG", DsegStart, DsegEnd - 1))
        return fail(Err, "internal: OSEG re-allocation unexpectedly "
                         "succeeded on the failure ledger");
      return false;
    }
    Migrated.push_back(Victim);
  }
  // The migrated slices now belong to the EDATA Region: the boundary symbols,
  // the map rows and the XINIT destination whitelist all key off it.
  for (InputSection *S : Migrated)
    S->Region = "EDATA";

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
  // G11 §1.3/F3: FIXED-XDATA reservations enter the 24-bit ledger before
  // the XSEG cursor loop; the fixed entity also requires the --area-start=
  // XSEG declaration (the board's XDATA window base) exactly like a
  // dynamic slice.  Fixed placements never advance XsegCursor.
  if (!layoutFixedXdata())
    return false;
  bool HasXSeg = llvm::any_of(AllSections,
                              [](const InputSection *S) { return S->Region == "XSEG"; });
  if (HasXSeg && !hasAreaStart("XSEG"))
    return fail(Err, "missing --area-start=XSEG");
  uint32_t XsegCursor = areaStart("XSEG", 0);
  for (InputSection *S : AllSections)
    if (S->Region == "XSEG") {
      // X3 ruling: one XSEG section is one object, and an UNMARKED XDATA
      // object never straddles a 64K window (the record format has a single
      // bank byte, and the XINIT v1 copier walks one contiguous range).  A
      // single unmarked object is therefore capped at 16 bits -- larger
      // unmarked objects exceed the corpus and the boards and cannot be
      // described by any record (G-T11-BASE negatives keep this gate).
      // G13b: a split-XSEG section (SHF_MCS251_XSEG_SPLIT, v2 objects only)
      // is one logical object the producer declared larger than the record
      // limit; the cap does not apply to it -- the linker places it as one
      // contiguous range and synthesizes the per-window records itself.
      if (S->Size > 0xffff && !S->XsegSplit) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "XSEG section " << S->Name << " (" << S->Size
           << " bytes) exceeds the 64K window: XDATA objects are limited to "
              "65535 bytes and never straddle a 64K window boundary";
        return fail(Err, Msg);
      }
      uint64_t Place = XsegCursor;
      if (hasAreaStart(S->Name)) {
        Place = areaStart(S->Name, 0);
        // An explicit per-section start is honored verbatim: a start that
        // would straddle a window is a layout error, never silently jumped.
        // G13b (I2.6): for a split section straddling IS the point -- the
        // check is exempted and the verbatim start stands (the 24-bit
        // rangeFits, the overlap gate and the capacity gate below still
        // apply).  Unmarked sections keep the rejection byte for byte.
        if (!S->XsegSplit && S->Size &&
            ((Place ^ (Place + S->Size - 1)) & 0xff0000ULL) != 0) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "explicit --area-start=" << S->Name << " places the section across a 64K window boundary: ["
             << format_hex(Place, 6, false) << ","
             << format_hex(Place + S->Size, 6, false) << ")";
          return fail(Err, Msg);
        }
      } else if (S->Size && !S->XsegSplit) {
        // Sequential placement keeps each unmarked object inside one 64K
        // window: when the tail of the current window cannot hold the whole
        // object, the cursor jumps to the next bank start.  The hole this
        // leaves counts toward the l_XSEG span (the existing span
        // semantics) and touches neither DSEG nor the stack.  A split
        // section skips the jump: it is placed at the cursor and spans as
        // many windows as it needs (single contiguous range, design §2.5
        // proposition A).  The cursor is allowed to advance to any phase;
        // the next unmarked object's window math below reads the ADVANCED
        // cursor (I2.1).
        const uint64_t WindowEnd = (XsegCursor & 0xff0000ULL) + 0x10000;
        if (uint64_t(S->Size) > WindowEnd - XsegCursor)
          Place = WindowEnd;
      }
      if (!rangeFits(Place, S->Size))
        return fail(Err, "XDATA address overflow in " + S->Name);
      Range R{static_cast<uint32_t>(Place),
              static_cast<uint32_t>(Place + S->Size)};
      for (const Range &U : XDataUsed)
        if (R.Start < U.End && U.Start < R.End)
          return fail(Err, "XDATA overlap for " + S->Name);
      S->Address = R.Start;
      if (S->Size)
        XDataUsed.push_back(R);
      XsegCursor = R.End;
    }
  // X3 capacity gate (--xdata-size): every allocated XSEG range must lie
  // inside [area-start(XSEG), area-start(XSEG)+N).  Checked per section with
  // 64-bit arithmetic; the always-on 24-bit rangeFits above still governs
  // the architectural limit when the option is absent.
  // G11 F3: a FIXED-XDATA entity is gated exactly like a dynamic slice.
  if (Config.XdataSize) {
    const uint64_t Base = areaStart("XSEG", 0);
    const uint64_t Limit = Base + uint64_t(Config.XdataSize);
    for (InputSection *S : AllSections)
      if ((S->Region == "XSEG" ||
           (S->Region == "FIXED" && S->PlacementClass == PSC_XDATA)) &&
          S->Size) {
        const uint64_t Lo = S->Address, Hi = Lo + S->Size;
        if (Lo < Base || Hi > Limit) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "XDATA capacity [" << format_hex(Lo, 6, false) << ","
             << format_hex(Hi, 6, false) << ") exceeds --xdata-size="
             << Config.XdataSize << " in " << S->Name;
          return fail(Err, Msg);
        }
      }
  }

  // G13b: synthesize clear-only v1 XDATA_INIT records for every split-XSEG
  // section (SHF_MCS251_XSEG_SPLIT).  Runs after the whole XSEG loop (every
  // flagged section has its final address) and after layoutCode has placed
  // the input XDATA_INIT sections (their CODE reservations are final, so
  // reserveCode below sees the full ledger).  Split algorithm (design
  // §3.1.2): len = min(remaining, 0xFFFF, window tail) -- the u16 record
  // field and the frozen single-window property both bound one record; the
  // record destinations tile [S->Address, S->Address+Size) exactly, in
  // order, by construction (the synthesis-specific coverage invariant --
  // validateXDATAInit does not check coverage, see §2.7.4 of the design).
  // Unmarked XSEG sections are never touched: their producer-emitted input
  // records stay the only description.
  if (llvm::any_of(AllSections, [](const InputSection *S) {
        return S->Region == "XSEG" && S->XsegSplit && S->Size != 0;
      })) {
    std::vector<uint8_t> Data;
    for (InputSection *S : AllSections)
      if (S->Region == "XSEG" && S->XsegSplit && S->Size != 0) {
        uint64_t Cur = S->Address;
        uint64_t Remaining = S->Size;
        while (Remaining) {
          const uint64_t WindowEnd = (Cur & 0xff0000ULL) + 0x10000;
          const uint64_t Len = std::min(std::min(Remaining, uint64_t(0xffff)),
                                        WindowEnd - Cur);
          Data.push_back(static_cast<uint8_t>(Cur >> 16));
          Data.push_back(static_cast<uint8_t>((Cur >> 8) & 0xff));
          Data.push_back(static_cast<uint8_t>(Cur & 0xff));
          Data.push_back(static_cast<uint8_t>(Len >> 8));
          Data.push_back(static_cast<uint8_t>(Len & 0xff));
          Data.push_back(0); // payload_size = 0: clear only
          Data.push_back(0);
          Cur += Len;
          Remaining -= Len;
        }
      }
    // G3 (design §2.7.8): the layoutCode "missing --area-start" check only
    // sees sections that EXIST as input; a link whose only XDATA_INIT table
    // is the synthesized one has no input record section, so the synthesis
    // point re-checks the requirement itself (XINIT bit-record precedent:
    // the allocateBitSlots self-check).
    if (!hasAreaStart("XDATA_INIT"))
      return fail(Err, "missing --area-start=XDATA_INIT");
    uint32_t Start = areaStart("XDATA_INIT", 0);
    for (InputSection *S : AllSections)
      if (S->Region == "XDATA_INIT" && S->Size)
        Start = std::max(Start, S->Address + static_cast<uint32_t>(S->Size));
    auto Syn = std::make_unique<InputSection>();
    Syn->Name = ".mcs251.xdata_init.synth";
    Syn->Type = ELF::SHT_PROGBITS;
    Syn->Flags = ELF::SHF_ALLOC;
    Syn->Size = Data.size();
    Syn->Align = 1;
    Syn->Address = Start;
    Syn->Region = "XDATA_INIT";
    Syn->IsAlloc = true;
    Syn->IsLoadable = true;
    Syn->Synthesized = true;
    Syn->Data = std::move(Data);
    // G1/G2 (design §3.1.2): the synthesized table occupies real CODE space
    // and must reserve it through the same ledger every ordinary CODE section
    // and the XINIT synthesis use: reserveCode() enforces the always-on
    // 24-bit address limit (G2, rangeFits) and rejects any overlap with an
    // already occupied CODE range (G1) -- the region after the XDATA_INIT
    // cursor tail is NOT implicitly free.
    if (!reserveCode(Syn->Address, static_cast<uint32_t>(Syn->Size),
                     Syn->Name))
      return false;
    for (size_t I = 0; I != Syn->Data.size(); ++I) {
      const uint32_t A = Syn->Address + static_cast<uint32_t>(I);
      if (Image.count(A))
        return fail(Err, "synthesized XDATA_INIT byte overlaps CODE at 0x" +
                             Twine::utohexstr(A));
      Image[A] = Syn->Data[I];
    }
    AllSections.push_back(Syn.get());
    OwnedSynth.push_back(std::move(Syn));
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
  for (const char *R : {"HOME", "VECS", "BOOT", "CSEG", "XINIT",
                        "XDATA_INIT"}) {
    uint32_t Start = areaStart(R, 0), End = Start;
    for (InputSection *S : AllSections)
      if (S->Region == R)
        End = std::max(End, S->Address + static_cast<uint32_t>(S->Size));
    Synth[(Twine("s_") + R).str()] = Start;
    Synth[(Twine("l_") + R).str()] = End - Start;
  }
  // BT14 (R1): the BITINIT boundary symbols are synthesized only when the
  // link actually carries the bit-init machinery: the bit profile is present
  // (so the bit CRT's s_BITINIT/l_BITINIT references resolve, including the
  // empty-table link where l_BITINIT = 0), or the build explicitly
  // configures the area with --area-start=BITINIT.  An old S1 link (no
  // .mcs251.bitprofile, no BITINIT configuration) has no BITINIT area and no
  // consumer, so its map, image and --keep-symbols symbol table stay
  // byte-identical to the pre-BT14 frozen artifacts: the symbols must not be
  // injected there.  This is the set consulted by errorUndefined(),
  // applyRelocations(), buildMap() and collectSymbols(); keeping it the
  // single source of truth keeps all four in agreement.
  const bool WantBitInit = BitProfileFile != nullptr || hasAreaStart("BITINIT");
  if (WantBitInit) {
    uint32_t Start = areaStart("BITINIT", 0), End = Start;
    for (InputSection *S : AllSections)
      if (S->Region == "BITINIT")
        End = std::max(End, S->Address + static_cast<uint32_t>(S->Size));
    Synth["s_BITINIT"] = Start;
    Synth["l_BITINIT"] = End - Start;
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
  // G8: EDATA boundary symbols.  s_EDATA is the address of the first
  // non-empty EDATA slice in input order; l_EDATA is the sum of the slice
  // sizes (the ISEG "sum of slice sizes, not physical span" convention), so a
  // dispersed migration reports its real footprint.  When nothing migrated
  // both stay absent, keeping the map byte-identical to a pre-G8 link.
  {
    InputSection *FirstEdata = nullptr;
    uint32_t EdataTotal = 0;
    for (InputSection *S : AllSections)
      if (S->Region == "EDATA" && S->Size) {
        if (!FirstEdata)
          FirstEdata = S;
        EdataTotal += static_cast<uint32_t>(S->Size);
      }
    if (FirstEdata) {
      Synth["s_EDATA"] = FirstEdata->Address;
      Synth["l_EDATA"] = EdataTotal;
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
                           ") overlaps the reserved vector area [0x" +
                           Twine::utohexstr(MCS251ISR::ISRVectorBase) +
                           ",0x" +
                           Twine::utohexstr(MCS251ISR::ISRVectorEnd) + ")");
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
  // T07 step 11: the CRT BOOT must start at or above the shared floor.  The
  // whole vector area is reserved, so a BOOT that reaches into it (the old
  // 0xff0210 default, or a FF0100-era layout) is a conflict in IRQ mode.
  for (InputSection *S : AllSections)
    if (S->File == CrtFile && S->Region == "BOOT" && S->Size &&
        S->Address < MCS251ISR::ISRBootMinAddress)
      return fail(Err, "MCS251 ISR: CRT BOOT must start at or above 0x" +
                           Twine::utohexstr(MCS251ISR::ISRBootMinAddress) +
                           ", got 0x" + Twine::utohexstr(S->Address));
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
           N == "XINIT" || N == "XDATA_INIT" || N == "BITINIT";
  };
  // A configured CODE-class area start must itself sit inside the window,
  // even when the area turns out to hold no bytes at all.
  for (const auto &P : Config.AreaStarts)
    if (IsCodeArea(StringRef(P.first)) && (P.second < Lo || P.second >= Hi))
      return fail(Err, "--area-start=" + P.first + "=" + Hex(P.second) +
                           " is outside the flash window " + Hex(Lo) + ".." +
                           Hex(Hi) + " (--flash-base/--flash-size)");
  for (const InputSection *S : AllSections) {
    // G11 F6: a FIXED entity of the CODE storage class joins the gate
    // (functions and CODE-space const objects alike; EXECINSTR is
    // irrelevant).  A FIXED entity without the explicit window never
    // reaches here -- layoutFixedCode already failed the link.
    const bool CodeArea =
        IsCodeArea(StringRef(S->Region)) ||
        (S->Region == "FIXED" && S->PlacementClass == PSC_CODE);
    if (!CodeArea || S->Size == 0)
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
      // G11 (§3.3 insertion 3): a bind-only placement name is a legal
      // undefined reference -- the placement table carries its address.
      if (!Globals.count(S.Name) && !Synth.count(S.Name) &&
          !PlacementNames.count(S.Name))
        return fail(Err, "undefined symbol: " + S.Name);
    }
  return true;
}

bool Linker::applyRelocations() {
  // P-6(b) test hook (design §8, rev 7): the black-box wording alone cannot
  // prove the internal gate ORDER -- the claim is that a rejected relocation
  // never reaches the band check or the field write (the F12 low-16 write is
  // exactly what F9 preempts).  With MCS251_P6_TRACE=<path> set, every
  // relocated field gets a sentinel planted into Image before the pass, every
  // write a relocation performs is counted, and a trace of the resulting
  // bytes is dumped afterwards (success or failure).  Unset, no sentinel
  // operation is performed at all: the link is byte-for-byte production
  // behavior; it is a test instrument, never a production switch.
  //
  // B6 (review 2026-09-17): the sentinel occupies EXACTLY the relocation's
  // field width (1/2/3 bytes from the R_* type), never a fixed two bytes.
  // Field widths are validated against the section before planting and the
  // restore walks the same width, so a width-1 field at the last byte of a
  // section can never touch the abutting next section (the review observed
  // 11223344 -> 5a223344: the second sentinel byte landed in the adjacent
  // section and the old two-byte-only restore skipped it).
  static constexpr uint8_t P6Sentinel[3] = {0xA5, 0x5A, 0x3C};
  using P6Key = std::pair<InputSection *, uint32_t>;
  struct P6Field {
    uint32_t Width;
    std::array<uint8_t, 3> Before;
  };
  std::map<P6Key, P6Field> P6Original;
  std::map<P6Key, unsigned> P6Writes;
  const char *P6Path = std::getenv("MCS251_P6_TRACE");
  if (P6Path) {
    // Capture every covered field's bytes BEFORE planting any sentinel, so
    // the success-path restore is order-independent even when two fields
    // overlap (e.g. adjacent MID8/LO8 slots).
    std::vector<P6Key> Plant;
    for (InputSection *S : AllSections) {
      if (S->IsNobits || !S->IsAlloc)
        continue;
      for (const Relocation &R : S->Relocs) {
        const uint32_t Width =
            (R.Type == ELF::R_MCS251_16 || R.Type == ELF::R_MCS251_J16 ||
             R.Type == ELF::R_MCS251_J11)
                ? 2
            : R.Type == ELF::R_MCS251_24                                  ? 3
            : R.Type == ELF::R_MCS251_LO8 || R.Type == ELF::R_MCS251_MID8 ||
                      R.Type == ELF::R_MCS251_HI8 ||
                      R.Type == ELF::R_MCS251_PC8
                ? 1
                : 0;
        // The whole field, and nothing beyond it, must already live inside
        // the section (the same bound the write path enforces).
        if (!Width || R.Offset + Width > S->Size ||
            R.Offset + Width > S->Data.size())
          continue;
        P6Field F;
        F.Width = Width;
        for (uint32_t I = 0; I != Width; ++I) {
          auto It = Image.find(S->Address + R.Offset + I);
          if (It == Image.end())
            break;
          F.Before[I] = It->second;
        }
        P6Original[{S, R.Offset}] = F;
        P6Writes[{S, R.Offset}] = 0;
        Plant.push_back({S, R.Offset});
      }
    }
    for (const P6Key &K : Plant) {
      const uint32_t W = P6Original[K].Width;
      for (uint32_t I = 0; I != W; ++I)
        Image[K.first->Address + K.second + I] = P6Sentinel[I];
    }
  }
  const bool P6OK = applyRelocationsImpl(P6Path ? &P6Writes : nullptr);
  if (P6Path) {
    if (P6OK)
      // Success: re-sync Image from the section bytes over exactly the
      // sentineled field widths (the record pass wrote them in lock-step and
      // the sentinel never touched anything else), so an accidentally
      // enabled trace can never corrupt a produced image.  The failure path
      // is left as-is (no output is emitted there), which is exactly what
      // the trace must show: an unwritten field keeps its sentinel.
      for (const auto &E : P6Original) {
        InputSection *S = E.first.first;
        const uint32_t O = E.first.second;
        for (uint32_t I = 0; I != E.second.Width; ++I)
          if (O + I < S->Data.size())
            Image[S->Address + O + I] = S->Data[O + I];
      }
    std::error_code EC;
    raw_fd_ostream OS(P6Path, EC);
    if (!EC) {
      auto Hex2 = [](int V) {
        if (V < 0)
          return std::string("--");
        const char *D = "0123456789abcdef";
        std::string S = "0x";
        S += D[(V >> 4) & 0xf];
        S += D[V & 0xf];
        return S;
      };
      // B6: the trace reports exactly the field's own bytes (width from the
      // relocation type), so a width-1 probe cannot be misread as a 2-byte
      // field and the reader can compare the full field byte for byte.
      auto Bytes = [&](const InputSection *S, uint32_t O, uint32_t W,
                       bool After) {
        std::string Out;
        for (uint32_t I = 0; I != W; ++I) {
          if (I)
            Out += ",";
          int V = -1;
          if (After) {
            auto It = Image.find(S->Address + O + I);
            if (It != Image.end())
              V = It->second;
          } else {
            auto It = P6Original.find({const_cast<InputSection *>(S), O});
            if (It != P6Original.end() && I < 3)
              V = It->second.Before[I];
          }
          Out += Hex2(V);
        }
        return Out;
      };
      OS << "P6 fields=" << P6Original.size() << " result="
         << (P6OK ? "ok" : "fail") << "\n";
      for (const auto &E : P6Original) {
        const InputSection *S = E.first.first;
        const uint32_t O = E.first.second;
        const uint32_t W = E.second.Width;
        const unsigned Wr = P6Writes[E.first];
        OS << "P6 REL " << S->Name << " off=0x" << Twine::utohexstr(O)
           << " writes=" << Wr << " before=" << Bytes(S, O, W, /*After=*/false)
           << " after=" << Bytes(S, O, W, /*After=*/true) << " width=" << W
           << "\n";
      }
    }
  }
  return P6OK;
}

bool Linker::applyRelocationsImpl(std::map<std::pair<InputSection *, uint32_t>,
                                          unsigned> *P6Writes) {
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
        // BT13: bit relocations. BITADDR8 writes the resolved bit address; the
        // bit symbol's ELF Address field is never a byte address, so it is
        // looked up in the allocation ledger, not read from Target->Address.
        if (R.Type == R_MCS251_BITADDR8) {
          auto BitIt = BitOf.find(Target);
          if (BitIt == BitOf.end())
            return fail(Err, "MCS251 bit: BITADDR8 in " + S->Name +
                                 " target " + Target->Name +
                                 " is not a bit object or fixed bit reference");
          const uint32_t BitAddr = BitIt->second;
          if (R.Offset + 1 > S->Size || S->IsNobits)
            return fail(Err, "relocation writes outside PROGBITS section " +
                                 S->Name);
          if (!Written.insert({S.get(), R.Offset}).second)
            return fail(Err, "overlapping relocation in " + S->Name);
          const uint8_t B = static_cast<uint8_t>(BitAddr);
          S->Data[R.Offset] = B;
          Image[S->Address + R.Offset] = B;
          BitAddrFields.insert({S.get(), R.Offset});
          continue;
        }
        // A bit object has no byte address: an ordinary address relocation may
        // never target one.  Fixed SHN_ABS bit references are only identified
        // by a bit relocation consuming them, so they are left untouched here.
        if (Target->Defined && Target->Sec && Target->File &&
            Target->Sec == Target->File->BitSection)
          return fail(Err, "MCS251 bit: ordinary relocation in " + S->Name +
                               " targets bit object " + Target->Name);
        // X3: a 16-bit relocation field cannot carry an XDATA address. XSEG
        // symbols hold a 24-bit canonical address (bank DPXL + window); a
        // 16-bit field would silently truncate it. HI8/MID8/LO8 and the
        // full R_MCS251_24 are the sanctioned channels. This covers named
        // symbols and section-symbol folds alike (both resolve to the XSEG
        // InputSection). Fail closed; there is no escape switch.
        // G11 F9 (classification ruling): the gate follows the target's
        // STORAGE CLASS, not the Region name -- it therefore also covers a
        // FIXED-XDATA owned section and a bind-only XDATA PlacementNames
        // record (placementTargetClass is the single classifier shared by
        // F9/F10/F11).  This check precedes the overflow band and the write
        // below (P-6 gate-order invariant): an XDATA-class target on the
        // 16-bit channel is rejected, never silently low-16 truncated.
        if ((R.Type == ELF::R_MCS251_16 || R.Type == ELF::R_MCS251_J16) &&
            placementTargetClass(Target) == PSC_XDATA) {
          StringRef TargetName =
              Target->Name.empty()
                  ? StringRef(Target->Sec->Name)
                  : StringRef(Target->Name);
          return fail(Err, "XDATA symbol " + TargetName +
                               " truncated to 16 bits (use the 24-bit "
                               "relocation channel) in " +
                               S->Name);
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
                     : PlacementNames.count(IS->Name)
                         ? PlacementNames[IS->Name].Address
                     : Synth.count(IS->Name) ? Synth[IS->Name]
                     : 0;
        int64_t Value = static_cast<int64_t>(V) + R.Addend;
        // X3-R3/X3-R8: a stored pointer initializer must resolve inside its
        // target object.  The channel is identified by the resolved target's
        // final belonging, never by the referenced symbol's STT type:
        // STT_NOTYPE definitions, same-address aliases and cross-TU
        // resolutions are all legal XSEG reference forms (loadFile accepts
        // STT_NOTYPE), so the old "STT_OBJECT/STT_SECTION target" test left
        // the read-only image channel ungated and a NOTYPE-targeted pointer
        // serialized as 00 00 00 00 with exit 0 (X3-R8).
        //
        // An R_MCS251_24 whose target resolves to an XSEG-defined symbol is
        // a stored data pointer in every legal program: XSEG is NOBITS XDATA,
        // so no call/EJMP/jump-table slot can target it (the 16-bit channel
        // already fails closed against XSEG symbols above), while the
        // pointer-initializer containers live in XINIT/XDATA_INIT record
        // payloads, in non-executable read-only images (.rodata), and -- as
        // the backend actually emits them, MCS251AsmPrinter places read-only
        // globals in .text -- in executable read-only images alike.  A code
        // R_MCS251_24 (target STT_FUNC or a code-section symbol) never
        // resolves to an XSEG section and keeps the plain range checks.  The
        // X2 code address materialization uses the HI8/MID8/LO8 byte
        // channels, which keep the plain range checks.
        //
        // The gate: an XDATA object is one XSEG slice, so the final value
        // (symbol + addend) must land in [slice, slice+size].  One-past-end
        // is allowed (frozen ruling: a pointer may address the byte after
        // the object's last byte -- e.g. a loop end sentinel; it is not
        // dereferenceable but is a legal value); anything outside the
        // half-open-plus-one interval is a dangling or wrong-bank pointer
        // and fails the link.  Fail closed; no escape switch.
        // G11 F10 (classification ruling): the interval is the owned FIXED
        // section's [Address, Address+sh_size) or, for a bind-only XDATA
        // record, the PlacementNames [A, A+sizeof) (one-past-end alike).
        if (R.Type == ELF::R_MCS251_24 && placementTargetClass(Target) == PSC_XDATA) {
          uint64_t Lo, Hi;
          if (Target->Sec) {
            Lo = Target->Sec->Address;
            Hi = Lo + Target->Sec->Size; // one-past-end allowed
          } else {
            const PlacementNameEntry &PNE = PlacementNames[Target->Name];
            Lo = PNE.Address;
            Hi = Lo + PNE.Size;
          }
          if (Value < static_cast<int64_t>(Lo) ||
              Value > static_cast<int64_t>(Hi)) {
            StringRef TargetName =
                Target->Name.empty()
                    ? StringRef(Target->Sec->Name)
                    : StringRef(Target->Name);
            std::string Msg;
            raw_string_ostream OS(Msg);
            OS << "stored XDATA pointer in " << S->Name
               << " resolves outside the target object " << TargetName
               << ": " << format_hex(uint64_t(Value), 6, false) << " not in ["
               << format_hex(Lo, 6, false) << "," << format_hex(Hi, 6, false)
               << "] (one-past-end is the last legal value)";
            return fail(Err, Msg);
          }
        }
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
        // G11 F11: the control-target CODE gate follows the classification
        // too -- a bind-only FUNCTION target (no section) is admitted when
        // its PlacementNames record says Entity==function and
        // StorageClass==CODE (A is the entry); a bind-only OBJECT stays
        // rejected.  Owned FIXED-CODE sections carry IsCode from
        // mergePlacement.
        bool CodeControlTarget =
            Target->Sec ? Target->Sec->IsCode
                        : [&]() {
                            auto It = PlacementNames.find(Target->Name);
                            return It != PlacementNames.end() &&
                                   It->second.Entity == PE_FUNCTION &&
                                   It->second.StorageClass == PSC_CODE;
                          }();
        if ((R.Type == ELF::R_MCS251_J16 || R.Type == ELF::R_MCS251_J11) &&
            !CodeControlTarget)
          return fail(Err, "control relocation target is not CODE in " + S->Name);
        if (R.Type == ELF::R_MCS251_J16 &&
            (((P + 2) & 0xffffff) & 0xff0000) != (U & 0xff0000))
          return fail(Err, "J16 bank overflow in " + S->Name);
        auto Put = [&](uint32_t O, uint8_t B) {
          S->Data[O] = B;
          Image[S->Address + O] = B;
          if (P6Writes)
            ++(*P6Writes)[{S.get(), R.Offset}];
        };
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
  // BT13/BT15: re-validate every BITADDR8 field against the *final* image,
  // after all relocations (including ones that could overwrite the field's
  // opcode byte) have been applied.  This is the same boundary rule as the
  // load-time check, but on the resolved bytes, so a relocation that turns
  // D2 00 into 74 xx or a truncated/decoy byte pair cannot slip through.
  if (!validateBitAddrFields())
    return false;
  if (IrqMode && !applyVectorJumps())
    return false;
  if (IrqMode && !validateIRQFinalAssets())
    return false;
  return true;
}

// BT13/BT15: final (post-relocation) BITADDR8 field validation.  Every
// recorded bit-address field must be the operand of a real bit instruction in
// the completed image: the section stream must decode as instructions with a
// bit opcode immediately before the field.  A field that no longer sits on a
// bit-instruction boundary -- because another relocation rewrote its opcode,
// or because the surrounding bytes were never a valid instruction stream -- is
// a hard error.  This is deliberately independent of the numeric value of the
// preceding byte.
bool Linker::validateBitAddrFields() {
  // Group the recorded fields by section, in offset order, and decode the
  // FINAL image with the same full-stream rule used before relocation.  The
  // whole section is decoded to its end, so a field placed inside a
  // truncated trailing instruction (or a stream that stops decoding) fails
  // closed even when the field's own bytes look right.
  std::map<InputSection *, std::vector<size_t>> Fields;
  for (const auto &P : BitAddrFields) {
    if (P.second >= P.first->Data.size())
      return fail(Err, "MCS251 bit: BITADDR8 field outside " + P.first->Name);
    Fields[P.first].push_back(P.second);
  }
  for (auto &E : Fields) {
    InputSection *S = E.first;
    std::vector<size_t> &Offs = E.second;
    llvm::sort(Offs);
    Offs.erase(std::unique(Offs.begin(), Offs.end()), Offs.end());
    if (!validateBitAddrStream(ArrayRef<uint8_t>(S->Data), S->Name, Offs, Err))
      return false;
  }
  return true;
}

// Apply the synthesized vector slots (shared ISRVectorCount):
// R_MCS251_24 semantics against the
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
  // BT13/BT14 ownership is unconditional: a byte holding an automatic bit
  // object belongs to the bit allocator for initialization, so *no* input
  // XINIT record may target it -- whether or not a __mcs251_globals_init
  // walker happens to be linked.  This runs before the walker gate below so
  // the constraint cannot be bypassed by omitting the CRT (the previous
  // walker-gated placement let a conflicting object link cleanly when the
  // walker was absent).  A byte owned only by a fixed reference is the user's
  // and is not restricted; the synthesized record section is exempt.
  for (InputSection *S : AllSections) {
    if (S->Region != "XINIT" || S->Synthesized)
      continue;
    size_t SOffset = 0;
    while (SOffset != S->Data.size()) {
      // Structural record errors are deliberately not reported here: the
      // walker-gated loop below owns that diagnostic order, so this pre-pass
      // stops at the first record it cannot parse and leaves the message to
      // the established path.  Only the ownership conflict is emitted here.
      if (S->Data.size() - SOffset < 6)
        break;
      ArrayRef<uint8_t> Record(S->Data);
      uint32_t Destination = (uint32_t(Record[SOffset]) << 8) |
                             Record[SOffset + 1];
      uint32_t ObjectSize = (uint32_t(Record[SOffset + 2]) << 8) |
                            Record[SOffset + 3];
      uint32_t PayloadSize = (uint32_t(Record[SOffset + 4]) << 8) |
                             Record[SOffset + 5];
      if (!ObjectSize || (PayloadSize != 0 && PayloadSize != ObjectSize) ||
          PayloadSize > S->Data.size() - SOffset - 6)
        break;
      const uint32_t DestEnd = Destination + ObjectSize;
      for (uint32_t B = Destination; B < DestEnd; ++B)
        BitInputXInitDest[B] = S->Name;
      for (const auto &P : BitMask) {
        const uint32_t B = P.first;
        if (Destination < B + 1 && B < DestEnd)
          return fail(Err, "MCS251 bit: XINIT record in " + S->Name +
                               " initializes byte 0x" + Twine::utohexstr(B) +
                               " owned by automatic bit objects; the bit "
                               "allocator owns that initialization and no "
                               "input XINIT record may target it");
      }
      SOffset += 6 + PayloadSize;
    }
  }

  bool InitializerPresent = false;
  for (const auto &F : Files)
    for (const InputSymbol &S : F->Symbols)
      if (S.Name == "__mcs251_globals_init" && S.Defined)
        InitializerPresent = true;
  if (!InitializerPresent) {
    // A nonzero automatic bit initial value needs the globals-init walker to
    // apply its synthesized record.  Without one the value would be silently
    // dropped (the section would sit in ROM unconsumed), and NOBITS must never
    // be trusted as power-on zero: fail closed instead of emitting a bit
    // object whose declared initial value is not realized.
    //
    // BT14: under the bit profile the values ride the synthesized
    // .mcs251.bittable applied by the CRT's __mcs251_bit_init walker, whose
    // presence validateBitProfileSet() already proved; the globals walker is
    // then not the initializer for bit values (ordinary XINIT records have
    // none here).  This branch keeps the S1 fail-closed behaviour untouched
    // for profile-less links.
    if (!BitValue.empty() && !BitProfileFile)
      return fail(Err, "MCS251 bit: a nonzero bit initial value requires a "
                       "__mcs251_globals_init initializer in the link; none "
                       "is present, so the value could not be applied");
    return true;
  }
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
        //
        // G8: EDATA slices are owned DSEG-class initialization targets too.
        // The record format is unchanged (u16 destination; the Driver already
        // rejects --edata-end > 0xffff, so no legal EDATA address needs more
        // than the u16 walker the CRT already runs), and the sparse record
        // covers a dispersed EDATA layout exactly as it does a dispersed
        // DSEG one.  Without this branch every migrated object's clear/copy
        // record would be rejected here -- a link that succeeded in layout
        // but could not start.
        // G11 F5: a FIXED AS0-DATA entity is an owned initialization target
        // as well (clang emits the sparse record with dest = A).
        if ((D->Region == "DSEG" || D->Region == "EDATA" ||
             D->Region == "DATA_ABS" || D->Region == "BSEG_BYTES" ||
             (D->Region == "FIXED" && D->PlacementClass == PSC_AS0_DATA)) &&
            D->Size != 0 && Destination >= D->Address &&
            rangeFits(uint64_t(Destination) - D->Address, ObjectSize,
                      D->Size)) {
          WithinOneSlice = true;
          break;
        }
      // BT13/BT14: a byte lld reserved for an automatic bit object (outside
      // every BSEG_BYTES pool) is an owned initialization target too.  Only
      // bytes with a nonzero synthesized value are eligible, so this cannot
      // widen the accepted set for unrelated input records.
      if (!WithinOneSlice)
        for (const auto &P : BitValue)
          if (Destination == P.first && ObjectSize == 1) {
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

// X3: parse and validate every `.mcs251.xdata_init` record after layout and
// relocation application (the bank/window fields are relocation-written, so
// the values checked here are the final ones).  Record v1 (frozen):
//   u8 bank, u16 window (BE), u16 object_size, u16 payload_size, payload
// with payload_size == 0 meaning "clear only".  For every record:
//   * structure must parse (header whole, payload inside the section),
//   * the destination [bank:window, bank:window+object_size) must stay in
//     the 24-bit space and inside ONE 64K window (belt-and-braces: the
//     allocator already refuses to straddle; a hand-made record that does
//     is a hard error, never silently wrapped),
//   * the destination must lie entirely inside one allocated XSEG slice
//     (last byte included; crossing out of the slice is an error),
//   * no two records may overlap in their destinations.
// G13b (D4): the linker does not synthesize XDATA_INIT bytes, except for
// clear-only v1 records synthesized from an input XSEG section carrying
// SHF_MCS251_XSEG_SPLIT; every synthesized record is emitted into the
// linker-owned XDATA_INIT output and is subject to the same XDATA_INIT
// structural, single-window, XSEG-containment, and non-overlap validation
// as input records.  The CRT consumer loop is the X4 slice.
bool Linker::validateXDATAInit() {
  struct DestUse {
    uint64_t Lo, Hi;
    std::string Sec;
  };
  std::vector<DestUse> Dests;
  for (InputSection *S : AllSections) {
    if (S->Region != "XDATA_INIT")
      continue;
    size_t Offset = 0;
    while (Offset != S->Data.size()) {
      if (S->Data.size() - Offset < 7)
        return fail(Err, "truncated XDATA_INIT record in " + S->Name);
      ArrayRef<uint8_t> Record(S->Data);
      const uint32_t Bank = Record[Offset];
      const uint32_t Window = (uint32_t(Record[Offset + 1]) << 8) |
                              Record[Offset + 2];
      const uint32_t ObjectSize = (uint32_t(Record[Offset + 3]) << 8) |
                                  Record[Offset + 4];
      const uint32_t PayloadSize = (uint32_t(Record[Offset + 5]) << 8) |
                                   Record[Offset + 6];
      if (!ObjectSize || (PayloadSize != 0 && PayloadSize != ObjectSize) ||
          PayloadSize > S->Data.size() - Offset - 7)
        return fail(Err, "invalid XDATA_INIT record in " + S->Name);
      const uint64_t Dest = (uint64_t(Bank) << 16) | Window;
      const uint64_t DestEnd = Dest + ObjectSize;
      auto Hex = [](uint64_t V) { return "0x" + Twine::utohexstr(V); };
      if (DestEnd > 0x1000000)
        return fail(Err, "XDATA_INIT destination overflows the 24-bit XDATA "
                         "space: [" +
                             Hex(Dest) + "," + Hex(DestEnd) + ") in " +
                             S->Name);
      if ((Dest ^ (DestEnd - 1)) & 0xff0000ULL)
        return fail(Err, "XDATA_INIT record in " + S->Name +
                             " spans a 64K window boundary: [" + Hex(Dest) +
                             "," + Hex(DestEnd) +
                             ") -- an XDATA object never straddles a bank");
      bool WithinOneSlice = false;
      for (InputSection *D : AllSections)
        // G11 F4: a FIXED-XDATA entity is an owned initialization target
        // too (clang emits xdata_init records with dest = A).
        if ((D->Region == "XSEG" ||
             (D->Region == "FIXED" && D->PlacementClass == PSC_XDATA)) &&
            D->Size != 0 && Dest >= D->Address &&
            DestEnd <= D->Address + D->Size) {
          WithinOneSlice = true;
          break;
        }
      if (!WithinOneSlice)
        return fail(Err, "XDATA_INIT destination is not within one XSEG "
                         "slice: [" +
                             Hex(Dest) + "," + Hex(DestEnd) + ") in " +
                             S->Name);
      // G13b SM4: for a split-XSEG section the link-time form of
      // "zero-initialized" is "no INPUT record covers any of it" -- a
      // nonzero initializer would have made the producer emit a record
      // (rejected there for >64K objects), so a covering record can only be
      // a hand-made ELF or a broken producer gate; either is a hard link
      // error.  The check is INPUT-record-only: a SYNTHESIZED record's
      // destination necessarily lies inside its own flagged section and
      // must not be rejected here (the Dests loop below consumes every
      // section, hence the Synthesized split).
      if (!S->Synthesized)
        for (InputSection *D : AllSections)
          if (D->Region == "XSEG" && D->XsegSplit && D->Size != 0 &&
              Dest < D->Address + D->Size && D->Address < DestEnd)
            return fail(Err, "input XDATA_INIT record in " + S->Name +
                                 " covers split-XSEG section " + D->Name +
                                 ": [" + Hex(Dest) + "," + Hex(DestEnd) +
                                 ")");
      Dests.push_back({Dest, DestEnd, S->Name});
      Offset += 7 + PayloadSize;
    }
  }
  if (Dests.size() < 2)
    return true;
  llvm::sort(Dests, [](const DestUse &A, const DestUse &B) {
    return A.Lo != B.Lo ? A.Lo < B.Lo : A.Hi < B.Hi;
  });
  for (size_t I = 1; I != Dests.size(); ++I)
    if (Dests[I].Lo < Dests[I - 1].Hi) {
      auto Hex = [](uint64_t V) { return "0x" + Twine::utohexstr(V); };
      return fail(Err, "XDATA_INIT record destinations overlap: [" +
                           Hex(Dests[I - 1].Lo) + "," + Hex(Dests[I - 1].Hi) +
                           ") from " + Dests[I - 1].Sec + " and [" +
                           Hex(Dests[I].Lo) + "," + Hex(Dests[I].Hi) +
                           ") from " + Dests[I].Sec);
    }
  return true;
}

// E2: static parameter-slot ABI reentrancy diagnosis (COMPILER-ASSESSMENT
// 20260910.md section 5).  The C ABI passes arguments in static per-object
// parameter slots (.mcs251.DSEG.* / .mcs251.OSEG.*).  A function invoked both
// from foreground code and from an ISR - or from two ISRs that can preempt
// each other - can have its not-yet-consumed arguments overwritten by the
// other execution context; full register save/restore cannot protect this
// shared static storage.
//
// What is decidable at link time (used here):
//   - ISR identity: the exact registered entry symbols collected in
//     IsrSymbols by validateISRIdentitiesAndRegistrations().
//   - Direct call edges: control-flow relocations (R_MCS251_24 / J16 / J11 /
//     PC8) in ALLOC code sections, attributed to the enclosing function
//     symbols of caller site and call target.
// The diagnosis propagates ISR/foreground context to a fixpoint and warns on
// every function reachable from BOTH contexts, or from two or more distinct
// registered ISRs.  Warning only: the link result is unaffected.
//
// Explicit coverage boundaries (also printed with every warning):
//   - Indirect calls (function addresses carried by data relocations such as
//     R_MCS251_16/LO8/MID8/HI8) are NOT call edges here; a call through a
//     pointer is invisible.
//   - Call sites without a relocation record are invisible.
//   - Prebuilt runtime-library objects that expose no relocation source for
//     their internal calls cannot be attributed.
//   - An edge whose target cannot be resolved to a function-containing
//     address, or whose call site lies outside every known function symbol,
//     is skipped.
//   - A function defined without st_size has no exact end: its presumed
//     interval stops at the next candidate symbol, so an interior label can
//     truncate it and call sites at or after that label are attributed to
//     the label or to nothing.
//   - Static parameter slots are attributed per DEFINING OBJECT only: the
//     linker sees no function-level slot ownership, so the warning lists the
//     DSEG/OSEG parameter-slot sections of the object that defines the
//     function.
void Linker::diagnoseIsrReentrancy(LinkerResult &Result) {
  // Function nodes: the CANONICAL OWNER symbols of per-section function
  // intervals in ALLOC code sections.  Graph nodes, call-edge endpoints and
  // context roots all use the same canonical identity.  Attribution rules:
  //
  //   0. Canonicalization of same-address groups: candidates in one section
  //      are grouped by symbol value; a group sharing one address (STT_FUNC
  //      aliases of an entry, entry labels of any other type) yields at most
  //      ONE interval and ONE graph node - the group's canonical owner -
  //      chosen by priority: (1) the registered ISR entry (exact IsrSymbols
  //      identity, so the root set and the call edges agree), (2) the default
  //      entry DefaultSym, (3) a defined STT_FUNC over any other type,
  //      (4) name-ascending.  Steps 1-4 only pick WHICH name denotes the
  //      group - every same-address alias denotes the identical code.  Without
  //      this, a local STT_FUNC alias of an ISR entry (same Value, same
  //      st_size) becomes a second node that steals the ISR's call-site
  //      attribution in Containing(), the ISR context never enters the call
  //      graph, and the diagnosis is silently muted (review round 3).
  //   1. Interval extent: a defined STT_FUNC owns the real function interval
  //      [Value, Value + Size) when its st_size is nonzero (the furthest
  //      explicit end in the group wins if same-address aliases disagree).  A
  //      zero-size STT_FUNC (hand-written asm without .size) owns the bounded
  //      gap [Value, next candidate symbol start) - or the section end when
  //      no candidate follows - so it can never silently claim the remainder
  //      of the section.  Coverage limit: without st_size the true function
  //      end is unrecoverable, so an interior label (any candidate symbol at
  //      a label offset) truncates the presumed gap there; call sites at or
  //      after that label inside a size-less function are attributed to the
  //      label or to nothing.  This limit is also named in every warning's
  //      coverage-boundary line.
  //   2. Normalization: same-address aliases and interior labels (STT_NOTYPE
  //      or any other type defined at or inside a STT_FUNC interval) own
  //      nothing, so a call site or call target inside the function is never
  //      attributed to the label.  Without this, a local assembler alias of
  //      an ISR entry steals the ISR's call-site attribution, the ISR context
  //      never enters the call graph, and the diagnosis is silently muted
  //      (round 2).
  //   3. Restricted NOTYPE fallback: an offset covered by no STT_FUNC
  //      interval is attributed ONLY to a STT_NOTYPE, as the owner of the
  //      bounded gap [Value, next candidate symbol start); symbols of any
  //      other type own nothing at uncovered addresses (the fallback used to
  //      apply to every non-FUNC type; narrowed in review round 3).  This
  //      fallback exists for the frozen CRT, whose BOOT entry
  //      (__mcs251_selfstart_boot), XINIT walker (__mcs251_globals_init) and
  //      walker-local labels are plain NOTYPE symbols not enclosed by any
  //      STT_FUNC.  The boundary is what keeps the fallback restricted: a
  //      fallback label never owns past the next symbol of any kind, so it
  //      can never swallow a call that lands inside a real function.
  struct Interval {
    uint64_t Lo = 0;
    uint64_t Hi = 0;
    InputSymbol *Sym = nullptr;
  };
  std::map<InputSection *, std::vector<InputSymbol *>> Candidates;
  for (auto &F : Files)
    for (auto &S : F->Symbols)
      if (S.Defined && !S.Name.empty() && S.Type != ELF::STT_SECTION &&
          S.Sec && S.Sec->IsAlloc && S.Sec->IsCode)
        Candidates[S.Sec].push_back(&S);
  std::map<InputSection *, std::vector<Interval>> Funcs;
  std::vector<InputSymbol *> FuncNodes; // graph nodes == canonical owners
  for (auto &P : Candidates) {
    InputSection *Sec = P.first;
    std::vector<InputSymbol *> &V = P.second;
    std::sort(V.begin(), V.end(),
              [](const InputSymbol *A, const InputSymbol *B) {
                if (A->Value != B->Value)
                  return A->Value < B->Value;
                if ((A->Type == ELF::STT_FUNC) != (B->Type == ELF::STT_FUNC))
                  return A->Type == ELF::STT_FUNC;
                return A->Name < B->Name;
              });
    // Candidate starts (sorted, unique): the boundaries that cap zero-size
    // functions and NOTYPE fallback gaps.
    std::vector<uint64_t> Starts;
    for (InputSymbol *S : V)
      Starts.push_back(S->Value);
    Starts.erase(std::unique(Starts.begin(), Starts.end()), Starts.end());
    auto NextBound = [&](uint64_t Val) -> uint64_t {
      auto It = std::upper_bound(Starts.begin(), Starts.end(), Val);
      return It != Starts.end() ? *It : uint64_t(Sec->Size);
    };
    auto Rank = [&](InputSymbol *S) {
      if (IsrSymbols.count(S))
        return 0;
      if (S == DefaultSym)
        return 1;
      return 2;
    };
    // Rule 0 tiebreak: does A make a better canonical owner than B?
    auto OwnsOver = [&](InputSymbol *A, InputSymbol *B) {
      if (Rank(A) != Rank(B))
        return Rank(A) < Rank(B);
      if ((A->Type == ELF::STT_FUNC) != (B->Type == ELF::STT_FUNC))
        return A->Type == ELF::STT_FUNC;
      return A->Name < B->Name;
    };
    std::vector<Interval> Ints;
    // Rules 0-3: process same-address candidate groups in ascending address
    // order; every group yields at most ONE interval and ONE node (rule 0),
    // so an alias can never form a second graph node.
    for (size_t I = 0; I < V.size();) {
      size_t J = I;
      while (J < V.size() && V[J]->Value == V[I]->Value)
        ++J;
      uint64_t Addr = V[I]->Value;
      InputSymbol *Owner = V[I];
      bool HasFunc = false;
      uint64_t Hi = 0; // furthest explicit STT_FUNC end in the group
      for (size_t K = I; K < J; ++K) {
        InputSymbol *S = V[K];
        if (OwnsOver(S, Owner))
          Owner = S;
        if (S->Type == ELF::STT_FUNC) {
          HasFunc = true;
          if (S->Size)
            Hi = std::max(Hi, uint64_t(S->Value) + S->Size);
        }
      }
      // Rule 2: the address lies inside an already-built function interval
      // (interior label, or alias of a function starting earlier) - a
      // non-FUNC group there owns nothing and creates no node; Containing()
      // attributes such offsets to the enclosing function.
      bool Covered = false;
      for (const Interval &FI : Ints)
        if (Addr >= FI.Lo && Addr < FI.Hi) {
          Covered = true; // rule 2: alias/interior label of a real function
          break;
        }
      // The group's node.  A group with any STT_FUNC keeps its interval even
      // when nested inside another function (rule 1); otherwise rule 3's
      // narrowing applies: only a STT_NOTYPE may own a fallback gap, any
      // other type owns nothing at an uncovered address.
      InputSymbol *Node = nullptr;
      if (HasFunc) {
        Node = Owner;
      } else if (!Covered) {
        for (size_t K = I; K < J; ++K)
          if (V[K]->Type == ELF::STT_NOTYPE &&
              (!Node || OwnsOver(V[K], Node)))
            Node = V[K];
      }
      if (Node && (!Covered || HasFunc)) {
        if (!Hi)
          Hi = NextBound(Addr); // rule 1: size-less group owns the bounded gap
        Ints.push_back({Addr, Hi, Node});
        FuncNodes.push_back(Node);
      }
      I = J;
    }
    std::sort(Ints.begin(), Ints.end(),
              [](const Interval &A, const Interval &B) {
                if (A.Lo != B.Lo)
                  return A.Lo < B.Lo;
                if (A.Hi != B.Hi)
                  return A.Hi < B.Hi;
                return A.Sym->Name < B.Sym->Name;
              });
    Funcs[Sec] = std::move(Ints);
  }
  auto Containing = [&](InputSection *Sec, uint64_t Off) -> InputSymbol * {
    auto It = Funcs.find(Sec);
    if (It == Funcs.end())
      return nullptr;
    InputSymbol *Best = nullptr;
    uint64_t BestLo = 0, BestHi = 0;
    for (const Interval &I : It->second) {
      if (I.Lo > Off)
        break;
      if (Off >= I.Hi)
        continue;
      // Innermost owner wins among (malformed) overlapping intervals:
      // the closest start, then the shortest span, then the name.
      if (!Best || I.Lo > BestLo ||
          (I.Lo == BestLo &&
           (I.Hi < BestHi || (I.Hi == BestHi && I.Sym->Name < Best->Name)))) {
        Best = I.Sym;
        BestLo = I.Lo;
        BestHi = I.Hi;
      }
    }
    return Best;
  };
  auto ShortFile = [](InputSymbol *S) {
    StringRef P = S->File->Path;
    return P.substr(P.rfind('/') + 1).str();
  };

  // Direct call edges from control-flow relocations.
  auto IsControlType = [](uint32_t T) {
    return T == ELF::R_MCS251_24 || T == ELF::R_MCS251_J16 ||
           T == ELF::R_MCS251_J11 || T == ELF::R_MCS251_PC8;
  };
  std::set<std::pair<InputSymbol *, InputSymbol *>> EdgeSet;
  std::vector<std::pair<InputSymbol *, InputSymbol *>> Edges;
  for (auto &F : Files)
    for (auto &S : F->Sections) {
      if (!S->IsAlloc || !S->IsCode || S->IsNobits)
        continue;
      for (const Relocation &R : S->Relocs) {
        if (!IsControlType(R.Type))
          continue;
        InputSymbol *IS = findSymbol(*F, R.Sym);
        if (!IS)
          continue;
        InputSymbol *Target = IS;
        if (!IS->Defined) {
          auto It = Globals.find(IS->Name);
          if (It == Globals.end())
            continue; // unresolved cross-object: documented boundary
          Target = It->second;
        }
        InputSymbol *To = nullptr;
        if (IS->Type == ELF::STT_SECTION) {
          if (R.Addend < 0 || !IS->Sec)
            continue;
          To = Containing(IS->Sec,
                          uint64_t(IS->Value) + uint64_t(R.Addend));
        } else if (Target->Defined && Target->Sec &&
                   Target->Type != ELF::STT_SECTION) {
          To = Containing(Target->Sec,
                          uint64_t(Target->Value) + uint64_t(R.Addend));
          if (!To)
            To = Target; // target outside every known function interval
        }
        InputSymbol *From = Containing(S.get(), R.Offset);
        if (!From || !To || From == To)
          continue;
        // A control edge into a registered ISR or the default entry is
        // already a hard R3 error; such edges never survive to this stage.
        if (To == DefaultSym || IsrSymbols.count(To))
          continue;
        if (EdgeSet.insert({From, To}).second)
          Edges.push_back({From, To});
      }
    }

  // Context fixpoint.  Bit0 = reachable in ISR context, bit1 = reachable in
  // foreground context.  Roots[X] = the distinct registered ISR entries from
  // which X is reachable along ISR-context paths.
  //
  // Foreground roots are the functions no collected call edge enters: module
  // entry, reset-chain heads, uncalled functions.  Registered ISR entries and
  // the default entry are entered by hardware, never foreground roots.
  std::map<InputSymbol *, unsigned> Ctx;
  std::map<InputSymbol *, std::set<InputSymbol *>> Roots;
  {
    std::set<InputSymbol *> Called;
    for (const auto &E : Edges)
      Called.insert(E.second);
    for (InputSymbol *S : FuncNodes)
      if (!Called.count(S) && !IsrSymbols.count(S) && S != DefaultSym)
        Ctx[S] |= 2;
  }
  for (InputSymbol *ISR : IsrSymbols) {
    Ctx[ISR] |= 1;
    Roots[ISR].insert(ISR);
  }
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (const auto &E : Edges) {
      auto CI = Ctx.find(E.first);
      if (CI == Ctx.end() || !CI->second)
        continue;
      const unsigned C = CI->second;
      if (C & 1) {
        if (!(Ctx[E.second] & 1)) {
          Ctx[E.second] |= 1;
          Changed = true;
        }
        for (InputSymbol *Root : Roots[E.first])
          if (Roots[E.second].insert(Root).second)
            Changed = true;
      }
      if ((C & 2) && !(Ctx[E.second] & 2)) {
        Ctx[E.second] |= 2;
        Changed = true;
      }
    }
  }

  // Warned functions: ISR+foreground mix, or two or more ISR roots.
  std::vector<InputSymbol *> Warned;
  for (const auto &P : Ctx) {
    InputSymbol *X = P.first;
    if (X == DefaultSym || IsrSymbols.count(X))
      continue;
    const bool Mixed = (P.second & 3) == 3;
    const bool MultiISR = Roots[X].size() >= 2;
    if (Mixed || MultiISR)
      Warned.push_back(X);
  }
  if (Warned.empty())
    return;
  std::sort(Warned.begin(), Warned.end(),
            [](const InputSymbol *A, const InputSymbol *B) {
              if (A->File->Path != B->File->Path)
                return A->File->Path < B->File->Path;
              return A->Name < B->Name;
            });

  std::string Out;
  raw_string_ostream OS(Out);
  for (InputSymbol *X : Warned) {
    const bool Mixed = (Ctx[X] & 3) == 3;
    const bool MultiISR = Roots[X].size() >= 2;
    if (Mixed)
      OS << "mcs251-lld: warning: ISR reentrancy: function '" << X->Name
         << "' (" << ShortFile(X)
         << ") is called from both ISR and foreground code\n";
    if (MultiISR)
      OS << "mcs251-lld: warning: ISR reentrancy: function '" << X->Name
         << "' (" << ShortFile(X) << ") is called from multiple registered "
         << "ISRs\n";
    OS << "mcs251-lld: warning: ISR reentrancy:   ISR entries reaching '"
       << X->Name << "':";
    // Sort by rendered reference: the root set iterates in pointer order,
    // which is not deterministic.
    std::vector<std::string> RootRefs;
    for (InputSymbol *R : Roots[X])
      RootRefs.push_back(R->Name + " (" + ShortFile(R) + ")");
    std::sort(RootRefs.begin(), RootRefs.end());
    for (const std::string &Ref : RootRefs)
      OS << " " << Ref;
    OS << '\n';
    // Direct foreground-context callers, deduplicated, stable order.
    std::set<std::string> Seen;
    std::vector<std::string> FG;
    for (const auto &E : Edges)
      if (E.second == X && (Ctx.count(E.first) && (Ctx[E.first] & 2))) {
        std::string Ref = E.first->Name + " (" + ShortFile(E.first) + ")";
        if (Seen.insert(Ref).second)
          FG.push_back(Ref);
      }
    if (!FG.empty()) {
      std::sort(FG.begin(), FG.end());
      OS << "mcs251-lld: warning: ISR reentrancy:   direct foreground callers "
         << "of '" << X->Name << "':";
      for (const std::string &Ref : FG)
        OS << " " << Ref;
      OS << '\n';
    }
    // Static parameter slots of the defining object (per-object attribution).
    OS << "mcs251-lld: warning: ISR reentrancy:   parameter slots of "
       << ShortFile(X) << " (per defining object):";
    bool AnySlot = false;
    std::vector<InputSection *> Slots;
    for (const auto &SP : X->File->Sections) {
      InputSection *S = SP.get();
      if (S->IsAlloc && (S->Region == "DSEG" || S->Region == "EDATA" ||
                         S->Region == "OSEG") &&
          S->Size)
        Slots.push_back(S);
    }
    std::sort(Slots.begin(), Slots.end(),
              [](const InputSection *A, const InputSection *B) {
                return A->Address < B->Address;
              });
    for (InputSection *S : Slots) {
      AnySlot = true;
      // Same hex conventions as the map rows: width 6 includes the "0x"
      // prefix, sizes print minimal width with their own "0x".
      OS << " " << S->Name << " " << format_hex(S->Address, 6, false)
         << " +" << format_hex(S->Size, 0, false);
    }
    if (!AnySlot)
      OS << " none found";
    OS << '\n';
  }
  OS << "mcs251-lld: warning: ISR reentrancy:   coverage boundary: only "
     << "direct control-flow relocations are analyzed; indirect calls, call "
     << "sites without relocation records, prebuilt runtime objects, and "
     << "unresolved cross-object targets are not covered; functions without "
     << "st_size have no exact bounds, so an interior label can truncate "
     << "their presumed interval\n";
  OS.flush();
  Result.Diagnostics = std::move(Out);
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
    for (const PlacementNoteRecord &R : F->PlacementRecords)
      Out << "  placement stable=" << R.Stable
          << " class=" << placementClassName(R.StorageClass)
          << " entity=" << (R.Entity == PE_FUNCTION ? "function" : "object")
          << " own=" << (R.Ownership == PO_BIND ? "bind" : "owned")
          << " addr=" << format_hex(R.Address, 6, false)
          << " size=" << R.Size << " align=" << R.Align
          << " flags=" << R.Flags << '\n';
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
  // BT13: bit allocation rows, emitted only when bit objects exist so links
  // without them keep byte-identical maps (frozen artifacts).  Each row names
  // the bit object, its bit address, backing byte/index, owner and init value.
  if (!BitMask.empty() || !BitOf.empty()) {
    auto Hex2 = [](uint32_t V) {
      const char *D = "0123456789abcdef";
      std::string S = "0x";
      S += D[(V >> 4) & 0xf];
      S += D[V & 0xf];
      return S;
    };
    std::vector<std::pair<std::string, uint32_t>> Bits;
    for (const auto &P : BitOf) {
      // LOCAL objects are file-scoped: qualify the name with the defining
      // object so two same-named statics are not confused in the map.
      std::string N = P.first->Name;
      if (P.first->Bind == ELF::STB_LOCAL && P.first->File)
        N = P.first->File->Path + ":" + N;
      auto It = P.second <= 0x7f
                    ? BitMask.find(BitWindowBase + (P.second >> 3))
                    : BitMask.end();
      const bool Automatic = It != BitMask.end() &&
                             (It->second & (1u << (P.second & 7)));
      Bits.push_back({N + (Automatic ? "" : " fixed"), P.second});
    }
    llvm::sort(Bits, [](const auto &A, const auto &B) {
      return A.second != B.second ? A.second < B.second : A.first < B.first;
    });
    for (const auto &P : Bits) {
      Out << "BIT " << P.first << " = " << Hex2(P.second);
      if (P.second > 0x7f) {
        // SFR bit: no RAM backing byte; byte/index are the SFR encoding.
        Out << " sfr byte " << Hex2(P.second & 0xf8) << " index "
            << (P.second & 7) << " init 0\n";
        continue;
      }
      const uint32_t Byte = BitWindowBase + (P.second >> 3);
      Out << " byte " << Hex2(Byte) << " index " << (P.second & 7);
      const bool Automatic = BitMask.count(Byte) != 0;
      if (Automatic) {
        auto V = BitValue.find(Byte);
        Out << " init "
            << ((V != BitValue.end() && (V->second & (1u << (P.second & 7))))
                    ? 1
                    : 0)
            << '\n';
      } else {
        // A fixed-reference bit is never initialized by the allocator.  Report
        // the user's own input-XINIT coverage honestly instead of the value 0:
        // "xinit" means an input record writes this byte, "none" means the bit
        // has no declared initial value (which is NOT the same as init 0).
        Out << " init "
            << (BitInputXInitDest.count(Byte) ? "user-xinit" : "none") << '\n';
      }
    }
    for (const auto &P : BitMask) {
      auto V = BitValue.find(P.first);
      const uint32_t Value = V == BitValue.end() ? 0 : V->second;
      // BT13 auditability: name the physical owner of the byte and how the
      // bits reach their initial value.  A pool byte is produced by the CRT's
      // explicit window clear followed by the synthesized XINIT record; a
      // non-pool byte has no clear, so only its nonzero bits are written.
      auto O = BitByteOrigin.find(P.first);
      Out << "BITBYTE " << Hex2(P.first) << " mask " << Hex2(P.second)
          << " value " << Hex2(Value) << " owner "
          << (O == BitByteOrigin.end() ? std::string("?") : O->second)
          << " init "
          << (BitProfileFile
                  ? std::string("bit-rmw")
                  : (BitPoolByte.count(P.first)
                         ? (Value ? "crt-clear+xinit" : "crt-clear")
                         : (Value ? "xinit" : "none")))
          << '\n';
    }
    // BT13 auditability: a byte owned ONLY by a fixed reference carries no
    // automatic mask, so it has no BITBYTE row above.  Emit one for each such
    // RAM byte with its owner and init policy, so a fixed RAM reference is
    // fully traceable and "no declared init" is distinguishable from
    // "initialized to 0" (user-xinit vs none).
    for (const auto &P : BitFixedByte) {
      if (BitMask.count(P.first))
        continue; // Covered by the automatic row above.
      const bool InPool = BitPoolByte.count(P.first) != 0;
      const bool UserXInit = BitInputXInitDest.count(P.first) != 0;
      // BT14: under the bit profile a fixed-only byte is never touched by the
      // CRT (no window clear exists and the byte carries no mask), so it
      // reports the user's own policy; only the S1 CRT's clear would justify
      // the crt-clear wording.
      Out << "BITBYTE " << Hex2(P.first) << " mask 0x00 value 0x00 owner "
          << "fixed " << P.second << " init "
          << (InPool && !BitProfileFile
                  ? (UserXInit ? "crt-clear+user-xinit" : "crt-clear")
                  : (UserXInit ? "user-xinit" : "none"))
          << '\n';
    }
  }
  // T07 steps 17-19: in IRQ mode the map carries exactly the 127
  // synthesized vector rows (slot as at least two decimal digits, so
  // 100..126 are three wide; address as 0x + 6 lowercase hex digits, the
  // format_hex width 8 counting the 0x prefix). No ISR-safe, stack or
  // priority fields exist anywhere.
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
            S.Bind == Bind) {
          // BT13: a bit object has no byte address (its st_value is a bit
          // metadata record offset), so it never enters the byte-address
          // symbol table.  The bit allocation is reported in the map instead.
          if (S.Sec && F->BitSection == S.Sec)
            continue;
          Out.push_back({S.Name, S.Address, S.Size, S.Bind, S.Type, false});
        }
  };
  Emit(ELF::STB_LOCAL);
  Emit(ELF::STB_GLOBAL);
  for (const auto &P : Synth)
    Out.push_back({P.first, P.second, 0, ELF::STB_GLOBAL, ELF::STT_NOTYPE,
                   true});
  // G11 §3.3 (rev 5 ordering + naming rulings): the bind-only placement
  // rows are APPENDED strictly after the existing synth sequence (the E5
  // append-only byte promise), in PlacementNames key order -- std::map's
  // lexicographic iteration keeps same-input -> same-output replay.  The
  // name is the ELF symbol name (declaration name; the manifest-only
  // fallback is the stable symbol), never assumed equal to the stable.
  // Collision safety was already proven by validatePlacementSymbolNames().
  for (const auto &P : PlacementNames)
    Out.push_back({P.first, P.second.Address, 0, ELF::STB_GLOBAL,
                   ELF::STT_NOTYPE, true});
}

// G11-D2 (design §3/§5/§6.2): the read-only positioning snapshot.  Facts the
// collection honours:
//   * object identity is the Config.Inputs ordinal; the fingerprint is
//     SHA-256 over the ALREADY READ buffer, never a re-open of the path;
//   * every legal ALLOC input section participates -- NOBITS, fixed, legal
//     zero-length and every overlay group member each keep their own record;
//   * the DATA_EMPTY_PENDING -> "IGNORE" `.data` exemption has no record;
//   * synthesized sections (no original shndx) never get a fabricated
//     (object_id, shndx) identity;
//   * a G8-migrated section keeps storage_space=0 and reports its FINAL
//     (EDATA) address, because Region was rewritten before this point;
//   * an SHF_MCS251_XSEG_SPLIT section is one record whose slices follow the
//     frozen canonical algorithm (design §3.7): length = min(remaining,
//     0xffff, window tail), so one layout has exactly one byte encoding.
bool Linker::collectPositions(LinkerResult &Result) {
  for (const auto &F : Files) {
    SHA256 Hash;
    Hash.update(F->Buffer->getBuffer());
    Result.PositionObjects.push_back(Hash.final());
  }
  for (size_t ObjId = 0; ObjId != Files.size(); ++ObjId) {
    for (const auto &SP : Files[ObjId]->Sections) {
      const InputSection &S = *SP;
      if (!S.IsAlloc || S.Region == "IGNORE")
        continue;
      LinkerResult::PositionSection P;
      P.ObjectId = static_cast<uint32_t>(ObjId);
      P.InputShndx = S.Index;
      P.InputSize = static_cast<uint32_t>(S.Size);
      const StringRef R = S.Region;
      if (R == "CSEG" || R == "HOME" || R == "VECS" || R == "BOOT" ||
          R == "XINIT" || R == "XDATA_INIT")
        P.StorageSpace = PSC_CODE;
      else if (R == "XSEG")
        P.StorageSpace = PSC_XDATA;
      else if (R == "FIXED") {
        // mergePlacement() pins every fixed section's class or fails the
        // link; reaching here unset is an ordering bug, never an input
        // property, and must not be serialized as a storage_space value.
        if (S.PlacementClass > PSC_CODE)
          return fail(Err, "internal: fixed section without a placement "
                           "storage class: " + S.Name);
        P.StorageSpace = S.PlacementClass;
      } else // DSEG/EDATA/ISEG/SSEG/OSEG/REG/BSEG_BYTES/BIT_BANK/DATA_ABS
        P.StorageSpace = PSC_AS0_DATA;
      if (S.XsegSplit && S.Size) {
        uint64_t Offset = 0, Current = S.Address, Remaining = S.Size;
        while (Remaining) {
          const uint64_t WindowEnd = (Current & 0xff0000ULL) + 0x10000;
          const uint64_t Len =
              std::min(std::min(Remaining, uint64_t(0xffff)),
                       WindowEnd - Current);
          P.Slices.push_back({static_cast<uint32_t>(Offset),
                              static_cast<uint32_t>(Current),
                              static_cast<uint32_t>(Len)});
          Offset += Len;
          Current += Len;
          Remaining -= Len;
        }
      } else {
        P.Slices.push_back({0, S.Address, P.InputSize});
      }
      Result.PositionSections.push_back(std::move(P));
    }
  }
  // The records must be strictly ascending by (object_id, input_shndx)
  // (design §3.6).  Files/Sections iterate in that order already; the sort
  // makes the invariant structural instead of incidental.
  llvm::sort(Result.PositionSections, [](const LinkerResult::PositionSection &A,
                                         const LinkerResult::PositionSection &B) {
    return std::tie(A.ObjectId, A.InputShndx) <
           std::tie(B.ObjectId, B.InputShndx);
  });
  return true;
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
  // W4 (design §4.2): the cross-object identity rules.  Per-object validity
  // is enforced in loadFile() (so --print-input still exercises it); this
  // stage is the property of the *set*: v1 and v2 objects never mix, and
  // every v2 object's ABI/protocol fields must agree.  It runs before any
  // symbol resolution or placement work, so a link whose identities disagree
  // fails without ever touching layout, the image or an output file.
  if (!validateIdentitySet())
    return false;
  // G11 §3.3 (rev 4 fronting ruling): mergePlacement runs between the
  // identity set and symbol resolution -- its inputs are complete (NOTE
  // records, FIXED sections and their symbols, the manifest) and its
  // outputs (PlacementNames, the merged contract, the pinned storage
  // classes) must exist before resolveSymbols' second loop reads them.
  if (!mergePlacement(Result))
    return false;
  if (!resolveSymbols())
    return false;
  // P-4 (freeze 2026-09-14): the signature symbol association needs the
  // loaded symbol table, and the cross-object consistency pass needs the
  // per-object verdicts.  Both run after resolveSymbols (which is where the
  // symbol table becomes final) and before any layout work, matching the
  // "布局前" timing the freeze names.
  // The link-wide code-reference set: the freeze's NOTYPE function-entry
  // evidence may come from another object (the CRT ecall targets a demo's
  // `_main`), so gather it across every input before the per-file checks.
  for (const auto &F : Files)
    for (const StringRef N : functionReferencedNames(*F))
      LinkFunctionReferenced.insert(N);
  for (auto &F : Files)
    if (!validateFileSignatures(*F))
      return false;
  if (!validateSignatureSet())
    return false;
  if (!buildBitIdentities())
    return false;
  // BT14: bit-profile set rules run after the symbol table is final (the
  // carrier check needs the walker definition) and before any layout work.
  if (!validateBitProfileSet())
    return false;
  if (IrqMode && (!validateISRIdentitiesAndRegistrations() ||
                  !synthesizeIRQVectors()))
    return false;
  // G11 §3.3: the post-resolve bind-only collision check -- a PlacementNames
  // key that meets a defined global WITHOUT placement is a contradiction.
  if (!validatePlacementResolution())
    return false;
  if (!layout())
    return false;
  if (IrqMode && !validateIRQReservedRangesAndCRT())
    return false;
  // The ROM gate runs after layout and before any output is produced, so a
  // rejected image never reaches a firmware file or a map.
  if (!checkFlashGate())
    return false;
  if (!errorUndefined() || !applyRelocations() || !validateXInit() ||
      !validateXDATAInit())
    return false;
  // E2: static parameter-slot reentrancy diagnosis (COMPILER-ASSESSMENT
  // 2026-09-10 section 5).  Runs after layout and relocation application so
  // every call-edge target has its final address and the R3 checks have
  // already rejected every edge that targets a registered ISR or the default
  // entry.  Warning-only: it never fails the link and never touches the
  // image, the map or the symbol outputs.
  if (IrqMode && Config.IsrReentrancyDiag)
    diagnoseIsrReentrancy(Result);
  Result.Entry = llvm::any_of(AllSections,
                              [](const InputSection *S) { return S->Region == "HOME"; })
                     ? areaStart("HOME", 0) : 0;
  // E5: final symbol snapshot with post-layout addresses; the flavor shell
  // decides whether to serialize it into the output ELF.
  // G11 §3.3: the bind-only Synth append happens inside collectSymbols;
  // its unique name-collision checkpoint runs immediately before, when
  // layoutData has filled the live Synth map (the two-channel check needs
  // both the static reserved forms and the live keys).
  if (!validatePlacementSymbolNames())
    return false;
  collectSymbols(Result.Symbols);
  // G11-D2 (design §6.2): the positioning snapshot is taken in the result
  // snapshot stage, before Result.Image is moved out: every address is final
  // (G8 migration and G13b synthesis already happened) and the collection
  // itself cannot disturb the image, the map or the symbols.
  if (Config.CollectPositions && !collectPositions(Result))
    return false;
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
