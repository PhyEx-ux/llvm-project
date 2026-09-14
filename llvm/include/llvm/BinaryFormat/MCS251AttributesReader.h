//===- llvm/BinaryFormat/MCS251AttributesReader.h - v2 identity reader -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Strict reader for the MCS-251 v2 relocatable-object identity carrier
// (`.mcs251.attributes`), implementing the rejection rules of DESIGN.md N.6.
//
// A4 status (PM ruling 2026-09-13): the former open value domains are
// registered, so a successful decode now attests the frozen
// envelope/record/type/tag rules, the ruled scalar values AND the
// A4-registered values.  Which (as0_pointer_bits, default_placement)
// profiles may be EMITTED is a narrower, emitter-side policy
// (isRegisteredA4Profile / renderRegisteredIdentity).
//
// The reader is deliberately total and total-failing: every malformed input
// produces a diagnostic and no partially-populated result.  It never guesses
// a value, never falls back to the v1 note, and never silently ignores a
// Critical tag it does not understand.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MCS251ATTRIBUTESREADER_H
#define LLVM_BINARYFORMAT_MCS251ATTRIBUTESREADER_H

#include "llvm/BinaryFormat/MCS251Attributes.h"
#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
namespace MCS251Attributes {

/// One decoded record.  \p Value holds the raw payload; \p Decoded holds the
/// schema-validated scalar for U32-valued tags (meaningless otherwise).
struct Record {
  uint32_t Tag = 0;
  bool Critical = false;
  uint8_t ValueType = 0;
  uint32_t Length = 0;
  std::vector<uint8_t> Value;
  /// Valid (set by the decoder) only when SchemaValidated is true and
  /// ValueType == VT_U32 and Length == 4.  Unknown optional records keep
  /// their payload exclusively in Value.
  uint32_t Scalar = 0;
  /// True only for records the decoder validated against the registry
  /// schema: required U32 tags (Critical U32 of length 4), the optional
  /// reserved zeros and the memory_model_profile MIX atom stream.  Unknown
  /// optional records are carried as raw bytes with this flag false, so a
  /// consumer must not decode their payload as a Scalar or as MIX atoms --
  /// this revision has no schema for it (A4-FINAL-R1).
  bool SchemaValidated = false;
};

/// A fully decoded, schema-validated v2 identity.
struct Decoded {
  std::vector<Record> Records;
  uint32_t VendorSize = 0;
  uint32_t ScopeSize = 0;
  uint32_t PayloadSize = 0;

  /// P-4: the decoded contents of Tag 28, present exactly when the carrier
  /// holds that tag (which a complete v2 identity always does).  \p
  /// HasSignatures distinguishes "absent" from a legitimately empty array
  /// (`01 00 00 00`, which decodes to zero records).
  bool HasSignatures = false;
  MCS251Signatures::Table Signatures;

  const Record *find(uint32_t Tag) const {
    for (const Record &R : Records)
      if (R.Tag == Tag)
        return &R;
    return nullptr;
  }
};

/// Decode \p Bytes as a whole `.mcs251.attributes` section.
///
/// Enforces the envelope, record, field and unknown-item rules of N.6, and
/// verifies that every tag in \c RequiredTags is present exactly once with the
/// correct type and Critical bit.  Optional reserved tags must be zero.
///
/// \param Bytes   the section contents exactly as stored (no leading/trailing
///                padding is tolerated).
/// \param IsBigEndian target byte order for the envelope u32 fields.
llvm::Error decode(StringRef Bytes, bool IsBigEndian, Decoded &Out);

} // end namespace MCS251Attributes
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251ATTRIBUTESREADER_H
