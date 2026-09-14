//===- MCS251Attributes.cpp - v2 object identity codec --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Encoder and strict decoder for the MCS-251 v2 identity carrier.  The byte
// layout is frozen in validation/mcs251-models/DESIGN.md N.3-N.6; see
// llvm/BinaryFormat/MCS251Attributes.h for the registration point.
//
// A4 status (PM ruling 2026-09-13): the former N.5/N.9 open value domains
// are registered (A4-V2-OBJECT-IDENTITY-DESIGN.md §2).  The decoder now
// enforces the registered values as equality checks, and
// renderRegisteredIdentity() assembles the minimal registered identity that
// the MCS251 v2 object path publishes.  No candidate value set can be
// serialized into a production object through this codec.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MCS251AttributesReader.h"
#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/BinaryFormat/MCS251AttributesWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstring>
#include <initializer_list>
#include <tuple>

using namespace llvm;
using namespace llvm::MCS251Attributes;

//===----------------------------------------------------------------------===//
// Writer
//===----------------------------------------------------------------------===//

void MCS251Attributes::encodeULEB128(uint64_t Value, std::string &Out) {
  do {
    uint8_t Byte = Value & 0x7fu;
    Value >>= 7;
    if (Value)
      Byte |= 0x80u;
    Out.push_back(char(Byte));
  } while (Value);
}

void MCS251Attributes::encodeULEB128Padded(uint64_t Value, unsigned Bytes,
                                           std::string &Out) {
  assert(Bytes >= 1 && Bytes <= 5 && "ULEB128 is at most 5 bytes");
  for (unsigned I = 0; I != Bytes; ++I) {
    uint8_t Byte = Value & 0x7fu;
    Value >>= 7;
    // The continuation bit is set on every byte but the last of this run,
    // regardless of whether Value has more significant bits: that is exactly
    // how a non-shortest encoding is produced.
    if (I + 1 != Bytes)
      Byte |= 0x80u;
    Out.push_back(char(Byte));
  }
}

void Writer::addU32(uint32_t Tag, uint32_t Value, bool Critical) {
  OutRecord R;
  R.Tag = Tag;
  R.Critical = Critical;
  R.ValueType = VT_U32;
  R.Scalar = Value;
  Records.push_back(std::move(R));
}

void Writer::addUTF8(uint32_t Tag, StringRef Value, bool Critical) {
  OutRecord R;
  R.Tag = Tag;
  R.Critical = Critical;
  R.ValueType = VT_UTF8;
  R.Bytes = Value.str();
  Records.push_back(std::move(R));
}

void Writer::addBytes(uint32_t Tag, StringRef Value, bool Critical) {
  OutRecord R;
  R.Tag = Tag;
  R.Critical = Critical;
  R.ValueType = VT_BYTES;
  R.Bytes = Value.str();
  Records.push_back(std::move(R));
}

void Writer::addMix(uint32_t Tag, std::vector<OutRecord> Atoms, bool Critical) {
  OutRecord R;
  R.Tag = Tag;
  R.Critical = Critical;
  R.ValueType = VT_MIX;
  R.Atoms = std::move(Atoms);
  Records.push_back(std::move(R));
}

void Writer::addRaw(uint32_t Tag, uint8_t ValueType, bool Critical,
                    std::string Payload) {
  OutRecord R;
  R.Tag = Tag;
  R.Critical = Critical;
  R.ValueType = ValueType;
  R.Bytes = std::move(Payload);
  R.IsRaw = true;
  Records.push_back(std::move(R));
}

static void appendU32(std::string &Out, uint32_t V, bool IsBigEndian) {
  char B[4];
  if (IsBigEndian) {
    B[0] = char((V >> 24) & 0xff);
    B[1] = char((V >> 16) & 0xff);
    B[2] = char((V >> 8) & 0xff);
    B[3] = char(V & 0xff);
  } else {
    B[0] = char(V & 0xff);
    B[1] = char((V >> 8) & 0xff);
    B[2] = char((V >> 16) & 0xff);
    B[3] = char((V >> 24) & 0xff);
  }
  Out.append(B, 4);
}

