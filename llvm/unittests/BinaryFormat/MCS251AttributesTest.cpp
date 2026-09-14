//===- MCS251AttributesTest.cpp - v2 identity codec unit tests ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Line-format tests for the MCS-251 v2 identity carrier.  The canonical byte
// string is the fixture frozen in DESIGN.md N.7; every rejection case is drawn
// from the N.6 rejection table.
//
// A4 status (PM ruling 2026-09-13): the complete identities in these tests
// carry the REGISTERED value set (design §2), which the decoder now enforces;
// the registered XSmall/Small identities here are byte-for-byte the payloads
// production objects publish.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MCS251AttributesReader.h"
#include "llvm/BinaryFormat/MCS251AttributesWriter.h"
#include "llvm/Support/Error.h"
#include <cctype>
#include <optional>
#include <string>
#include <gtest/gtest.h>

using namespace llvm;
using namespace llvm::MCS251Attributes;

namespace {

/// Decode a hex string into bytes.
std::string fromHex(StringRef Hex) {
  std::string Out;
  for (size_t I = 0; I + 1 < Hex.size(); I += 2) {
    auto Nibble = [](char C) -> unsigned {
      if (C >= '0' && C <= '9') return unsigned(C - '0');
      if (C >= 'a' && C <= 'f') return unsigned(C - 'a' + 10);
      if (C >= 'A' && C <= 'F') return unsigned(C - 'A' + 10);
      return 0;
    };
    Out.push_back(char((Nibble(Hex[I]) << 4) | Nibble(Hex[I + 1])));
  }
  return Out;
}

std::string toHex(StringRef Bytes) {
  static const char *Digits = "0123456789abcdef";
  std::string Out;
  for (unsigned char C : Bytes) {
    Out.push_back(Digits[C >> 4]);
    Out.push_back(Digits[C & 0xf]);
  }
  return Out;
}

/// The DESIGN.md N.7 fixture: two required attributes plus one unknown
/// optional attribute.  It is intentionally NOT a complete v2 identity, so it
/// must be rejected for missing required tags -- but its envelope and record
/// decoding must succeed up to that point, and the byte string must round-trip.
constexpr const char *FixtureHex =
    "41000000254D435332353100010000001A"
    "04810400000002"
    "09810400000020"
    "80020203616263";

class MCS251AttributesTest : public ::testing::Test {};

// A complete first-slice identity with all required tags and the registered
// values (the XSmall profile: as0_pointer_bits 32, default_placement 8),
// minus the one tag named by \p Skip (nullopt = complete).  The field values
// are the A4-registered set (design §2, PM ruling 2026-09-13), so the
// rendered bytes are exactly the payload a production v2 object carries.
static void addCompleteIdentity(Writer &W,
                                std::optional<uint32_t> Skip = std::nullopt) {
  auto U32 = [&](uint32_t Tag, uint32_t Value) {
    if (Tag != Skip)
      W.addU32(Tag, Value);
  };
  U32(Tag_ObjectProtocolVersion, ObjectProtocolVersion);
  U32(Tag_CallABIMajor, CallABIMajor);
  U32(Tag_CallABIMinor, CallABIMinor);
  U32(Tag_RegisterParameterVariant, RegisterParameterVariant);
  U32(Tag_GeneralRegisterSet, GeneralRegisterSet);
  U32(Tag_IntBits, IntBits);
  U32(Tag_LongBits, LongBits);
  U32(Tag_AS0PointerBits, AS0PointerBits32);
  U32(Tag_ASLayoutVersion, ASLayoutVersion);
  U32(Tag_DefaultPlacement, Placement_InternalExtended);
  U32(Tag_InitProtocolVersion, InitProtocolVersion);
  U32(Tag_PlacementProtocolVersion, PlacementProtocolVersion);
  U32(Tag_StackContractVersion, StackContractVersion);
  U32(Tag_FunctionContractVersion, FunctionContractVersion);
  U32(Tag_RequiredCapabilitiesLo, RequiredCapabilitiesLo);
  U32(Tag_RequiredCapabilitiesHi, RequiredCapabilitiesHi);
  U32(Tag_ABIOptions, ABIOptions);
  if (Tag_MemoryModelProfile != Skip)
    W.addMix(Tag_MemoryModelProfile,
             {OutRecord{0, true, VT_U32, AS0PointerBits32, "", {}, false},
              OutRecord{0, true, VT_U32, Placement_InternalExtended, "", {},
                        false}});
  U32(Tag_CodeModelProfile, CodeModelProfile);
  U32(Tag_CodePointerBits, CodePointerBits);
  U32(Tag_ObjectProtocolMinor, ObjectProtocolMinor);
}

TEST_F(MCS251AttributesTest, EnvelopeDecodesFromFixture) {
  std::string Bytes = fromHex(FixtureHex);
  EXPECT_EQ(Bytes.size(), 38u) << "fixture must be 38 bytes (17 + P=21)";

  Decoded D;
  Error E = decode(Bytes, /*IsBigEndian=*/true, D);
  // The fixture is not a complete identity, so a missing-required-tag error is
  // expected; the envelope numbers must still have been parsed.
  if (E) {
    std::string Msg = toString(std::move(E));
    EXPECT_NE(Msg.find("is missing"), std::string::npos) << Msg;
  }
}

TEST_F(MCS251AttributesTest, EnvelopeLengthsMatchRuling) {
  // VendorSize = 16 + P, ScopeSize = 5 + P, section = 17 + P, with P = 21.
  std::string Bytes = fromHex(FixtureHex);
  ASSERT_EQ(Bytes.size(), 38u);
  EXPECT_EQ(uint8_t(Bytes[0]), 0x41);
  // VendorSize at offset 1, big-endian: 37 = 16 + 21.
  EXPECT_EQ(uint8_t(Bytes[1]), 0x00);
  EXPECT_EQ(uint8_t(Bytes[2]), 0x00);
  EXPECT_EQ(uint8_t(Bytes[3]), 0x00);
  EXPECT_EQ(uint8_t(Bytes[4]), 0x25);
  // Vendor "MCS251\0" at offset 5..11.
  EXPECT_EQ(Bytes.substr(5, 7), std::string("MCS251\0", 7));
  // ScopeTag = 1 at offset 12.
  EXPECT_EQ(uint8_t(Bytes[12]), 0x01);
  // ScopeSize at offset 13, big-endian: 26 = 5 + 21.
  EXPECT_EQ(uint8_t(Bytes[17 - 4]), 0x00);
  EXPECT_EQ(uint8_t(Bytes[14]), 0x00);
  EXPECT_EQ(uint8_t(Bytes[15]), 0x00);
  EXPECT_EQ(uint8_t(Bytes[16]), 0x1A);
}

TEST_F(MCS251AttributesTest, UnknownOptionalTagIsSkippedByLength) {
  // Tag 256 is unallocated and optional; its Length=3 must place the next
  // record exactly 3 bytes later.  Append a synthetic decoder probe: a second
  // attribute after it must be seen.
  Writer W;
  W.addRaw(256, VT_UTF8, /*Critical=*/false, std::string("abc"));
  W.addU32(400, 0x11223344, /*Critical=*/false);
  std::string Bytes = W.render(/*IsBigEndian=*/true);

  Decoded D;
  // Unknown optional tags are accepted; the object is then rejected only for
  // missing required tags.
  Error E = decode(Bytes, true, D);
  if (E)
    EXPECT_NE(toString(std::move(E)).find("is missing"), std::string::npos);
  // Both unknown records must have been collected, and the second must begin
  // exactly at the first record's value end (the skip is length-driven, not
  // NUL- or heuristic-driven).  Unknown tags are not schema-interpreted, so
  // their Scalar is not required to be populated.
  const Record *R256 = D.find(256);
  const Record *R400 = D.find(400);
  ASSERT_NE(R256, nullptr);
  ASSERT_NE(R400, nullptr);
  EXPECT_EQ(R256->Length, 3u);
  EXPECT_EQ(R256->ValueType, unsigned(VT_UTF8));
  EXPECT_EQ(std::string(R256->Value.begin(), R256->Value.end()),
            std::string("abc"));
  EXPECT_EQ(R400->Length, 4u);
}

TEST_F(MCS251AttributesTest, UnknownCriticalTagIsRejected) {
  Writer W;
  W.addRaw(256, VT_U32, /*Critical=*/true, std::string("\0\0\0\1", 4));
  std::string Bytes = W.render(true);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("unknown Critical tag"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, ReservedValueTypeZeroIsRejected) {
  Writer W;
  W.addRaw(4, VT_Reserved, /*Critical=*/true, "");
  std::string Bytes = W.render(true);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("reserved value type 0"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, NonShortestULEBIsRejected) {
  // Build a record whose tag uses a two-byte encoding of the value 4.
  std::string Payload;
  encodeULEB128Padded(4, 2, Payload);
  Payload.push_back(char(uint8_t(VT_U32) | CriticalMask));
  encodeULEB128(4, Payload);
  Payload.append(std::string("\0\0\0\2", 4));

  std::string Bytes;
  Bytes.push_back(char(FormatVersion));
  auto appendBE = [&](uint32_t V) {
    Bytes.push_back(char((V >> 24) & 0xff));
    Bytes.push_back(char((V >> 16) & 0xff));
    Bytes.push_back(char((V >> 8) & 0xff));
    Bytes.push_back(char(V & 0xff));
  };
  appendBE(16 + uint32_t(Payload.size()));
  Bytes.append("MCS251\0", 7);
  Bytes.push_back(char(ScopeTagFile));
  appendBE(5 + uint32_t(Payload.size()));
  Bytes.append(Payload);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("non-shortest ULEB128"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, TruncatedValueIsRejected) {
  // Claim a value longer than the remaining scope.  The section size is
  // NOT changed (the envelope lengths stay consistent), and the patched
  // length still encodes in one ULEB byte, so the rejection must come from
  // the record's own value read ("runs past the end of the scope"), not
  // from the outer envelope check.
  Writer W;
  W.addRaw(300, VT_U32, /*Critical=*/false, std::string("\0\0\0", 3));
  std::string Bytes = W.render(true);
  ASSERT_GT(Bytes.size(), 4u);
  // The last record is tag(2) + TypeFlags(1) + Length(1) + value(3): patch
  // its Length byte from 3 to 5 with only 3 payload bytes remaining.
  Bytes[Bytes.size() - 4] = char(5);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  std::string Msg = toString(std::move(E));
  EXPECT_NE(Msg.find("runs past the end of the scope"), std::string::npos)
      << "actual: " << Msg;
}

TEST_F(MCS251AttributesTest, DuplicateTagIsRejected) {
  // Two records with the same tag: hand-build so the writer's ordering
  // assertion does not fire.
  std::string Payload;
  encodeULEB128(4, Payload);
  Payload.push_back(char(uint8_t(VT_U32) | CriticalMask));
  encodeULEB128(4, Payload);
  Payload.append(std::string("\0\0\0\2", 4));
  // Repeat the identical record.
  encodeULEB128(4, Payload);
  Payload.push_back(char(uint8_t(VT_U32) | CriticalMask));
  encodeULEB128(4, Payload);
  Payload.append(std::string("\0\0\0\2", 4));

  std::string Bytes;
  Bytes.push_back(char(FormatVersion));
  auto appendBE = [&](uint32_t V) {
    Bytes.push_back(char((V >> 24) & 0xff));
    Bytes.push_back(char((V >> 16) & 0xff));
    Bytes.push_back(char((V >> 8) & 0xff));
    Bytes.push_back(char(V & 0xff));
  };
  appendBE(16 + uint32_t(Payload.size()));
  Bytes.append("MCS251\0", 7);
  Bytes.push_back(char(ScopeTagFile));
  appendBE(5 + uint32_t(Payload.size()));
  Bytes.append(Payload);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("duplicate object_protocol_version(4)"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, BadFormatByteIsRejected) {
  std::string Bytes = fromHex(FixtureHex);
  Bytes[0] = char(0x42);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("format"), std::string::npos);
}

TEST_F(MCS251AttributesTest, WrongVendorIsRejected) {
  std::string Bytes = fromHex(FixtureHex);
  Bytes[5] = 'X';
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("vendor"), std::string::npos);
}

TEST_F(MCS251AttributesTest, WrongScopeTagIsRejected) {
  std::string Bytes = fromHex(FixtureHex);
  Bytes[12] = char(0x02);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("scope"), std::string::npos);
}

TEST_F(MCS251AttributesTest, TrailingBytesAreRejected) {
  // SectionSize must equal 17 + P exactly; an extra byte makes VendorSize
  // disagree with the section length.
  std::string Bytes = fromHex(FixtureHex);
  Bytes.push_back(char(0x55));
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("VendorSize"), std::string::npos);
}

TEST_F(MCS251AttributesTest, CompleteIdentityRoundTrips) {
  // Emit a complete first-slice identity with all required tags plus the
  // registered values, then decode it and confirm every field survives.
  Writer W;
  W.addU32(Tag_ObjectProtocolVersion, ObjectProtocolVersion);
  W.addU32(Tag_CallABIMajor, CallABIMajor);
  W.addU32(Tag_CallABIMinor, CallABIMinor);
  W.addU32(Tag_RegisterParameterVariant, RegisterParameterVariant);
  W.addU32(Tag_GeneralRegisterSet, GeneralRegisterSet);
  W.addU32(Tag_IntBits, IntBits);
  W.addU32(Tag_LongBits, LongBits);
  W.addU32(Tag_AS0PointerBits, AS0PointerBits32);
  W.addU32(Tag_ASLayoutVersion, ASLayoutVersion);
  W.addU32(Tag_DefaultPlacement, Placement_InternalExtended);
  W.addU32(Tag_InitProtocolVersion, InitProtocolVersion);
  W.addU32(Tag_PlacementProtocolVersion, PlacementProtocolVersion);
  W.addU32(Tag_StackContractVersion, StackContractVersion);
  W.addU32(Tag_FunctionContractVersion, FunctionContractVersion);
  W.addU32(Tag_RequiredCapabilitiesLo, RequiredCapabilitiesLo);
  W.addU32(Tag_RequiredCapabilitiesHi, RequiredCapabilitiesHi);
  W.addU32(Tag_ABIOptions, ABIOptions);
  W.addMix(Tag_MemoryModelProfile,
           {OutRecord{0, true, VT_U32, AS0PointerBits32, "", {}},
            OutRecord{0, true, VT_U32, Placement_InternalExtended, "", {}}});
  W.addU32(Tag_CodeModelProfile, CodeModelProfile);
  W.addU32(Tag_CodePointerBits, CodePointerBits);
  W.addU32(Tag_ObjectProtocolMinor, ObjectProtocolMinor);

  std::string Bytes = W.render(/*IsBigEndian=*/true);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_FALSE(bool(E)) << toString(std::move(E));

  EXPECT_EQ(D.find(Tag_ObjectProtocolVersion)->Scalar, 2u);
  EXPECT_EQ(D.find(Tag_AS0PointerBits)->Scalar, 32u);
  EXPECT_EQ(D.find(Tag_DefaultPlacement)->Scalar, 8u);
  EXPECT_EQ(D.find(Tag_GeneralRegisterSet)->Scalar, 0x0000f3ffu);
  EXPECT_EQ(D.PayloadSize, Bytes.size() - 17);
}

TEST_F(MCS251AttributesTest, LittleEndianEnvelopeRoundTrips) {
  // The envelope u32 lengths follow target byte order; a little-endian target
  // must round-trip too (only the u32 fields swap).
  Writer W;
  W.addU32(Tag_ObjectProtocolVersion, ObjectProtocolVersion);
  std::string BE = W.render(/*IsBigEndian=*/true);
  std::string LE = W.render(/*IsBigEndian=*/false);
  EXPECT_EQ(BE.size(), LE.size());
  EXPECT_NE(BE, LE) << "envelope must differ between byte orders";

  Decoded D;
  Error E = decode(LE, /*IsBigEndian=*/false, D);
  if (E)
    // Still incomplete identity, but envelope must parse: only missing-field
    // complaints are acceptable.
    EXPECT_NE(toString(std::move(E)).find("is missing"), std::string::npos);
}

TEST_F(MCS251AttributesTest, MemoryModelProfileMustAgreeWithTags11And13) {
  Writer W;
  addCompleteIdentity(W);
  // Deliberately inconsistent: patch the profile to (16, InternalMovable)
  // while the scalar tags say (32, InternalExtended).
  for (OutRecord &R : const_cast<std::vector<OutRecord> &>(W.records()))
    if (R.Tag == Tag_MemoryModelProfile) {
      R.Atoms[0].Scalar = AS0PointerBits16;
      R.Atoms[1].Scalar = Placement_InternalMovable;
    }

  std::string Bytes = W.render(true);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("disagrees with as0_pointer_bits(11)"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, RuledValuesAreEnforced) {
  auto buildWith = [](uint32_t Tag, uint32_t Value) {
    Writer W;
    addCompleteIdentity(W);
    // Patch the target tag to a wrong value while keeping ordering valid.
    for (OutRecord &R : const_cast<std::vector<OutRecord> &>(W.records()))
      if (R.Tag == Tag)
        R.Scalar = Value;
    return W.render(true);
  };

  for (auto [Tag, Bad, Expect] :
       std::initializer_list<std::tuple<uint32_t, uint32_t, const char *>>{
           {Tag_ObjectProtocolVersion, 1, "object_protocol_version"},
           {Tag_IntBits, 16, "int_bits"},
           {Tag_LongBits, 16, "long_bits"},
           {Tag_CodePointerBits, 16, "code_pointer_bits"},
           {Tag_GeneralRegisterSet, 0, "general_register_set"},
           {Tag_AS0PointerBits, 24, "as0_pointer_bits"},
           {Tag_DefaultPlacement, 2, "default_placement"},
           // A4-registered values (design §2): every field rejects a
           // candidate/wrong value, old candidate values included.
           {Tag_CallABIMajor, 1, "call_abi_major"},
           {Tag_CallABIMinor, 0, "call_abi_minor"},
           {Tag_RegisterParameterVariant, 2, "register_parameter_variant"},
           {Tag_ASLayoutVersion, 1, "as_layout_version"},
           {Tag_InitProtocolVersion, 1, "init_protocol_version"},
           {Tag_PlacementProtocolVersion, 1, "placement_protocol_version"},
           {Tag_StackContractVersion, 1, "stack_contract_version"},
           {Tag_FunctionContractVersion, 1, "function_contract_version"},
           {Tag_RequiredCapabilitiesLo, 1, "required_capabilities_lo"},
           {Tag_RequiredCapabilitiesHi, 1, "required_capabilities_hi"},
           {Tag_ABIOptions, 1, "abi_options"},
           {Tag_CodeModelProfile, 0, "code_model_profile"},
           {Tag_ObjectProtocolMinor, 1, "object_protocol_minor"}}) {
    Decoded D;
    Error E = decode(buildWith(Tag, Bad), true, D);
    ASSERT_TRUE(bool(E)) << "tag " << Tag << " should reject value " << Bad;
    std::string Msg = toString(std::move(E));
    EXPECT_NE(Msg.find(Expect), std::string::npos) << Msg;
    EXPECT_NE(Msg.find("got"), std::string::npos) << Msg;
  }
}

TEST_F(MCS251AttributesTest, RequiredTagMissingIsRejected) {
  Writer W;
  W.addU32(Tag_IntBits, IntBits);
  W.addU32(Tag_LongBits, LongBits);
  std::string Bytes = W.render(true);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("is missing"), std::string::npos);
}

TEST_F(MCS251AttributesTest, RequiredTagWithoutCriticalIsRejected) {
  std::string Payload;
  encodeULEB128(Tag_ObjectProtocolVersion, Payload);
  Payload.push_back(char(VT_U32)); // Critical cleared
  encodeULEB128(4, Payload);
  Payload.append(std::string("\0\0\0\2", 4));

  std::string Bytes;
  Bytes.push_back(char(FormatVersion));
  auto appendBE = [&](uint32_t V) {
    Bytes.push_back(char((V >> 24) & 0xff));
    Bytes.push_back(char((V >> 16) & 0xff));
    Bytes.push_back(char((V >> 8) & 0xff));
    Bytes.push_back(char(V & 0xff));
  };
  appendBE(16 + uint32_t(Payload.size()));
  Bytes.append("MCS251\0", 7);
  Bytes.push_back(char(ScopeTagFile));
  appendBE(5 + uint32_t(Payload.size()));
  Bytes.append(Payload);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("must be Critical"), std::string::npos);
}

