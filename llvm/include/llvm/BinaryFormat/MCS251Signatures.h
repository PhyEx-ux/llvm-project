//===- llvm/BinaryFormat/MCS251Signatures.h - P-4 signature value ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The internal value format of Tag 28 (MCS251_TAG_FUNCTION_SIGNATURES), frozen
// in validation/mcs251-models/proposals/P4-SIGNATURE-PROTOCOL-FREEZE.md.
//
// Endianness: ONLY the multi-byte fields *inside this tag* are little-endian.
// The attributes envelope and every existing U32 record stay big-endian.  All
// strings are NUL-terminated and collected in a trailing blob; the record
// length is implicit (9 + ceil(param_count/8)) so a length/content mismatch
// cannot be expressed.
//
//   Value  ::= version:u8(=1) flags:u8(=0) count:u16le  record*  blob
//   record ::= name_off:u32le role:u8 param_count:u8 bitmap:ceil(n/8)B
//              ret:u8 call_abi_major:u8 call_abi_minor:u8     (n=param_count)
//   blob   ::= count NUL-terminated non-empty names, concatenated in order
//
// The empty set is exactly `01 00 00 00`; a zero-length value is NOT a legal
// spelling of it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MCS251SIGNATURES_H
#define LLVM_BINARYFORMAT_MCS251SIGNATURES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
namespace MCS251Signatures {

/// The only value-format version registered for this revision.
inline constexpr uint8_t ValueVersion = 1;

/// role bits (P4 freeze "内部值格式").  Bits 4..7 are reserved and must be 0.
enum RoleBit : uint8_t {
  Role_HasDefinition       = 0x01, ///< this TU defines the function
  Role_DeclaredNotDefined  = 0x02, ///< this TU only declares/references it
  Role_NoPrototype         = 0x04, ///< K&R no-prototype declaration
  Role_Variadic            = 0x08, ///< variadic (param_count excludes "...")
};
/// `role & 3` must be 1 (defined) or 2 (declared); 0 and 3 are rejected.
inline constexpr uint8_t Role_DefinitionMask = 0x03;
/// Bits 4..7 are reserved.
inline constexpr uint8_t Role_ReservedMask = 0xF0;

/// One decoded signature record.  \p Name is only filled by decode()/encode()
/// from the blob; \p NameOffset is the blob-relative offset of the name's
/// first byte.
struct Record {
  uint32_t NameOffset = 0;
  uint8_t Role = 0;
  uint8_t ParamCount = 0;
  std::string Name;
  std::vector<uint8_t> Bitmap;
  uint8_t Ret = 0;
  uint8_t CallABIMajor = 0;
  uint8_t CallABIMinor = 0;
};

struct Table {
  std::vector<Record> Records;

  /// \return the record whose name equals \p Name, or nullptr.  Names are
  /// unique inside one table (the strict decoder rejects duplicates), so a
  /// linear scan is exact.  Used by the lld cross-object comparison.
  const Record *find(StringRef Name) const {
    for (const Record &R : Records)
      if (R.Name == Name)
        return &R;
    return nullptr;
  }

