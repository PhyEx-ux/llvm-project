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
// X3-R1 status: this is a STRUCTURE codec with no production caller; a
// successful decode attests the frozen envelope/record/type/tag rules and
// the ruled scalar values only, never that the field-value combination is
// an approved v2 object identity (the N.5/N.9 value domains are open).
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
  uint32_t Scalar = 0; ///< valid when ValueType == VT_U32 and Length == 4
};

/// A fully decoded, schema-validated v2 identity.
struct Decoded {
  std::vector<Record> Records;
  uint32_t VendorSize = 0;
  uint32_t ScopeSize = 0;
  uint32_t PayloadSize = 0;

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