TEST_F(MCS251AttributesTest, ReservedTagNonZeroIsRejected) {
  Writer W;
  W.addU32(Tag_Reserved0, 7, /*Critical=*/false);
  std::string Bytes = W.render(true);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("reserved reserved0(21) must be zero"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, RequiredU32WithWrongLengthIsRejected) {
  // A U32-typed required tag whose Length is 2 rather than 4.  The record is
  // self-contained, so the type rule (not a scope overrun) must reject it.
  Writer W;
  W.addRaw(Tag_ObjectProtocolVersion, VT_U32, /*Critical=*/true,
           std::string("\0\2", 2));
  std::string Bytes = W.render(true);
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  std::string Msg = toString(std::move(E));
  // Record the actual diagnostic in the failure output.
  EXPECT_NE(Msg.find("length"), std::string::npos) << "actual: " << Msg;
}

TEST_F(MCS251AttributesTest, OrderingIsIncreasingOnEmit) {
  // N.6 canonical order is a hard render error (not an assert), so it is
  // observable in every build configuration: render() must abort.  The
  // descending pair (9 then 4) and the equal pair (4 then 4) are both
  // non-canonical.
  {
    Writer W;
    W.addU32(9, 1);
    W.addU32(4, 1);
    EXPECT_DEATH(W.render(true),
                 "strictly increasing tag order.*tag 4 follows tag 9");
  }
  {
    Writer W;
    W.addU32(4, 1);
    W.addU32(4, 2);
    EXPECT_DEATH(W.render(true),
                 "strictly increasing tag order.*tag 4 follows tag 4");
  }
}

TEST_F(MCS251AttributesTest, EncodeULEB128IsShortest) {
  std::string S;
  encodeULEB128(0x7f, S);
  EXPECT_EQ(S.size(), 1u);
  S.clear();
  encodeULEB128(0x80, S);
  EXPECT_EQ(S.size(), 2u);
  S.clear();
  encodeULEB128(0xffffffffu, S);
  EXPECT_EQ(S.size(), 5u);
  S.clear();
  encodeULEB128(0x100000000ull, S);
  EXPECT_EQ(S.size(), 5u); // truncated to 32 bits by callers; shape is 5
}

TEST_F(MCS251AttributesTest, MissingEnvelopeIsRejected) {
  std::string Bytes = fromHex("4100000025");
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("envelope"), std::string::npos);
}