  bool empty() const { return Records.empty(); }
};

/// Strictly decode one Tag 28 value.  Enforces every format-level rule of the
/// freeze (version/flags, record length, role legal combinations, bitmap tail
/// bits, ret domain, name_off / empty-name / duplicate-name / trailing-blob
/// rejection).  The object-level "call_abi must equal the object identity"
/// rule is NOT applied here: use checkABIGeneration() with the object's own
/// generation, because this function cannot see the sibling tags.
llvm::Error decode(StringRef Value, Table &Out);

/// Check every record's (call_abi_major, call_abi_minor) against the object's
/// own CallABI generation (freeze: "call_abi_major/minor 必须与本对象身份的
/// CallABI 代一致").
llvm::Error checkABIGeneration(const Table &T, uint8_t Major, uint8_t Minor);

/// Serialize \p T into the Tag 28 value.  The blob and every name_off are
/// derived from the records' names; the result is self-checked against
/// decode() so the writer can never disagree with the reader.
std::string encode(const Table &T);

/// \return ceil(ParamCount / 8): the exact bitmap byte count for a record.
inline size_t bitmapBytes(uint8_t ParamCount) {
  return (size_t(ParamCount) + 7) / 8;
}

//===----------------------------------------------------------------------===//
// IR-level carrier: the `!mcs251.signatures` named metadata (freeze "签名的
// IR 层保留").  clang publishes one MDNode per external-linkage function the
// TU declares or defines; llc reads the nodes back and emits Tag 28.  The
// metadata is deliberately source-typed, never re-derived from the i8
// boundary: the AST knows `bit` from `unsigned char`, and nothing downstream
// can tell them apart.
//
// Node layout (operand index -> meaning):
//   0              MDString  final ELF symbol name (target mangling already
//                             applied; a `\01` asm-label escape already
//                             stripped).  llc does NOT re-prefix it.
//   1              i32       role byte (bit0 definition, bit1 declaration,
//                             bit2 no-prototype, bit3 variadic)
//   2              i32       return bit-ness (0/1)
//   3 .. 3+n-1     i32       per-source-parameter bit-ness, frozen order
// `n` is the number of fixed source parameters; the record's blob name is
// exactly operand 0, so a name that does not match the final ELF symbol is a
// producer bug the strict decoder cannot see and must not be trusted by llc.
//===----------------------------------------------------------------------===//

inline constexpr StringRef MetadataName = "mcs251.signatures";
inline constexpr unsigned MetadataOperandName = 0;
inline constexpr unsigned MetadataOperandRole = 1;
inline constexpr unsigned MetadataOperandRet = 2;
inline constexpr unsigned MetadataOperandFirstParam = 3;

//===----------------------------------------------------------------------===//
// Role helpers (freeze "role 位"): the two legal low-bit combinations are
// "only definition" (1) and "only declaration/reference" (2); 0 and 3 are
// rejected by the decoder, so every successfully decoded record answers
// exactly one of these two.
//===----------------------------------------------------------------------===//

inline bool isDefinitionRole(uint8_t Role) {
  return (Role & Role_DefinitionMask) == Role_HasDefinition;
}
inline bool isDeclarationRole(uint8_t Role) {
  return (Role & Role_DefinitionMask) == Role_DeclaredNotDefined;
}
inline bool hasNoPrototype(uint8_t Role) {
  return (Role & Role_NoPrototype) != 0;
}
inline bool isVariadic(uint8_t Role) {
  return (Role & Role_Variadic) != 0;
}

//===----------------------------------------------------------------------===//
// Source-parameter bitmap accessors.  Bit i (0-based source position) lives in
// byte i/8 at bit i%8 -- the frozen wire order, little bit within each byte.
// The high bits beyond ParamCount are always zero on the wire; callers that
// build a bitmap themselves still go through encode(), which runs the strict
// decoder, so a stray set tail bit cannot be serialized.
//===----------------------------------------------------------------------===//

inline bool bitmapGet(const std::vector<uint8_t> &Bitmap, unsigned I) {
  return I / 8 < Bitmap.size() &&
         ((Bitmap[I / 8] >> (I % 8)) & uint8_t(1)) != 0;
}

inline void bitmapSet(std::vector<uint8_t> &Bitmap, unsigned I, bool Value) {
  size_t Need = size_t(I) / 8 + 1;
  if (Bitmap.size() < Need)
    Bitmap.resize(Need, 0);
  if (Value)
    Bitmap[I / 8] = uint8_t(Bitmap[I / 8] | uint8_t(1u << (I % 8)));
  else
    Bitmap[I / 8] = uint8_t(Bitmap[I / 8] & uint8_t(~(1u << (I % 8))));
}

/// Compare two same-name records under the frozen "比较语义" and report the
/// first disagreement as a diagnostic.  Returns success when the two records
/// are compatible:
///   - bit2 (no-prototype) differs on the two sides          -> conflict;
///   - both sides bit2=1                                     -> compare
///     (ret, call_abi) only;
///   - both sides bit2=0                                     -> compare
///     (param_count, bitmap, ret, call_abi, bit3).
/// \p Context names the record for the diagnostic (for example the two input
/// paths); it may be empty.
llvm::Error compareRecords(const Record &A, const Record &B,
                           StringRef Context);

/// Render a role byte for diagnostics, e.g. "definition+variadic".  Never
/// asserts: an illegal combination is described literally so a diagnostic
/// path can carry it.
std::string describeRole(uint8_t Role);

/// Build a definition/declaration record for an externals function.  The
/// name is the FINAL ELF symbol name (already target-mangled); \p Bitmap has
/// one bit per source parameter in frozen order; \p Ret is 1 when the source
/// return type is `bit`.  Bit2/bit3 are derived from \p NoPrototype and
/// \p Variadic.  The returned record is NOT validated; encode() re-runs the
/// strict decoder.
Record makeRecord(StringRef Name, bool IsDefinition, unsigned ParamCount,
                  const std::vector<uint8_t> &Bitmap, bool Ret,
                  bool NoPrototype, bool Variadic, uint8_t CallABIMajor,
                  uint8_t CallABIMinor);

} // end namespace MCS251Signatures
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251SIGNATURES_H
