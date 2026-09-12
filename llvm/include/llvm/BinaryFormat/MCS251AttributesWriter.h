//===- llvm/BinaryFormat/MCS251AttributesWriter.h - v2 identity writer -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Writer for the MCS-251 v2 identity carrier byte format.  Emits the exact
// layout frozen in DESIGN.md N.3/N.4/N.5 and is used by the unit tests and
// YAML fixtures.
//
// X3-R1 status: this is a STRUCTURE codec, not an approved v2 object
// identity emitter.  The N.5/N.9 field VALUE DOMAINS are still open (call
// ABI, register variant, init/placement/stack/function subprotocols, code
// model profile, capability words, ABI options), so nothing in production
// may render these bytes into a relocatable object: the writer currently
// has no production caller, and the tests that build "complete" identities
// exercise the codec's rules, not an approved value combination.
//
// Emission is canonical: tags strictly increasing by number (N.6; a
// violation is a hard render error, not an assert), reserved tags omitted
// unless explicitly requested, shortest-form ULEB128, and the envelope
// lengths computed from the payload rather than hardcoded.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MCS251ATTRIBUTESWRITER_H
#define LLVM_BINARYFORMAT_MCS251ATTRIBUTESWRITER_H

#include "llvm/BinaryFormat/MCS251Attributes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
namespace MCS251Attributes {

/// One output record.  For U32 the payload is derived from \p Scalar; for
/// UTF8/BYTES it comes from \p Bytes; MIX payloads are assembled by adding
/// atoms with addAtom().
struct OutRecord {
  uint32_t Tag = 0;
  bool Critical = false;
  uint8_t ValueType = VT_U32;
  uint32_t Scalar = 0;
  std::string Bytes;
  std::vector<OutRecord> Atoms; ///< used when ValueType == VT_MIX
  /// When true, Bytes is forwarded verbatim as the Value regardless of
  /// ValueType.  Used only to construct deliberate malformed inputs.
  bool IsRaw = false;
};

/// Accumulates records and renders the whole section.
class Writer {
public:
  /// Add a U32-valued record.
  void addU32(uint32_t Tag, uint32_t Value, bool Critical = true);

  /// Add a UTF8-valued record.
  void addUTF8(uint32_t Tag, StringRef Value, bool Critical = false);

  /// Add a BYTES-valued record.
  void addBytes(uint32_t Tag, StringRef Value, bool Critical = false);

  /// Add a MIX record built from \p Atoms (U32/UTF8/BYTES only).
  void addMix(uint32_t Tag, std::vector<OutRecord> Atoms,
              bool Critical = true);

  /// Add an opaque record with a caller-supplied raw payload.  Used by tests
  /// to build malformed inputs (non-shortest ULEB, bad type codes, ...).
  void addRaw(uint32_t Tag, uint8_t ValueType, bool Critical,
              std::string Payload);

  /// Render the section bytes for a big-endian target (\p IsBigEndian true) or
  /// a little-endian target.  Tags must be strictly increasing.
  std::string render(bool IsBigEndian) const;

  const std::vector<OutRecord> &records() const { return Records; }

private:
  std::vector<OutRecord> Records;
};

/// Encode a ULEB128 value in shortest form.
void encodeULEB128(uint64_t Value, std::string &Out);

/// Encode a ULEB128 value using a caller-chosen byte count (tests only; allows
/// deliberately non-shortest encodings so the reader can reject them).
void encodeULEB128Padded(uint64_t Value, unsigned Bytes, std::string &Out);

} // end namespace MCS251Attributes
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251ATTRIBUTESWRITER_H