/// Serialise one record's Value into \p Out (Tag/TypeFlags/Length excluded).
static void renderValue(const OutRecord &R, bool IsBigEndian,
                        std::string &Out) {
  // A raw record forwards its Bytes verbatim: this is how tests build inputs
  // that are deliberately malformed (wrong length for the declared type,
  // non-shortest MIX atoms, ...).
  if (R.IsRaw) {
    Out.append(R.Bytes);
    return;
  }
  switch (R.ValueType) {
  case VT_U32:
    appendU32(Out, R.Scalar, IsBigEndian);
    break;
  case VT_UTF8:
  case VT_BYTES:
    Out.append(R.Bytes);
    break;
  case VT_MIX:
    for (const OutRecord &Atom : R.Atoms) {
      // N.4: atom types are plain u8 values without a Critical bit, and the
      // first slice allows only U32/UTF8/BYTES.  Emitting anything else --
      // a nested MIX, the reserved type 0, an unallocated code, or any
      // value with bit 7 set -- would make the writer disagree with the
      // strict reader, so reject it instead of silently repairing it.
      if (Atom.ValueType != VT_U32 && Atom.ValueType != VT_UTF8 &&
          Atom.ValueType != VT_BYTES)
        report_fatal_error(
            "MCS251 attributes: invalid MIX atom type (only U32, UTF8 and "
            "BYTES are allowed, without a Critical bit)");
      Out.push_back(char(Atom.ValueType));
      std::string AtomValue;
      renderValue(Atom, IsBigEndian, AtomValue);
      if (AtomValue.size() > 0xffffffffull)
        report_fatal_error("MCS251 attributes: MIX atom value exceeds the "
                           "32-bit atom Length field");
      encodeULEB128(AtomValue.size(), Out);
      Out.append(AtomValue);
    }
    break;
  default:
    // Raw/unknown payloads: forward Bytes verbatim.
    Out.append(R.Bytes);
    break;
  }
}

std::string Writer::render(bool IsBigEndian) const {
  std::string Payload;
  uint32_t PreviousTag = 0;
  bool First = true;
  for (const OutRecord &R : Records) {
    // N.6 canonical order is a hard render error, not an assert: a Release
    // build without assertions must never serialize a non-canonical record
    // sequence silently.
    if (!First && R.Tag <= PreviousTag)
      report_fatal_error("MCS251 attributes: records must be emitted in "
                         "strictly increasing tag order (tag " +
                         Twine(R.Tag) + " follows tag " + Twine(PreviousTag) +
                         ")");
    First = false;
    PreviousTag = R.Tag;

    encodeULEB128(R.Tag, Payload);

    uint8_t TypeFlags = uint8_t(R.ValueType & TypeMask);
    if (R.Critical)
      TypeFlags |= CriticalMask;
    Payload.push_back(char(TypeFlags));

    std::string Value;
    renderValue(R, IsBigEndian, Value);
    // The record Length field is a 32-bit ULEB128; a wider value cannot be
    // serialized losslessly, so fail instead of narrowing.
    if (Value.size() > 0xffffffffull)
      report_fatal_error("MCS251 attributes: record value exceeds the 32-bit "
                         "Length field");
    encodeULEB128(Value.size(), Payload);
    Payload.append(Value);
  }

  // VendorSize = 16 + P and ScopeSize = 5 + P are 32-bit fields; a payload
  // so large that either sum would wrap must fail instead of narrowing.
  if (Payload.size() >
      0xffffffffull - (VendorSizeBase > ScopeSizeBase ? VendorSizeBase
                                                      : ScopeSizeBase))
    report_fatal_error("MCS251 attributes: payload exceeds the 32-bit "
                       "envelope length fields");

  std::string Out;
  Out.push_back(char(FormatVersion));
  appendU32(Out, VendorSizeBase + uint32_t(Payload.size()), IsBigEndian);
  Out.append(VendorBytes, VendorBytesSize);
  Out.push_back(char(ScopeTagFile));
  appendU32(Out, ScopeSizeBase + uint32_t(Payload.size()), IsBigEndian);
  Out.append(Payload);
  return Out;
}

std::string MCS251Attributes::renderRegisteredIdentity(uint32_t AS0Bits,
                                                       uint32_t Placement) {
  // The v1-era caller has no signature set to offer.  Every v2 object must
  // carry Tag 28, so emitting the empty registered identity here would
  // produce an object the reader rejects; route such a call through the
  // signature-aware overload instead.
  report_fatal_error(
      "MCS251 attributes: renderRegisteredIdentity without function "
      "signatures cannot produce a legal v2 identity (Tag 28 is required); "
      "use the signature-aware overload");
}