TEST_F(MCS251AttributesTest, TagNameNeverDanglesInDiagnostics) {
  // formatTag/printTag exist so a diagnostic can name a tag without building
  // a `tagName(T).str().c_str()` temporary.  Exercise both the registered and
  // the unallocated path and make sure the rendered message is well formed.
  Writer W;
  W.addU32(400, 1, /*Critical=*/true); // unknown Critical -> rejected by name
  Decoded D;
  Error E = decode(W.render(true), true, D);
  ASSERT_TRUE(bool(E));
  std::string Msg = toString(std::move(E));
  EXPECT_NE(Msg.find("unknown Critical tag 400"), std::string::npos) << Msg;
}

TEST_F(MCS251AttributesTest, ReservedScopeNamespaceTagIsRejected) {
  // DESIGN.md N.4: tags 0..3 belong to the scope namespace, so a record using
  // one is not a skippable unknown extension.  Reject it as optional and as
  // Critical alike.
  for (bool Critical : {false, true}) {
    Writer W;
    W.addU32(2, 0x1234, Critical);
    Decoded D;
    Error E = decode(W.render(true), true, D);
    ASSERT_TRUE(bool(E)) << "Critical=" << Critical;
    std::string Msg = toString(std::move(E));
    EXPECT_NE(Msg.find("reserved scope namespace"), std::string::npos) << Msg;
  }
}

