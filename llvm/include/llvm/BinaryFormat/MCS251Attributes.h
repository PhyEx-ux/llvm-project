//===- llvm/BinaryFormat/MCS251Attributes.h - MCS251 v2 object identity ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared, frozen constants for the MCS-251 v2 relocatable-object identity
// carrier, as ruled in validation/mcs251-models/DESIGN.md N.3-N.9 (the
// "three-track mixed + self-describing Tag" decision).
//
// This header is the single registration point for:
//   - the `.mcs251.attributes` section name and type,
//   - the v2 e_flags value,
//   - the ARM-attributes envelope byte layout (N.3),
//   - the self-describing record encoding (N.4),
//   - the value type codes (N.4),
//   - the concrete Tag numbers (N.5).
//
// Endianness rule (N.3): the envelope's two u32 length fields follow the ELF
// target byte order.  MCS-251 is MSB, so VendorSize and ScopeSize are stored
// big-endian.  Tag and Length inside a record are naked ULEB128 and are never
// byte-swapped.
//
// This file defines the encoding and the value registration.  The A4 open
// values were ruled by the PM on 2026-09-13 (design
// validation/mcs251-models/proposals/A4-V2-OBJECT-IDENTITY-DESIGN.md §2 and
// the ruling record at its end), so the registered constants below are the
// only values production may emit or accept; a candidate value must not
// enter a relocatable object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MCS251ATTRIBUTES_H
#define LLVM_BINARYFORMAT_MCS251ATTRIBUTES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>