std::string MCS251Attributes::renderRegisteredIdentity(
    uint32_t AS0Bits, uint32_t Placement,
    const MCS251Signatures::Table &Signatures) {
  // The A4 minimal registration set (design §2.2): all 21 RequiredTags, plus
  // the P-4 Tag 28, in canonical order, tags 21-23 omitted.  Only the XSmall
  // (32,8) and Small (32,1) profiles are approved for production emission;
  // every other frozen profile fails closed here instead of reaching an
  // object.
  if (!isRegisteredA4Profile(AS0Bits, Placement))
    report_fatal_error(
        "MCS251 attributes: the (as0_pointer_bits, default_placement) pair (" +
        Twine(AS0Bits) + "," + Twine(Placement) +
        ") is not registered for A4 v2 object emission");

  Writer W;
  W.addU32(Tag_ObjectProtocolVersion, ObjectProtocolVersion);
  W.addU32(Tag_CallABIMajor, CallABIMajor);
  W.addU32(Tag_CallABIMinor, CallABIMinor);
  W.addU32(Tag_RegisterParameterVariant, RegisterParameterVariant);
  W.addU32(Tag_GeneralRegisterSet, GeneralRegisterSet);
  W.addU32(Tag_IntBits, IntBits);
  W.addU32(Tag_LongBits, LongBits);
  W.addU32(Tag_AS0PointerBits, AS0Bits);
  W.addU32(Tag_ASLayoutVersion, ASLayoutVersion);
  W.addU32(Tag_DefaultPlacement, Placement);
  W.addU32(Tag_InitProtocolVersion, InitProtocolVersion);
  W.addU32(Tag_PlacementProtocolVersion, PlacementProtocolVersion);
  W.addU32(Tag_StackContractVersion, StackContractVersion);
  W.addU32(Tag_FunctionContractVersion, FunctionContractVersion);
  W.addU32(Tag_RequiredCapabilitiesLo, RequiredCapabilitiesLo);
  W.addU32(Tag_RequiredCapabilitiesHi, RequiredCapabilitiesHi);
  W.addU32(Tag_ABIOptions, ABIOptions);
  W.addMix(Tag_MemoryModelProfile,
           {OutRecord{0, true, VT_U32, AS0Bits, "", {}},
            OutRecord{0, true, VT_U32, Placement, "", {}}});
  W.addU32(Tag_CodeModelProfile, CodeModelProfile);
  W.addU32(Tag_CodePointerBits, CodePointerBits);
  W.addU32(Tag_ObjectProtocolMinor, ObjectProtocolMinor);
  // P-4 Tag 28: the function-signature array is VT_BYTES and Critical.  Its
  // value carries its own little-endian layout, independent of the envelope's
  // target byte order.  The freeze also requires the embedded call_abi
  // generation to agree with this object's identity (Tag 5/6).
  if (llvm::Error E = MCS251Signatures::checkABIGeneration(
          Signatures, uint8_t(CallABIMajor), uint8_t(CallABIMinor)))
    report_fatal_error(Twine("MCS251 attributes: ") + toString(std::move(E)));
  W.addBytes(Tag_FunctionSignatures, MCS251Signatures::encode(Signatures),
             /*Critical=*/true);
  // The MCS-251 ELF target is big-endian; the envelope u32 lengths and the
  // U32 record values are always serialized MSB-first.
  return W.render(/*IsBigEndian=*/true);
}

//===----------------------------------------------------------------------===//
// Reader
//===----------------------------------------------------------------------===//

namespace {

/// Bounded cursor with explicit overflow-safe range checks (N.3: the reader
/// must not compute the end position with a wrapping 32-bit addition).
class Cursor {
public:
  Cursor(StringRef Data) : Data(Data) {}

  size_t remaining() const { return Data.size() - Pos; }
  bool empty() const { return Pos == Data.size(); }
  size_t position() const { return Pos; }
  StringRef slice(size_t Offset, size_t Len) const {
    return Data.substr(Offset, Len);
  }

  bool readU8(uint8_t &Out) {
    if (remaining() < 1)
      return false;
    Out = uint8_t(Data[Pos++]);
    return true;
  }

