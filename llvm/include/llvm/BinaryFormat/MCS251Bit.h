//===- llvm/BinaryFormat/MCS251Bit.h - MCS251 bit-object protocol -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared, frozen constants for the MCS-251 persistent-`bit` object protocol
// (BIT-TASK-BREAKDOWN.md BT12/BT13/BT14 and the lld-side frozen input contract
// `lld/MCS251/BIT-OBJECT-CONTRACT.md`, v1).  This header is the single
// registration point for:
//   - the `.mcs251.bit` object-metadata section layout (BT12),
//   - the `.mcs251.bitprofile` bit-aware CRT profile section (BT14, reserved),
//   - the two bit-object relocation numbers (registered in ELFRelocs/MCS251.def
//     by BT00) and the frozen bit-address field position rule.
//
// Names, numbers, byte layout, endianness and semantics are frozen by the lld
// contract; changes may only be made via the design owner through PM
// arbitration.  The backend stream emits exactly this shape.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MCS251BIT_H
#define LLVM_BINARYFORMAT_MCS251BIT_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
namespace MCS251Bit {

//===----------------------------------------------------------------------===//
// Sections (BIT-OBJECT-CONTRACT.md §3, §6)
//===----------------------------------------------------------------------===//

/// The bit-object record section.  A non-ALLOC SHT_PROGBITS section with
/// sh_flags = 0 and sh_addralign = 4, holding whole 8-byte records
/// (sh_entsize = 0).  At most one per object.
inline constexpr StringRef MetaSectionName = ".mcs251.bit";
inline constexpr uint64_t MetaSectionAlignment = 4;

/// The bit-aware CRT profile section.  Frozen by BT14; this revision reserves
/// the exact name and fails closed (the lld stream rejects it with a dedicated
/// diagnostic until the profile is defined).
inline constexpr StringRef ProfileSectionName = ".mcs251.bitprofile";

//===----------------------------------------------------------------------===//
// Record layout: fixed 8 bytes, big-endian (BIT-OBJECT-CONTRACT.md §3)
//===----------------------------------------------------------------------===//

/// protocol_version of the first frozen revision.
inline constexpr uint8_t ProtocolVersion = 1;
/// Fixed record size in bytes; the section must be a whole number of records.
inline constexpr uint32_t RecordSize = 8;
/// The frozen capability word.
inline constexpr uint8_t Capabilities = 1;

enum RecordKind : uint8_t {
  RK_DEFINITION = 1, ///< kind 1: a defined persistent bit object.
  RK_REFERENCE = 2,  ///< kind 2: a fixed SHN_ABS bit reference.
};

/// Record offsets (little offsets into each 8-byte record; the multi-byte
/// symbol_reference is big-endian but is emitted as four zero bytes).
namespace RecordOffset {
inline constexpr unsigned Version = 0;          // u8, must be 1
inline constexpr unsigned Kind = 1;             // u8, 1 = definition 2 = reference
inline constexpr unsigned InitValue = 2;        // u8, 0 or 1
inline constexpr unsigned Capabilities = 3;     // u8, must be 1
inline constexpr unsigned SymbolReference = 4;  // 4 bytes zero; RELA type 10
} // namespace RecordOffset

//===----------------------------------------------------------------------===//
// Relocation identities (BIT-OBJECT-CONTRACT.md §4; also ELFRelocs/MCS251.def)
//===----------------------------------------------------------------------===//
//
// The numeric types are registered publicly by BT00 as
// R_MCS251_BIT_REF = 10 and R_MCS251_BITADDR8 = 11.  They are deliberately
// re-stated here only as documentation; code uses the ELF:: symbols.

/// R_MCS251_BIT_REF: zero-width identity association, valid exclusively inside
/// `.rela.mcs251.bit`, exactly one per record at r_offset = record + 4.
inline constexpr unsigned BitRefReloc = 10;
/// R_MCS251_BITADDR8: writes the resolved bit address into the one-byte
/// bit-address field of a bit instruction; the producer zero-fills the field.
inline constexpr unsigned BitAddr8Reloc = 11;

/// Frozen classic opcode bytes whose second byte is a bit-address field
/// (BIT-OBJECT-CONTRACT.md §4.2).  Mirrored by the lld decoder; used here to
/// document the field rule.
enum BitFieldOpcode : uint8_t {
  BFO_SETB = 0xD2,
  BFO_CLR = 0xC2,
  BFO_CPL = 0xB2,
  BFO_MOV_BIT_C = 0x92,
  BFO_MOV_C_BIT = 0xA2,
  BFO_JB = 0x20,
  BFO_JNB = 0x30,
  BFO_JBC = 0x10,
};

//===----------------------------------------------------------------------===//
// Address model (BIT-OBJECT-CONTRACT.md §1)
//===----------------------------------------------------------------------===//

/// Bit-addressable internal RAM is bit addresses 0x00..0x7F mapping onto bytes
/// 0x20..0x2F; 0x80..0xFF are SFR bit references with no RAM backing.
inline constexpr uint32_t WindowBaseByte = 0x20;
inline constexpr uint32_t WindowByteCount = 16;
inline constexpr uint32_t WindowBitCount = 128;

/// \return the backing byte of an internal-RAM bit address, or -1 for an SFR
/// bit reference (0x80..0xFF).
inline constexpr int backingByte(uint32_t BitAddr) {
  return BitAddr < WindowBitCount ? int(WindowBaseByte + (BitAddr >> 3)) : -1;
}

//===----------------------------------------------------------------------===//
// Backend IR handle convention (P09 placeholder; documented contract)
//===----------------------------------------------------------------------===//
//
// A persistent/static bit object reaches the backend as an ordinary
// default-address-space i8 GlobalVariable placeholder carrying the structural
// global attribute below.  The placeholder is object identity only: it is
// never allocated as a byte (no DSEG/XINIT/CSEG data), it has no byte address,
// and ordinary load/store/GEP/cast/ptrtoint/initializer escapes are rejected
// by the contract verifier.  The AsmPrinter diverts it into a `.mcs251.bit`
// kind-1 record and the MC layer references it only through a symbolic
// R_MCS251_BITADDR8 field.
inline constexpr StringRef BitObjectAttrName = "mcs251-bit-object";

} // end namespace MCS251Bit
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251BIT_H