namespace llvm {
namespace MCS251Attributes {

//===----------------------------------------------------------------------===//
// Section and object header identity (N.1, N.3)
//===----------------------------------------------------------------------===//

/// The v2 object identity carrier.  Non-ALLOC metadata: sh_flags = 0,
/// sh_addralign = 1, sh_entsize = 0, sh_link = 0, sh_info = 0.
inline constexpr StringRef SectionName = ".mcs251.attributes";

/// SHT_LOPROC range, the same number ARM/AArch64/RISC-V/MSP430/Hexagon use.
inline constexpr uint32_t SectionType = 0x70000003u;

/// v2 relocatable-object e_flags.  Low byte is the object protocol version
/// (2); bit 8 is the Source/native backend-produced marker.
inline constexpr uint32_t EFlagsV2 = 0x00000102u;

/// The vendor *name* used when handing the vendor string to a section
/// builder that appends its own terminator: six bytes, no NUL.
inline constexpr StringRef Vendor = "MCS251";

/// The vendor bytes exactly as they appear in the envelope: seven bytes
/// including the terminator NUL ("MCS251\0").  Comparisons against section
/// contents must use this length; a bare string literal would be truncated by
/// strlen at the NUL.
inline constexpr char VendorBytes[] = {'M','C','S','2','5','1','\0'};
inline constexpr uint32_t VendorBytesSize = sizeof(VendorBytes);

//===----------------------------------------------------------------------===//
// Envelope (N.3)
//===----------------------------------------------------------------------===//
//
//   offset 0  format      u8 = 0x41 ('A')
//   offset 1  VendorSize  u32 (target byte order) = 16 + P
//   offset 5  vendor      4D 43 53 32 35 31 00  ("MCS251\0")
//   offset 12 ScopeTag    u8 = 1 (File scope)
//   offset 13 ScopeSize   u32 (target byte order) = 5 + P
//   offset 17 attributes  P bytes of self-describing records
//
// Section size is 17 + P and must satisfy VendorSize == sh_size - 1 and
// ScopeSize == VendorSize - 11.

inline constexpr uint8_t FormatVersion = 0x41u;   ///< ASCII 'A'
inline constexpr uint8_t ScopeTagFile = 0x01u;    ///< File scope
inline constexpr uint32_t VendorSizeBase = 16u;   ///< VendorSize - P
inline constexpr uint32_t ScopeSizeBase = 5u;     ///< ScopeSize  - P
inline constexpr uint32_t EnvelopeSize = 17u;     ///< bytes before attributes

//===----------------------------------------------------------------------===//
// Record encoding (N.4)
//===----------------------------------------------------------------------===//
//
//   Record := Tag:ULEB128  TypeFlags:u8  Length:ULEB128  Value:byte[Length]
//
//   TypeFlags.bit7    = Critical
//   TypeFlags.bits6:0 = ValueType
//
// Tag and Length are shortest-form ULEB128 (max 5 bytes, <= 0xffffffff) and
// are never byte-swapped.  Length counts only Value.

enum ValueType : uint8_t {
  VT_Reserved = 0x00, ///< never emitted; rejected on read
  VT_U32      = 0x01, ///< Length = 4, target byte order (BE32 on MCS-251)
  VT_UTF8     = 0x02, ///< exactly Length UTF-8 bytes, no trailing NUL
  VT_MIX      = 0x03, ///< sequence of Atoms (below)
  VT_BYTES    = 0x04, ///< raw Length bytes, no endian/string rules
};

inline constexpr uint8_t TypeMask     = 0x7fu;
inline constexpr uint8_t CriticalMask = 0x80u;

/// MIX atom: AtomType:u8  Length:ULEB128  Value:byte[Length].  Atoms never
/// carry a Critical bit and never nest another MIX.  The last atom must end
/// exactly at the end of the enclosing Value.  Atom types are therefore
/// compared as raw bytes: there is deliberately no mask to silently drop a
/// set Critical bit.

//===----------------------------------------------------------------------===//
// Attribute Tag registry (N.5).  Numbers are decimal per the ruling table.
//===----------------------------------------------------------------------===//

enum Tag : uint32_t {
  // Values 0-3 are reserved for the scope namespace; the first allocated
  // attribute tag is 4.
  Tag_ObjectProtocolVersion   = 4,  ///< U32, required, ruled: 2
  Tag_CallABIMajor            = 5,  ///< U32, required, registered A4: 2
  Tag_CallABIMinor            = 6,  ///< U32, required, registered A4: 1
  Tag_RegisterParameterVariant= 7,  ///< U32, required, registered A4: 3
  Tag_GeneralRegisterSet      = 8,  ///< U32, required, ruled: 0x0000f3ff
  Tag_IntBits                 = 9,  ///< U32, required, ruled: 32
  Tag_LongBits                = 10, ///< U32, required, ruled: 32
  Tag_AS0PointerBits          = 11, ///< U32, required, ruled: 16 or 32
  Tag_ASLayoutVersion         = 12, ///< U32, required, registered A4: 2
  Tag_DefaultPlacement        = 13, ///< U32, required, ruled: 1/3/8
  Tag_InitProtocolVersion     = 14, ///< U32, required, registered A4: 2
  Tag_PlacementProtocolVersion= 15, ///< U32, required, registered A4: 2
  Tag_StackContractVersion    = 16, ///< U32, required, registered A4: 2
  Tag_FunctionContractVersion = 17, ///< U32, required, registered A4: 2
  Tag_RequiredCapabilitiesLo  = 18, ///< U32, required, registered A4: 0
  Tag_RequiredCapabilitiesHi  = 19, ///< U32, required, registered A4: 0
  Tag_ABIOptions              = 20, ///< U32, required, registered A4: 0
  Tag_Reserved0               = 21, ///< U32, optional, must be 0
  Tag_Reserved1               = 22, ///< U32, optional, must be 0
  Tag_Reserved2               = 23, ///< U32, optional, must be 0
  Tag_MemoryModelProfile      = 24, ///< MIX(U32,U32), required
  Tag_CodeModelProfile        = 25, ///< U32, required, registered A4: 1
  Tag_CodePointerBits         = 26, ///< U32, required, ruled: 32
  Tag_ObjectProtocolMinor     = 27, ///< U32, required, registered A4: 0
  // P-4 (freeze 2026-09-14): the function-signature array.  VT_BYTES, one
  // per v2 object, Critical, and a member of RequiredTags.  Its internal
  // value format is owned by MCS251Signatures.h.
  Tag_FunctionSignatures      = 28, ///< BYTES, required, Critical