  bool readU32(bool IsBigEndian, uint32_t &Out) {
    if (remaining() < 4)
      return false;
    const uint8_t *P = reinterpret_cast<const uint8_t *>(Data.data() + Pos);
    if (IsBigEndian)
      Out = (uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
            (uint32_t(P[2]) << 8) | uint32_t(P[3]);
    else
      Out = (uint32_t(P[3]) << 24) | (uint32_t(P[2]) << 16) |
            (uint32_t(P[1]) << 8) | uint32_t(P[0]);
    Pos += 4;
    return true;
  }

  bool readBytes(size_t Len, StringRef &Out) {
    if (remaining() < Len)
      return false;
    Out = Data.substr(Pos, Len);
    Pos += Len;
    return true;
  }

  bool skip(size_t Len) {
    if (remaining() < Len)
      return false;
    Pos += Len;
    return true;
  }

private:
  StringRef Data;
  size_t Pos = 0;
};

/// Read a shortest-form ULEB128 bounded to 5 bytes / 32 bits (N.4).
llvm::Error readULEB128(Cursor &C, uint32_t &Out, const char *What) {
  uint64_t Value = 0;
  unsigned Shift = 0;
  unsigned Count = 0;
  while (true) {
    uint8_t Byte = 0;
    if (!C.readU8(Byte))
      return makeError([&](raw_ostream &OS) {
        OS << "truncated ULEB128 in " << What;
      });
    ++Count;
    if (Count > 5)
      return makeError([&](raw_ostream &OS) {
        OS << "ULEB128 longer than 5 bytes in " << What;
      });
    Value |= uint64_t(Byte & 0x7fu) << Shift;
    if (!(Byte & 0x80u)) {
      // Shortest form: the final byte must not be a redundant high zero.
      if (Count > 1 && (Byte & 0x7fu) == 0)
        return makeError([&](raw_ostream &OS) {
          OS << "non-shortest ULEB128 in " << What;
        });
      if (Value > 0xffffffffull)
        return makeError([&](raw_ostream &OS) {
          OS << "ULEB128 exceeds 32 bits in " << What;
        });
      Out = uint32_t(Value);
      return Error::success();
    }
    Shift += 7;
    if (Shift >= 35)
      return makeError([&](raw_ostream &OS) {
        OS << "ULEB128 exceeds 32 bits in " << What;
      });
  }
}

/// \return true when \p T is a tag registered in this revision.
bool isKnownTag(uint32_t T) {
  return T >= Tag_FirstAllocated && T <= Tag_LastRegistered;
}

/// Attributes 0..3 are reserved by the scope namespace (DESIGN.md N.4): the
/// first allocated attribute tag is 4.  A record carrying one of them is not
/// an unknown-but-ignorable extension -- it is an out-of-namespace tag, so it
/// must be rejected whether or not it is marked Critical.
bool isReservedNamespaceTag(uint32_t T) {
  return T < Tag_FirstAllocated;
}

/// Tags that require Critical = 1 with type U32.
bool isRequiredU32Tag(uint32_t T) {
  switch (T) {
  case Tag_ObjectProtocolVersion:
  case Tag_CallABIMajor:
  case Tag_CallABIMinor:
  case Tag_RegisterParameterVariant:
  case Tag_GeneralRegisterSet:
  case Tag_IntBits:
  case Tag_LongBits:
  case Tag_AS0PointerBits:
  case Tag_ASLayoutVersion:
  case Tag_DefaultPlacement:
  case Tag_InitProtocolVersion:
  case Tag_PlacementProtocolVersion:
  case Tag_StackContractVersion:
  case Tag_FunctionContractVersion:
  case Tag_RequiredCapabilitiesLo:
  case Tag_RequiredCapabilitiesHi:
  case Tag_ABIOptions:
  case Tag_CodeModelProfile:
  case Tag_CodePointerBits:
  case Tag_ObjectProtocolMinor:
    return true;
  default:
    return false;
  }
}

llvm::Error checkMIXSchema(const Record &R, bool IsBigEndian) {
  // Tag 24 memory_model_profile: exactly two U32 atoms.
  if (R.Tag != Tag_MemoryModelProfile)
    return Error::success();
  if (R.ValueType != VT_MIX)
    return makeError([&](raw_ostream &OS) {
      OS << "memory_model_profile must be MIX";
    });
  Cursor C(StringRef(reinterpret_cast<const char *>(R.Value.data()),
                     R.Value.size()));
  unsigned Atoms = 0;
  uint32_t AS0 = 0, Placement = 0;
  while (!C.empty()) {
    uint8_t AtomType = 0;
    if (!C.readU8(AtomType))
      return makeError([&](raw_ostream &OS) {
        OS << "memory_model_profile atom truncated";
      });
    // DESIGN.md N.4: an atom type is a plain u8 with no Critical bit.  A set
    // bit 7 is not an unknown-but-ignorable atom type, so it is rejected
    // rather than masked off.
    if (AtomType & CriticalMask)
      return makeError([&](raw_ostream &OS) {
        OS << "memory_model_profile atom type " << format_hex(AtomType, 4)
           << " has a Critical bit, which MIX atoms do not carry";
      });
    if (AtomType != VT_U32)
      return makeError([&](raw_ostream &OS) {
        OS << "memory_model_profile atom must be U32, got ";
        printValueType(OS, AtomType);
      });
    uint32_t Len = 0;
    if (llvm::Error E = readULEB128(C, Len, "memory_model_profile atom length"))
      return E;
    if (Len != 4)
      return makeError([&](raw_ostream &OS) {
        OS << "memory_model_profile U32 atom must be 4 bytes, got " << Len;
      });
    StringRef Payload;
    if (!C.readBytes(4, Payload))
      return makeError([&](raw_ostream &OS) {
        OS << "memory_model_profile atom value truncated";
      });
    const uint8_t *P = reinterpret_cast<const uint8_t *>(Payload.data());
    uint32_t V;
    if (IsBigEndian)
      V = (uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
          (uint32_t(P[2]) << 8) | uint32_t(P[3]);
    else
      V = (uint32_t(P[3]) << 24) | (uint32_t(P[2]) << 16) |
          (uint32_t(P[1]) << 8) | uint32_t(P[0]);
    if (Atoms == 0)
      AS0 = V;
    else if (Atoms == 1)
      Placement = V;
    ++Atoms;
    if (Atoms > 2)
      return makeError([&](raw_ostream &OS) {
        OS << "memory_model_profile must have exactly two atoms";
      });
  }
  if (Atoms != 2)
    return makeError([&](raw_ostream &OS) {
      OS << "memory_model_profile must have exactly two atoms, got " << Atoms;
    });
  // Consistency against Tag 11 / Tag 13 is checked after all records are read.
  (void)AS0;
  (void)Placement;
  return Error::success();
}

} // end anonymous namespace

llvm::Error MCS251Attributes::decode(StringRef Bytes, bool IsBigEndian,
                                     Decoded &Out) {
  Out = Decoded();

  // --- Envelope -----------------------------------------------------------
  if (Bytes.size() < EnvelopeSize)
    return makeError([&](raw_ostream &OS) {
      OS << "section smaller than the " << EnvelopeSize
         << "-byte envelope (" << Bytes.size() << " bytes)";
    });

  uint8_t Format = uint8_t(Bytes[0]);
  if (Format != FormatVersion)
    return makeError([&](raw_ostream &OS) {
      OS << "envelope format is " << format_hex(Format, 4) << ", expected "
         << format_hex(FormatVersion, 4);
    });

  auto readBE32At = [&](size_t Off, uint32_t &V) {
    const uint8_t *P = reinterpret_cast<const uint8_t *>(Bytes.data() + Off);
    if (IsBigEndian)
      V = (uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
          (uint32_t(P[2]) << 8) | uint32_t(P[3]);
    else
      V = (uint32_t(P[3]) << 24) | (uint32_t(P[2]) << 16) |
          (uint32_t(P[1]) << 8) | uint32_t(P[0]);
  };

  uint32_t VendorSize = 0, ScopeSize = 0;
  readBE32At(1, VendorSize);
  readBE32At(13, ScopeSize);

  StringRef SectionVendor = Bytes.substr(5, VendorBytesSize);
  if (SectionVendor != StringRef(VendorBytes, VendorBytesSize))
    return makeError([&](raw_ostream &OS) {
      OS << "vendor is not exactly " << VendorBytesSize
         << " bytes \"MCS251\\0\"";
    });

  uint8_t Scope = uint8_t(Bytes[12]);
  if (Scope != ScopeTagFile)
    return makeError([&](raw_ostream &OS) {
      OS << "scope tag " << format_hex(Scope, 4) << " is not the File scope ("
         << unsigned(ScopeTagFile) << ")";
    });

  // VendorSize = sh_size - 1; ScopeSize = VendorSize - 11.
  if (VendorSize != Bytes.size() - 1)
    return makeError([&](raw_ostream &OS) {
      OS << "VendorSize " << VendorSize << " does not match section size "
         << Bytes.size() << " (expected " << (Bytes.size() - 1) << ")";
    });
  if (VendorSize < 11 || ScopeSize != VendorSize - 11)
    return makeError([&](raw_ostream &OS) {
      OS << "ScopeSize " << ScopeSize << " inconsistent with VendorSize "
         << VendorSize;
    });

  Out.VendorSize = VendorSize;
  Out.ScopeSize = ScopeSize;
  Out.PayloadSize = uint32_t(Bytes.size() - EnvelopeSize);

  StringRef Payload = Bytes.substr(EnvelopeSize);

  // --- Records ------------------------------------------------------------
  Cursor C(Payload);
  while (!C.empty()) {
    Record R;
    if (llvm::Error E = readULEB128(C, R.Tag, "tag"))
      return E; // bounds/overflow/shortest checked here

    uint8_t TypeFlags = 0;
    if (!C.readU8(TypeFlags))
      return makeError([&](raw_ostream &OS) {
        OS << "missing TypeFlags for ";
        printTag(OS, R.Tag);
      });
    R.Critical = (TypeFlags & CriticalMask) != 0;
    R.ValueType = uint8_t(TypeFlags & TypeMask);

    if (llvm::Error E = readULEB128(C, R.Length, "record length"))
      return E;

    if (R.ValueType == VT_Reserved)
      return makeError([&](raw_ostream &OS) {
        printTag(OS, R.Tag);
        OS << " uses reserved value type 0";
      });

    StringRef Value;
    if (!C.readBytes(R.Length, Value))
      return makeError([&](raw_ostream &OS) {
        OS << "value of ";
        printTag(OS, R.Tag);
        OS << " (length " << R.Length << ") runs past the end of the scope";
      });
    R.Value.assign(Value.begin(), Value.end());

    // Duplicate rejection covers known and unknown tags alike (N.6).
    if (Out.find(R.Tag))
      return makeError([&](raw_ostream &OS) {
        OS << "duplicate ";
        printTag(OS, R.Tag);
      });

    // Tags 0..3 belong to the scope namespace, not the attribute namespace.
    // They are never a skippable extension.
    if (isReservedNamespaceTag(R.Tag))
      return makeError([&](raw_ostream &OS) {
        OS << "tag " << R.Tag << " is in the reserved scope namespace (0.."
           << (Tag_FirstAllocated - 1) << "); the first attribute tag is "
           << Tag_FirstAllocated;
      });

    // Unknown tags: bounded skip already happened; only Critical matters.
    if (!isKnownTag(R.Tag)) {
      if (R.Critical)
        return makeError([&](raw_ostream &OS) {
          OS << "unknown Critical ";
          printTag(OS, R.Tag);
          OS << " cannot be ignored";
        });
      Out.Records.push_back(std::move(R));
      continue;
    }

    // Known tag: type and Critical must match the registry.
    if (isRequiredU32Tag(R.Tag)) {
      if (R.ValueType != VT_U32)
        return makeError([&](raw_ostream &OS) {
          printTag(OS, R.Tag);
          OS << " must be U32, got ";
          printValueType(OS, R.ValueType);
        });
      if (!R.Critical)
        return makeError([&](raw_ostream &OS) {
          OS << "required ";
          printTag(OS, R.Tag);
          OS << " must be Critical";
        });
      if (R.Length != 4)
        return makeError([&](raw_ostream &OS) {
          OS << "U32 ";
          printTag(OS, R.Tag);
          OS << " must have length 4, got " << R.Length;
        });
      const uint8_t *P = reinterpret_cast<const uint8_t *>(R.Value.data());
      if (IsBigEndian)
        R.Scalar = (uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
                   (uint32_t(P[2]) << 8) | uint32_t(P[3]);
      else
        R.Scalar = (uint32_t(P[3]) << 24) | (uint32_t(P[2]) << 16) |
                   (uint32_t(P[1]) << 8) | uint32_t(P[0]);
    } else if (R.Tag == Tag_MemoryModelProfile) {
      if (R.ValueType != VT_MIX)
        return makeError([&](raw_ostream &OS) {
          printTag(OS, R.Tag);
          OS << " must be MIX, got ";
          printValueType(OS, R.ValueType);
        });
      if (!R.Critical)
        return makeError([&](raw_ostream &OS) {
          printTag(OS, R.Tag);
          OS << " must be Critical";
        });
      if (llvm::Error E = checkMIXSchema(R, IsBigEndian))
        return E;
    } else if (R.Tag == Tag_Reserved0 || R.Tag == Tag_Reserved1 ||
               R.Tag == Tag_Reserved2) {
      // Optional reserved words: if present, they must be U32 zero.
      if (R.ValueType != VT_U32 || R.Length != 4)
        return makeError([&](raw_ostream &OS) {
          OS << "reserved ";
          printTag(OS, R.Tag);
          OS << " must be U32, got ";
          printValueType(OS, R.ValueType);
        });
      if (R.Critical)
        return makeError([&](raw_ostream &OS) {
          OS << "reserved ";
          printTag(OS, R.Tag);
          OS << " must not be Critical";
        });
      const uint8_t *P = reinterpret_cast<const uint8_t *>(R.Value.data());
      uint32_t V = IsBigEndian
                       ? ((uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
                          (uint32_t(P[2]) << 8) | uint32_t(P[3]))
                       : ((uint32_t(P[3]) << 24) | (uint32_t(P[2]) << 16) |
                          (uint32_t(P[1]) << 8) | uint32_t(P[0]));
      if (V != 0)
        return makeError([&](raw_ostream &OS) {
          OS << "reserved ";
          printTag(OS, R.Tag);
          OS << " must be zero, got " << V;
        });
      R.Scalar = V;
    } else if (R.Tag == Tag_FunctionSignatures) {
      // P-4: Tag 28 is VT_BYTES and Critical.  Its payload is the frozen
      // signature value; the strict sub-decoder below enforces every
      // format-level rule.  The object-identity CallABI agreement is checked
      // once all records are known (after this loop).
      if (R.ValueType != VT_BYTES)
        return makeError([&](raw_ostream &OS) {
          printTag(OS, R.Tag);
          OS << " must be BYTES, got ";
          printValueType(OS, R.ValueType);
        });
      if (!R.Critical)
        return makeError([&](raw_ostream &OS) {
          OS << "required ";
          printTag(OS, R.Tag);
          OS << " must be Critical";
        });
      StringRef Payload(reinterpret_cast<const char *>(R.Value.data()),
                        R.Value.size());
      MCS251Signatures::Table SigTable;
      if (llvm::Error E = MCS251Signatures::decode(Payload, SigTable))
        return E;
      Out.HasSignatures = true;
      Out.Signatures = std::move(SigTable);
    } else {
      // Registered but not yet assigned a concrete type rule.
      if (R.Critical)
        return makeError([&](raw_ostream &OS) {
          printTag(OS, R.Tag);
          OS << " has no implemented schema in this revision and cannot be "
                "treated as Critical";
        });
      // No concrete type rule: the value bytes were never schema-decoded;
      // keep SchemaValidated false so consumers display raw bytes.
      Out.Records.push_back(std::move(R));
      continue;
    }

    // Known tag with a concrete type rule: the checks above validated the
    // record (and decoded Scalar where applicable).
    R.SchemaValidated = true;
    Out.Records.push_back(std::move(R));
  }

  // --- Required tags ------------------------------------------------------
  for (uint32_t T : RequiredTags) {
    const Record *R = Out.find(T);
    if (!R)
      return makeError([&](raw_ostream &OS) {
        OS << "required ";
        printTag(OS, T);
        OS << " is missing";
      });
  }

  // --- Ruled-value and internal-consistency checks (N.5/N.6) -------------
  auto getU32 = [&](uint32_t T) -> uint32_t {
    const Record *R = Out.find(T);
    return R ? R->Scalar : 0u;
  };

  if (getU32(Tag_ObjectProtocolVersion) != ObjectProtocolVersion)
    return makeError([&](raw_ostream &OS) {
      OS << "object_protocol_version must be " << ObjectProtocolVersion
         << ", got " << getU32(Tag_ObjectProtocolVersion);
    });
  if (getU32(Tag_IntBits) != IntBits)
    return makeError([&](raw_ostream &OS) {
      OS << "int_bits must be " << IntBits << ", got "
         << getU32(Tag_IntBits);
    });
  if (getU32(Tag_LongBits) != LongBits)
    return makeError([&](raw_ostream &OS) {
      OS << "long_bits must be " << LongBits << ", got "
         << getU32(Tag_LongBits);
    });
  if (getU32(Tag_CodePointerBits) != CodePointerBits)
    return makeError([&](raw_ostream &OS) {
      OS << "code_pointer_bits must be " << CodePointerBits << ", got "
         << getU32(Tag_CodePointerBits);
    });
  if (getU32(Tag_GeneralRegisterSet) != GeneralRegisterSet)
    return makeError([&](raw_ostream &OS) {
      OS << "general_register_set must be "
         << format_hex_no_prefix(GeneralRegisterSet, 8) << ", got "
         << format_hex_no_prefix(getU32(Tag_GeneralRegisterSet), 8);
    });

  // --- A4-registered values (PM ruling 2026-09-13; design §2) ------------
  // These fields were the N.5/N.9 open set; the ruling registered exactly
  // one value per field, so the decoder enforces equality.  A candidate or
  // future value must be rejected here, not silently defaulted.
  for (auto [T, Registered, Name] :
       std::initializer_list<std::tuple<uint32_t, uint32_t, const char *>>{
           {Tag_CallABIMajor, CallABIMajor, "call_abi_major"},
           {Tag_CallABIMinor, CallABIMinor, "call_abi_minor"},
           {Tag_RegisterParameterVariant, RegisterParameterVariant,
            "register_parameter_variant"},
           {Tag_ASLayoutVersion, ASLayoutVersion, "as_layout_version"},
           {Tag_InitProtocolVersion, InitProtocolVersion,
            "init_protocol_version"},
           {Tag_PlacementProtocolVersion, PlacementProtocolVersion,
            "placement_protocol_version"},
           {Tag_StackContractVersion, StackContractVersion,
            "stack_contract_version"},
           {Tag_FunctionContractVersion, FunctionContractVersion,
            "function_contract_version"},
           {Tag_RequiredCapabilitiesLo, RequiredCapabilitiesLo,
            "required_capabilities_lo"},
           {Tag_RequiredCapabilitiesHi, RequiredCapabilitiesHi,
            "required_capabilities_hi"},
           {Tag_ABIOptions, ABIOptions, "abi_options"},
           {Tag_CodeModelProfile, CodeModelProfile, "code_model_profile"},
           {Tag_ObjectProtocolMinor, ObjectProtocolMinor,
            "object_protocol_minor"},
       })
    if (getU32(T) != Registered)
      return makeError([&](raw_ostream &OS) {
        OS << Name << " must be " << Registered << ", got " << getU32(T)
           << " (the A4-registered value is the only approved value)";
      });

  // P-4: every signature record's call_abi generation must equal the object
  // identity's own (Tag 5 / Tag 6).  This is the one Tag 28 rule the value
  // sub-decoder cannot enforce on its own.
  if (Out.HasSignatures)
    if (llvm::Error E = MCS251Signatures::checkABIGeneration(
            Out.Signatures, uint8_t(getU32(Tag_CallABIMajor)),
            uint8_t(getU32(Tag_CallABIMinor))))
      return E;

  uint32_t AS0 = getU32(Tag_AS0PointerBits);
  if (AS0 != AS0PointerBits16 && AS0 != AS0PointerBits32)
    return makeError([&](raw_ostream &OS) {
      OS << "as0_pointer_bits must be " << AS0PointerBits16 << " or "
         << AS0PointerBits32 << ", got " << AS0;
    });

  uint32_t Placement = getU32(Tag_DefaultPlacement);
  if (Placement != Placement_InternalMovable &&
      Placement != Placement_ExternalData &&
      Placement != Placement_InternalExtended)
    return makeError([&](raw_ostream &OS) {
      OS << "default_placement must be " << Placement_InternalMovable << ", "
         << Placement_ExternalData << " or " << Placement_InternalExtended
         << ", got " << Placement;
    });

  // N.5: the (as0_pointer_bits, default_placement) pair must be one of the
  // five frozen memory model profiles.  The individual field domains above
  // are necessary but not sufficient: e.g. (16, ExternalData) matches both
  // scalar domains yet is not a profile.
  {
    bool IsFrozenProfile = false;
    for (const MemoryModelProfile &P :
         {MemoryModelProfile_Tiny, MemoryModelProfile_XTiny,
          MemoryModelProfile_Small, MemoryModelProfile_XSmall,
          MemoryModelProfile_Large})
      if (P.AS0PointerBits == AS0 && P.Placement == Placement) {
        IsFrozenProfile = true;
        break;
      }
    if (!IsFrozenProfile)
      return makeError([&](raw_ostream &OS) {
        OS << "as0_pointer_bits/default_placement pair (" << AS0 << ","
           << Placement
           << ") is not one of the five frozen memory model profiles";
      });
  }

  // memory_model_profile must agree with Tag 11 and Tag 13.
  {
    const Record *MM = Out.find(Tag_MemoryModelProfile);
    Cursor C(StringRef(reinterpret_cast<const char *>(MM->Value.data()),
                       MM->Value.size()));
    uint32_t Vals[2] = {0, 0};
    unsigned N = 0;
    while (!C.empty() && N < 2) {
      uint8_t AtomType = 0;
      C.readU8(AtomType);
      uint32_t Len = 0;
      if (llvm::Error E = readULEB128(C, Len, "memory_model_profile atom length"))
        return E;
      StringRef Payload;
      if (!C.readBytes(Len, Payload))
        return makeError([&](raw_ostream &OS) {
          OS << "memory_model_profile atom value truncated";
        });
      const uint8_t *P = reinterpret_cast<const uint8_t *>(Payload.data());
      Vals[N] = IsBigEndian ? ((uint32_t(P[0]) << 24) | (uint32_t(P[1]) << 16) |
                               (uint32_t(P[2]) << 8) | uint32_t(P[3]))
                            : ((uint32_t(P[3]) << 24) | (uint32_t(P[2]) << 16) |
                               (uint32_t(P[1]) << 8) | uint32_t(P[0]));
      ++N;
    }
    if (N == 2) {
      if (Vals[0] != AS0)
        return makeError([&](raw_ostream &OS) {
          OS << "memory_model_profile as0_pointer_bits " << Vals[0]
             << " disagrees with ";
          printTag(OS, Tag_AS0PointerBits);
          OS << " (" << AS0 << ")";
        });
      if (Vals[1] != Placement)
        return makeError([&](raw_ostream &OS) {
          OS << "memory_model_profile default_placement " << Vals[1]
             << " disagrees with ";
          printTag(OS, Tag_DefaultPlacement);
          OS << " (" << Placement << ")";
        });
    }
  }

  return Error::success();
}