TEST_F(MCS251AttributesTest, MIXAtomWithCriticalBitIsRejected) {
  // An atom type is a plain u8: bit 7 is not a Critical flag and must not be
  // masked away.  Build the payload by hand so the writer cannot repair it.
  std::string AtomPayload;
  AtomPayload.push_back(char(0x81)); // VT_U32 | 0x80
  encodeULEB128(4, AtomPayload);
  AtomPayload.append(std::string("\0\0\0\2", 4));
  AtomPayload.push_back(char(VT_U32));
  encodeULEB128(4, AtomPayload);
  AtomPayload.append(std::string("\0\0\0\10", 4));

  Writer W;
  W.addRaw(Tag_MemoryModelProfile, VT_MIX, /*Critical=*/true, AtomPayload);
  Decoded D;
  Error E = decode(W.render(true), true, D);
  ASSERT_TRUE(bool(E));
  std::string Msg = toString(std::move(E));
  EXPECT_NE(Msg.find("Critical bit"), std::string::npos) << Msg;
}

TEST_F(MCS251AttributesTest, MIXAtomMustBeOneOfTheFirstSliceTypes) {
  // Only U32/UTF8/BYTES are allowed in the first slice; MIX may not nest.
  std::string AtomPayload;
  AtomPayload.push_back(char(VT_MIX)); // nested MIX
  encodeULEB128(0, AtomPayload);

  Writer W;
  W.addRaw(Tag_MemoryModelProfile, VT_MIX, /*Critical=*/true, AtomPayload);
  Decoded D;
  Error E = decode(W.render(true), true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("must be U32"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, DiagnosticsCarrySpecificValues) {
  // The streamed diagnostics must include the offending values, not just a
  // generic sentence.  A complete identity is needed so the value check (which
  // runs after the required-field check) is actually reached.
  Writer W;
  addCompleteIdentity(W);
  // Wrong value: object_protocol_version must be 2.
  for (OutRecord &R : const_cast<std::vector<OutRecord> &>(W.records()))
    if (R.Tag == Tag_ObjectProtocolVersion)
      R.Scalar = 1;

  Decoded D;
  Error E = decode(W.render(true), true, D);
  ASSERT_TRUE(bool(E));
  std::string Msg = toString(std::move(E));
  EXPECT_NE(Msg.find("object_protocol_version must be 2"), std::string::npos)
      << Msg;
  EXPECT_NE(Msg.find("got 1"), std::string::npos) << Msg;
}

TEST_F(MCS251AttributesTest, HexRoundTripHelperIsConsistent) {
  std::string Bytes = fromHex(FixtureHex);
  // toHex emits lowercase; compare case-insensitively.
  std::string Lower(FixtureHex);
  for (char &C : Lower)
    C = char(std::tolower(static_cast<unsigned char>(C)));
  EXPECT_EQ(toHex(Bytes), Lower);
}

TEST_F(MCS251AttributesTest, EveryRequiredTagIndependentlyRequired) {
  // Remove exactly one required tag from an otherwise complete identity:
  // each removal must be rejected, and the diagnostic must name the missing
  // field (not just any "is missing" complaint).
  for (uint32_t Missing : RequiredTags) {
    Writer W;
    addCompleteIdentity(W, Missing);
    Decoded D;
    Error E = decode(W.render(true), true, D);
    ASSERT_TRUE(bool(E)) << "missing tag " << Missing << " should be rejected";
    std::string Msg = toString(std::move(E));
    EXPECT_NE(Msg.find("is missing"), std::string::npos) << Msg;
    EXPECT_NE(Msg.find(tagName(Missing).str()), std::string::npos)
        << "the diagnostic must name " << tagName(Missing).str() << ": "
        << Msg;
  }
}

TEST_F(MCS251AttributesTest, DuplicateUnknownTagIsRejected) {
  // N.6 duplicate rejection covers unknown tags too, not just registered
  // ones.  Two tag-256 records: hand-build the payload so the writer's
  // canonical ordering cannot hide the duplicate.
  std::string Payload;
  for (int I = 0; I != 2; ++I) {
    encodeULEB128(256, Payload);
    Payload.push_back(char(VT_UTF8));
    encodeULEB128(3, Payload);
    Payload.append("abc", 3);
  }
  std::string Bytes;
  Bytes.push_back(char(FormatVersion));
  auto appendBE = [&](uint32_t V) {
    Bytes.push_back(char((V >> 24) & 0xff));
    Bytes.push_back(char((V >> 16) & 0xff));
    Bytes.push_back(char((V >> 8) & 0xff));
    Bytes.push_back(char(V & 0xff));
  };
  appendBE(16 + uint32_t(Payload.size()));
  Bytes.append("MCS251\0", 7);
  Bytes.push_back(char(ScopeTagFile));
  appendBE(5 + uint32_t(Payload.size()));
  Bytes.append(Payload);

  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("duplicate tag 256"),
            std::string::npos);
}

TEST_F(MCS251AttributesTest, MemoryModelProfilePairMustBeFrozen) {
  // The (as0_pointer_bits, default_placement) pair must be one of the five
  // frozen profiles.  (16, ExternalData) passes both individual field
  // domains -- the pair itself is not a profile and must be rejected even
  // when the MIX copy agrees with the scalar tags.
  auto buildPair = [](uint32_t AS0, uint32_t Placement) {
    Writer W;
    addCompleteIdentity(W);
    for (OutRecord &R : const_cast<std::vector<OutRecord> &>(W.records())) {
      if (R.Tag == Tag_AS0PointerBits)
        R.Scalar = AS0;
      if (R.Tag == Tag_DefaultPlacement)
        R.Scalar = Placement;
      if (R.Tag == Tag_MemoryModelProfile) {
        R.Atoms[0].Scalar = AS0;
        R.Atoms[1].Scalar = Placement;
      }
    }
    return W.render(true);
  };

  // The five frozen pairs all decode.
  for (const MemoryModelProfile &P :
       {MemoryModelProfile_Tiny, MemoryModelProfile_XTiny,
        MemoryModelProfile_Small, MemoryModelProfile_XSmall,
        MemoryModelProfile_Large}) {
    Decoded D;
    Error E = decode(buildPair(P.AS0PointerBits, P.Placement), true, D);
    ASSERT_FALSE(bool(E))
        << "frozen pair (" << P.AS0PointerBits << "," << P.Placement
        << ") must decode: " << toString(std::move(E));
  }

  // (16, ExternalData): both fields individually ruled, pair not frozen.
  Decoded D;
  Error E = decode(buildPair(AS0PointerBits16, Placement_ExternalData), true,
                   D);
  ASSERT_TRUE(bool(E));
  std::string Msg = toString(std::move(E));
  EXPECT_NE(Msg.find("not one of the five frozen memory model profiles"),
            std::string::npos)
      << "actual: " << Msg;
}

TEST_F(MCS251AttributesTest, CompleteIdentityEmittedBytesAreFrozen) {
  // Byte-for-byte comparison of the rendered complete identity against an
  // independently spelled-out hex string (envelope + every record).  This
  // pins the exact emitted bytes, not just a decode round-trip:
  //   envelope: 41 | VendorSize=16+P | "MCS251\0" | 01 | ScopeSize=5+P
  //   P = 20 U32 records (7 bytes each) + the MIX record (3 + 12 bytes)
  //     = 155, so VendorSize = 171 (0xAB) and ScopeSize = 160 (0xA0).
  // The values are the A4-registered set (PM ruling 2026-09-13): call ABI
  // 2/1, register parameter variant 3, AS layout 2, placement 8, the four
  // subprotocols 2, capabilities 0/0, abi_options 0, code model 1, object
  // protocol minor 0.
  Writer W;
  addCompleteIdentity(W);
  std::string Bytes = W.render(/*IsBigEndian=*/true);
  EXPECT_EQ(Bytes.size(), 172u) << "17-byte envelope + P=155";

  static const char *ExpectedHex =
      // clang-format off
      // Envelope (big-endian u32 lengths).
      "41" "000000AB" "4D435332353100" "01" "000000A0"
      // Twenty U32 records, tag order 4..20.
      "04810400000002" "05810400000002" "06810400000001"
      "07810400000003" "0881040000F3FF" "09810400000020"
      "0A810400000020" "0B810400000020" "0C810400000002"
      "0D810400000008" "0E810400000002" "0F810400000002"
      "10810400000002" "11810400000002" "12810400000000"
      "13810400000000" "14810400000000"
      // Tag 24 MIX (Critical): two U32 atoms (32, 8); each atom is type(1)
      // + length(1) + value(4) = 6 bytes, so the record length is 12.
      "18830C" "01" "04" "00000020" "01" "04" "00000008"
      // Remaining U32 records, tag order 25..27.
      "19810400000001" "1A810400000020" "1B810400000000";
      // clang-format on
  // toHex emits lowercase; compare case-insensitively.
  std::string Lower(ExpectedHex);
  for (char &C : Lower)
    C = char(std::tolower(static_cast<unsigned char>(C)));
  EXPECT_EQ(toHex(Bytes), Lower);

  // The frozen bytes must still decode.
  Decoded D;
  Error E = decode(Bytes, true, D);
  ASSERT_FALSE(bool(E)) << toString(std::move(E));
}

TEST_F(MCS251AttributesTest, RegisteredIdentityMatchesFrozenBytes) {
  // renderRegisteredIdentity() is the production payload source; its XSmall
  // output must be byte-for-byte the complete identity above, and Small must
  // differ from it in exactly the two placement fields (Tag 13 and the
  // memory_model_profile placement atom).
  std::string XSmall = renderRegisteredIdentity(
      MemoryModelProfile_XSmall.AS0PointerBits, MemoryModelProfile_XSmall.Placement);
  Writer W;
  addCompleteIdentity(W);
  EXPECT_EQ(XSmall, W.render(/*IsBigEndian=*/true));

  std::string Small = renderRegisteredIdentity(
      MemoryModelProfile_Small.AS0PointerBits, MemoryModelProfile_Small.Placement);
  EXPECT_EQ(Small.size(), XSmall.size());
  // The only differences are Tag 13 (default_placement, the 10th record:
  // tag 4 is at payload offset 0, each U32 record is 7 bytes) and the
  // memory_model_profile placement atom (the last 4 bytes of the MIX value,
  // which is the payload's final 4 bytes before tags 25..27).
  size_t Tag13Offset = 17 + 9 * 7;
  EXPECT_EQ(toHex(StringRef(XSmall).substr(Tag13Offset, 7)),
            "0d810400000008");
  EXPECT_EQ(toHex(StringRef(Small).substr(Tag13Offset, 7)),
            "0d810400000001");
  // MIX placement atom: the MIX record (tag 24) follows the 17 U32 records
  // of tags 4..20; its header is 3 bytes, atom 1 is 6 bytes (type, length,
  // BE32 value), so atom 2's 4-byte value starts at MixOffset + 3 + 6 + 2.
  size_t MixOffset = 17 + 17 * 7;
  EXPECT_EQ(toHex(StringRef(XSmall).substr(MixOffset + 3 + 6 + 2, 4)),
            "00000008");
  EXPECT_EQ(toHex(StringRef(Small).substr(MixOffset + 3 + 6 + 2, 4)),
            "00000001");
  // Every byte outside those two fields is identical.
  std::string A = XSmall, B = Small;
  for (size_t I = 0; I < A.size(); ++I) {
    bool InTag13 = I >= Tag13Offset && I < Tag13Offset + 7;
    bool InMixPlacement =
        I >= MixOffset + 3 + 6 + 2 && I < MixOffset + 3 + 6 + 2 + 4;
    if (!InTag13 && !InMixPlacement)
      EXPECT_EQ(A[I], B[I]) << "byte " << I << " must be identical";
  }
  // Both decode.
  for (const std::string *Identity : {&XSmall, &Small}) {
    Decoded D;
    Error E = decode(*Identity, true, D);
    ASSERT_FALSE(bool(E)) << toString(std::move(E));
  }
}

TEST_F(MCS251AttributesTest, RegisteredIdentityRejectsUnregisteredProfiles) {
  // Only XSmall (32,8) and Small (32,1) are registered for A4 emission.
  // Tiny, XTiny and Large are structurally decodable profiles but must not
  // be renderable as production identities.
  for (const MemoryModelProfile &P :
       {MemoryModelProfile_Tiny, MemoryModelProfile_XTiny,
        MemoryModelProfile_Large})
    EXPECT_DEATH(
        renderRegisteredIdentity(P.AS0PointerBits, P.Placement),
        "is not registered for A4 v2 object emission");
  EXPECT_DEATH(renderRegisteredIdentity(32, 3),
               "is not registered for A4 v2 object emission");
}

TEST_F(MCS251AttributesTest, RegisteredValuesMatchRulingTable) {
  // Pin the A4 ruling values (design §2, PM 2026-09-13) as named constants.
  EXPECT_EQ(CallABIMajor, 2u);
  EXPECT_EQ(CallABIMinor, 1u);
  EXPECT_EQ(RegisterParameterVariant, 3u);
  EXPECT_EQ(ASLayoutVersion, 2u);
  EXPECT_EQ(InitProtocolVersion, 2u);
  EXPECT_EQ(PlacementProtocolVersion, 2u);
  EXPECT_EQ(StackContractVersion, 2u);
  EXPECT_EQ(FunctionContractVersion, 2u);
  EXPECT_EQ(RequiredCapabilitiesLo, 0u);
  EXPECT_EQ(RequiredCapabilitiesHi, 0u);
  EXPECT_EQ(ABIOptions, 0u);
  EXPECT_EQ(CodeModelProfile, 1u);
  EXPECT_EQ(ObjectProtocolMinor, 0u);
  EXPECT_TRUE(isRegisteredA4Profile(32, 8));
  EXPECT_TRUE(isRegisteredA4Profile(32, 1));
  EXPECT_FALSE(isRegisteredA4Profile(16, 1));
  EXPECT_FALSE(isRegisteredA4Profile(16, 8));
  EXPECT_FALSE(isRegisteredA4Profile(32, 3));
}

TEST_F(MCS251AttributesTest, WriterRejectsInvalidMIXAtomTypes) {
  // The writer is the encoder of record: it must refuse to serialize a MIX
  // record whose atom type is not exactly U32/UTF8/BYTES without a Critical
  // bit -- an unallocated code, a nested MIX and a Critical-bit value are
  // all hard render errors (mirroring the strict reader's atom rules).
  {
    Writer W;
    W.addMix(300, {OutRecord{0, false, /*ValueType=*/0x05, 0, "x", {}}});
    EXPECT_DEATH(W.render(true), "invalid MIX atom type");
  }
  {
    Writer W;
    W.addMix(300, {OutRecord{0, false, VT_MIX, 0, "", {}}});
    EXPECT_DEATH(W.render(true), "invalid MIX atom type");
  }
  {
    Writer W;
    W.addMix(300,
             {OutRecord{0, false, uint8_t(VT_U32 | CriticalMask), 0, "", {}}});
    EXPECT_DEATH(W.render(true), "invalid MIX atom type");
  }
  // The three legal atom types render without complaint.
  {
    Writer W;
    W.addMix(300, {OutRecord{0, false, VT_U32, 1, "", {}},
                   OutRecord{0, false, VT_UTF8, 0, "ab", {}},
                   OutRecord{0, false, VT_BYTES, 0, "\x01", {}}},
             /*Critical=*/false);
    std::string Bytes = W.render(true);
    Decoded D;
    Error E = decode(Bytes, true, D);
    if (E) {
      std::string Msg = toString(std::move(E));
      EXPECT_NE(Msg.find("is missing"), std::string::npos)
          << "actual: " << Msg; // incomplete identity, envelope must parse
    }
  }
}

} // end anonymous namespace