  Tag_FirstAllocated = Tag_ObjectProtocolVersion,
  Tag_LastRegistered = Tag_FunctionSignatures,
};

/// Tags that are required for a first-slice v2 identity.  Every entry must
/// appear exactly once with Critical = 1.  (N.5 "必需".)
inline constexpr Tag RequiredTags[] = {
    Tag_ObjectProtocolVersion,    Tag_CallABIMajor,
    Tag_CallABIMinor,             Tag_RegisterParameterVariant,
    Tag_GeneralRegisterSet,       Tag_IntBits,
    Tag_LongBits,                 Tag_AS0PointerBits,
    Tag_ASLayoutVersion,          Tag_DefaultPlacement,
    Tag_InitProtocolVersion,      Tag_PlacementProtocolVersion,
    Tag_StackContractVersion,     Tag_FunctionContractVersion,
    Tag_RequiredCapabilitiesLo,   Tag_RequiredCapabilitiesHi,
    Tag_ABIOptions,               Tag_MemoryModelProfile,
    Tag_CodeModelProfile,         Tag_CodePointerBits,
    Tag_ObjectProtocolMinor,      Tag_FunctionSignatures,
};

/// Tags 21-23 are optional reserved words; when present they must be zero.
inline constexpr Tag OptionalReservedTags[] = {
    Tag_Reserved0,
    Tag_Reserved1,
    Tag_Reserved2,
};

//===----------------------------------------------------------------------===//
// Ruled field values (N.5).  Open fields are intentionally absent: an
// implementation must not invent them.
//===----------------------------------------------------------------------===//

inline constexpr uint32_t ObjectProtocolVersion = 2;
inline constexpr uint32_t GeneralRegisterSet     = 0x0000f3ffu;
inline constexpr uint32_t IntBits                = 32;
inline constexpr uint32_t LongBits               = 32;
inline constexpr uint32_t CodePointerBits        = 32;
inline constexpr uint32_t AS0PointerBits16       = 16;
inline constexpr uint32_t AS0PointerBits32       = 32;

//===----------------------------------------------------------------------===//
// A4-registered field values (PM ruling 2026-09-13; A4 design §2).  These
// were the open fields of N.5/N.9; the ruling registered exactly this set,
// and the decoder enforces them as equality checks, so a candidate value can
// no longer be serialized into a production object.
//===----------------------------------------------------------------------===//

inline constexpr uint32_t CallABIMajor            = 2;
/// CallABIMinor=1 is the SOLE carrier of the "later pointer parameters take
/// static slots" capability (design §2.1): no RequiredCapabilitiesLo bit is
/// defined for it and the two encodings must not be mixed.
inline constexpr uint32_t CallABIMinor            = 1;
inline constexpr uint32_t RegisterParameterVariant = 3;
inline constexpr uint32_t ASLayoutVersion         = 2;
inline constexpr uint32_t InitProtocolVersion     = 2;
inline constexpr uint32_t PlacementProtocolVersion = 2;
inline constexpr uint32_t StackContractVersion    = 2;
inline constexpr uint32_t FunctionContractVersion = 2;
inline constexpr uint32_t RequiredCapabilitiesLo  = 0;
inline constexpr uint32_t RequiredCapabilitiesHi  = 0;
inline constexpr uint32_t ABIOptions              = 0;
inline constexpr uint32_t CodeModelProfile        = 1;
inline constexpr uint32_t ObjectProtocolMinor     = 0;

/// default_placement: InternalMovable / ExternalData / InternalExtended.
enum DefaultPlacement : uint32_t {
  Placement_InternalMovable  = 1,
  Placement_ExternalData     = 3,
  Placement_InternalExtended = 8,
};

/// The five memory model profiles, as the (as0_pointer_bits, default_placement)
/// pair.  The display names are Tiny / XTiny / Small / XSmall / Large.
struct MemoryModelProfile {
  uint32_t AS0PointerBits;
  DefaultPlacement Placement;
};
inline constexpr MemoryModelProfile MemoryModelProfile_Tiny   = {16, Placement_InternalMovable};
inline constexpr MemoryModelProfile MemoryModelProfile_XTiny  = {16, Placement_InternalExtended};
inline constexpr MemoryModelProfile MemoryModelProfile_Small  = {32, Placement_InternalMovable};
inline constexpr MemoryModelProfile MemoryModelProfile_XSmall = {32, Placement_InternalExtended};
inline constexpr MemoryModelProfile MemoryModelProfile_Large  = {32, Placement_ExternalData};

/// \return true when (AS0Bits, Placement) is one of the two memory model
/// profiles registered for A4 production emission: XSmall (32,
/// InternalExtended) and Small (32, InternalMovable).  The other three
/// frozen profiles remain structurally decodable but are not approved for
/// emission (16-bit objects are rejected by the object gate; Large is
/// outside the A4 registration).
inline bool isRegisteredA4Profile(uint32_t AS0Bits, uint32_t Placement) {
  return AS0Bits == AS0PointerBits32 &&
         (Placement == Placement_InternalExtended ||
          Placement == Placement_InternalMovable);
}

/// Tag 256 in the N.7 fixture is an unallocated tag used to exercise the
/// unknown-tag rules; this constant exists only so tests do not hardcode it.
inline constexpr uint32_t Tag_FixtureUnknownOptional = 256;

//===----------------------------------------------------------------------===//
// Tag naming (diagnostics and llvm-readobj; N.7)
//===----------------------------------------------------------------------===//

/// \return the stable field name for \p T, or an empty StringRef when the tag
/// is not registered in this revision.
///
/// Callers that print the name must use \ref formatTag, not this function
/// directly: the returned StringRef may be a temporary-free literal today, but
/// the diagnostic helpers deliberately funnel every name through one place so
/// that a caller cannot accidentally build a dangling `char *` (e.g.
/// `tagName(T).str().c_str()`) or pass a StringRef where printf `%s` expects a
/// NUL-terminated array.
inline StringRef tagName(uint32_t T) {
  switch (T) {
  case Tag_ObjectProtocolVersion:    return "object_protocol_version";
  case Tag_CallABIMajor:             return "call_abi_major";
  case Tag_CallABIMinor:             return "call_abi_minor";
  case Tag_RegisterParameterVariant: return "register_parameter_variant";
  case Tag_GeneralRegisterSet:       return "general_register_set";
  case Tag_IntBits:                  return "int_bits";
  case Tag_LongBits:                 return "long_bits";
  case Tag_AS0PointerBits:           return "as0_pointer_bits";
  case Tag_ASLayoutVersion:          return "as_layout_version";
  case Tag_DefaultPlacement:         return "default_placement";
  case Tag_InitProtocolVersion:      return "init_protocol_version";
  case Tag_PlacementProtocolVersion: return "placement_protocol_version";
  case Tag_StackContractVersion:     return "stack_contract_version";
  case Tag_FunctionContractVersion:  return "function_contract_version";
  case Tag_RequiredCapabilitiesLo:   return "required_capabilities_lo";
  case Tag_RequiredCapabilitiesHi:   return "required_capabilities_hi";
  case Tag_ABIOptions:               return "abi_options";
  case Tag_Reserved0:                return "reserved0";
  case Tag_Reserved1:                return "reserved1";
  case Tag_Reserved2:                return "reserved2";
  case Tag_MemoryModelProfile:       return "memory_model_profile";
  case Tag_CodeModelProfile:         return "code_model_profile";
  case Tag_CodePointerBits:          return "code_pointer_bits";
  case Tag_ObjectProtocolMinor:      return "object_protocol_minor";
  case Tag_FunctionSignatures:       return "function_signatures";
  default:                           return StringRef();
  }
}

/// \return the display name of a value type, or "unknown" for unallocated
/// codes.  Reserved type 0 is never emitted and is rejected on read.
inline StringRef valueTypeName(uint8_t VT) {
  switch (VT) {
  case VT_U32:   return "U32";
  case VT_UTF8:  return "UTF8";
  case VT_MIX:   return "MIX";
  case VT_BYTES: return "BYTES";
  case VT_Reserved: return "reserved(0)";
  default:       return "unknown";
  }
}

/// Stream a tag for a diagnostic as `object_protocol_version(4)` for a
/// registered field, or `tag 256` for an unallocated one.
///
/// Diagnostics in this protocol are built with a raw_string_ostream and this
/// helper, never with printf-style format strings: llvm's `format()`/`%s`
/// rejects `std::string` outright, and a temporary used as a `%s` argument
/// (`tagName(T).str().c_str()`) would dangle.  Taking a stream keeps every tag
/// name renderable without either hazard.
inline void printTag(raw_ostream &OS, uint32_t T) {
  StringRef Name = tagName(T);
  if (Name.empty()) {
    OS << "tag " << T;
    return;
  }
  OS << Name << "(" << T << ")";
}

/// Stream a value type for a diagnostic as `U32(0x01)` / `unknown(0x05)`.
inline void printValueType(raw_ostream &OS, uint8_t VT) {
  OS << valueTypeName(VT) << "(" << format_hex(VT, 4) << ")";
}

/// Build an invalid_argument error from a streamed message.
///
/// This is the single sanctioned constructor for this protocol's diagnostics.
/// Usage:
/// \code
///   return makeError([&](raw_ostream &OS) {
///     OS << "duplicate ";
///     printTag(OS, Tag);
///   });
/// \endcode
template <typename Fn>
inline llvm::Error makeError(Fn &&Build) {
  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "MCS251 attributes: ";
  Build(OS);
  OS.flush();
  return createStringError(std::errc::invalid_argument, "%s", Msg.c_str());
}

} // end namespace MCS251Attributes
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_MCS251ATTRIBUTES_H
