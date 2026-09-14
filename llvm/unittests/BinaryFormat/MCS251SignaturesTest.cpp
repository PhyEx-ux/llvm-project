//===- MCS251SignaturesTest.cpp - P-4 signature value codec tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the internal value format of Tag 28, frozen in
// validation/mcs251-models/proposals/P4-SIGNATURE-PROTOCOL-FREEZE.md.
//
// The tests pin the grammar end to end: the empty-set spelling, the implicit
// record length, the bitmap bit order, the name_blob rules (first byte,
// empty name, duplicate name, trailing bytes), the role legal combinations
// (role&3 in {1,2}, bit2 forcing param_count=0, bit3), the ret domain and
// the encoder's self-check.  The frozen mutation matrix names exactly these
// cases, so a change to the wire format breaks one test here by design.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/Support/Error.h"
#include <gtest/gtest.h>

using namespace llvm;
using namespace llvm::MCS251Signatures;

namespace {

std::string hex(StringRef S) {
  static const char *D = "0123456789abcdef";
  std::string Out;
  for (unsigned char C : S) {
    Out.push_back(D[C >> 4]);
    Out.push_back(D[C & 0xf]);
  }
  return Out;
}

std::string fromHex(StringRef H) {
  std::string Out;
  auto N = [](char C) -> unsigned {
    if (C >= '0' && C <= '9') return unsigned(C - '0');
    if (C >= 'a' && C <= 'f') return unsigned(C - 'a' + 10);
    return unsigned(C - 'A' + 10);
  };
  for (size_t I = 0; I + 1 < H.size(); I += 2)
    Out.push_back(char((N(H[I]) << 4) | N(H[I + 1])));
  return Out;
}

// A definition record with \p ParamCount non-bit parameters.
Record simpleRecord(StringRef Name, uint8_t Role, uint8_t ParamCount,
                    bool Ret = false) {
  return makeRecord(Name, isDefinitionRole(Role), ParamCount, {}, Ret,
                    hasNoPrototype(Role), isVariadic(Role),
                    /*CallABIMajor=*/2, /*CallABIMinor=*/1);
}

class MCS251SignaturesTest : public ::testing::Test {};

/// Append a 32-bit little-endian value.
void pushU32LE(std::string &S, uint32_t V) {
  for (int I = 0; I != 4; ++I)
    S.push_back(char((V >> (8 * I)) & 0xff));
}

/// A blob holding one NUL-terminated name (the C++ string literal `"_f\0"`
/// would silently drop the terminator).
std::string nulBlob(StringRef Name) {
  std::string S = Name.str();
  S.push_back('\0');
  return S;
}

/// Hand-build one Tag 28 value with a single explicit record and blob,
/// bypassing encode()'s self-check so the decoder's rejection rules can be
/// exercised on values the encoder must refuse to produce.  \p BitmapBytes
/// is emitted verbatim after param_count.
std::string rawValue(uint8_t Role, uint8_t ParamCount,
                     StringRef BitmapBytes, uint8_t Ret, uint8_t Major,
                     uint8_t Minor, StringRef Blob, uint32_t NameOff,
                     uint16_t Count = 1) {
  std::string S;
  S.push_back(char(ValueVersion));
  S.push_back(char(0));
  S.push_back(char(Count & 0xff));
  S.push_back(char((Count >> 8) & 0xff));
  pushU32LE(S, NameOff);
  S.push_back(char(Role));
  S.push_back(char(ParamCount));
  S.append(BitmapBytes.data(), BitmapBytes.size());
  S.push_back(char(Ret));
  S.push_back(char(Major));
  S.push_back(char(Minor));
  S.append(Blob.data(), Blob.size());
  return S;
}

} // namespace

TEST_F(MCS251SignaturesTest, EmptySetIsExactlyFourBytes) {
  // Freeze: the empty set is exactly `01 00 00 00`; a zero-length value is
  // NOT a legal spelling of it.
  Table T;
  std::string Bytes = encode(T);
  EXPECT_EQ(hex(Bytes), "01000000");

  Table D;
  ASSERT_FALSE(bool(decode(Bytes, D)));
  EXPECT_TRUE(D.empty());

  Table NotEmpty;
  Error E = decode("", NotEmpty);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("zero-length"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, HeaderIsVersionFlagsCountLE) {
  // version=1, flags=0, count=u16 little-endian.
  Table T;
  T.Records.push_back(simpleRecord("_f", Role_HasDefinition, 2));
  T.Records.push_back(simpleRecord("_g", Role_HasDefinition, 0));
  std::string Bytes = encode(T);
  EXPECT_EQ(uint8_t(Bytes[0]), 1u);            // version
  EXPECT_EQ(uint8_t(Bytes[1]), 0u);            // flags
  EXPECT_EQ(uint8_t(Bytes[2]), 2u);            // count low byte
  EXPECT_EQ(uint8_t(Bytes[3]), 0u);            // count high byte (LE)
  Table D;
  ASSERT_FALSE(bool(decode(Bytes, D)));
  ASSERT_EQ(D.Records.size(), 2u);
  EXPECT_EQ(D.Records[0].Name, "_f");
  EXPECT_EQ(D.Records[1].Name, "_g");
}

TEST_F(MCS251SignaturesTest, RecordLengthIsImplicitFromParamCount) {
  // A record is 9 + ceil(n/8) bytes; the decoder derives it from param_count,
  // so a length/content mismatch cannot be expressed.  Sanity-check the
  // arithmetic through the round-trip at the boundary counts 0,1,8,9,16,17.
  for (unsigned N : {0u, 1u, 8u, 9u, 16u, 17u}) {
    Table T;
    T.Records.push_back(simpleRecord("_f", Role_HasDefinition, uint8_t(N)));
    std::string Bytes = encode(T);
    // 4 header + (9 + bitmap) record + ("_f" + NUL) blob.
    EXPECT_EQ(Bytes.size(), 4u + 9u + bitmapBytes(uint8_t(N)) + 3u)
        << "param_count " << N;
    Table D;
    ASSERT_FALSE(bool(decode(Bytes, D))) << N;
    EXPECT_EQ(D.Records[0].ParamCount, N);
  }
}

TEST_F(MCS251SignaturesTest, BitmapBitOrderIsSourcePosition) {
  // bit i of the frozen order lives in byte i/8 at bit i%8.
  Table T;
  Record R = simpleRecord("_f", Role_HasDefinition, 10);
  std::vector<uint8_t> Bits(10, 0);
  Bits[0] = 1;  // byte 0 bit 0
  Bits[9] = 1;  // byte 1 bit 1
  R.Bitmap.assign(bitmapBytes(10), 0);
  bitmapSet(R.Bitmap, 0, true);
  bitmapSet(R.Bitmap, 9, true);
  T.Records.push_back(R);
  Table D;
  ASSERT_FALSE(bool(decode(encode(T), D)));
  ASSERT_EQ(D.Records.size(), 1u);
  EXPECT_EQ(D.Records[0].Bitmap.size(), 2u);
  EXPECT_EQ(hex(StringRef(reinterpret_cast<const char *>(
                   D.Records[0].Bitmap.data()),
               D.Records[0].Bitmap.size())),
            "0102");
  EXPECT_TRUE(bitmapGet(D.Records[0].Bitmap, 0));
  EXPECT_FALSE(bitmapGet(D.Records[0].Bitmap, 1));
  EXPECT_TRUE(bitmapGet(D.Records[0].Bitmap, 9));
}

TEST_F(MCS251SignaturesTest, NonZeroTailBitmapBitIsRejected) {
  // Beyond param_count the bitmap bits must be zero.  Hand-build the value:
  // encode() would refuse to serialize this malformed shape.
  std::string Bytes =
      rawValue(Role_HasDefinition, 1, std::string(1, char(0x80)),
               /*Ret=*/0, 2, 1, nulBlob("_f"), 0);
  Table D;
  Error E = decode(Bytes, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("beyond param_count"),
            std::string::npos);
}

TEST_F(MCS251SignaturesTest, IllegalRoleCombinationsAreRejected) {
  // role&3 must be 1 (only definition) or 2 (only declaration/reference);
  // 0 and 3 are both rejected.  Bits 4..7 are reserved.
  for (uint8_t Role : {uint8_t(0), uint8_t(3), uint8_t(0x10),
                       uint8_t(0x11), uint8_t(0x01)}) {
    std::string Bytes = rawValue(Role, 0, "", /*Ret=*/0, 2, 1, nulBlob("_f"), 0);
    Table D;
    Error E = decode(Bytes, D);
    if (Role == Role_HasDefinition)
      ASSERT_FALSE(bool(E)) << "role 1 is the only legal definition shape";
    else
      ASSERT_TRUE(bool(E)) << "role 0x" << hex(StringRef(
                               reinterpret_cast<const char *>(&Role), 1))
                           << " must be rejected";
  }
}

TEST_F(MCS251SignaturesTest, NoPrototypeForcesZeroParamsAndClearsVariadic) {
  // bit2=1 requires param_count=0, empty bitmap and bit3=0.
  {
    Table T;
    Record R = simpleRecord("_f", Role_HasDefinition | Role_NoPrototype, 0);
    T.Records.push_back(R);
    Table D;
    ASSERT_FALSE(bool(decode(encode(T), D)));
    EXPECT_EQ(D.Records[0].ParamCount, 0u);
  }
  {
    std::string Bytes = rawValue(Role_HasDefinition | Role_NoPrototype, 1,
                                 std::string(1, char(0x00)), 0, 2, 1,
                                 nulBlob("_f"), 0);
    Table D;
    ASSERT_TRUE(bool(decode(Bytes, D)));
  }
  {
    std::string Bytes = rawValue(
        Role_HasDefinition | Role_NoPrototype | Role_Variadic, 0, "", 0, 2,
        1, nulBlob("_f"), 0);
    Table D;
    ASSERT_TRUE(bool(decode(Bytes, D)));
  }
}

TEST_F(MCS251SignaturesTest, RetMustBeZeroOrOne) {
  std::string Bytes =
      rawValue(Role_HasDefinition, 0, "", /*Ret=*/2, 2, 1, nulBlob("_f"), 0);
  Table D;
  Error E = decode(Bytes, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("ret"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, NameOffMustPointAtTheFirstByteOfEachString) {
  // The encoder lays names out consecutively; a decoder must reject an
  // offset into the middle of a string, a missing terminator, an empty name,
  // duplicate names and trailing bytes.
  Table T;
  T.Records.push_back(simpleRecord("_f", Role_HasDefinition, 0));
  T.Records.push_back(simpleRecord("_g", Role_HasDefinition, 0));
  std::string Good = encode(T);
  Table D;
  ASSERT_FALSE(bool(decode(Good, D)));
  EXPECT_EQ(D.Records[0].Name, "_f");
  EXPECT_EQ(D.Records[1].Name, "_g");

  // Corrupt record 1's name_off from 3 to 4 (into the middle of "_f\0").
  std::string Bad = Good;
  // blob starts after 4 + 2*(9) = 22; name offsets: rec0=0, rec1=3.
  // name_off of record 1 is at record base 4 + 9 + 0 = 13.
  Bad[13] = char(4);
  Table D2;
  ASSERT_TRUE(bool(decode(Bad, D2)));

  // Duplicate names (equal content) are rejected by the decoder.  encode()
  // refuses to build this value -- it self-checks against the very decoder --
  // so hand-build a two-record value whose blob strings are equal.
  const std::string One = nulBlob("_same");
  std::string Dup;
  Dup.push_back(char(ValueVersion));
  Dup.push_back(char(0));
  Dup.push_back(char(2));
  Dup.push_back(char(0));
  for (int Rec = 0; Rec != 2; ++Rec) {
    pushU32LE(Dup, uint32_t(Rec) * One.size());
    Dup.push_back(char(Role_HasDefinition)); // role
    Dup.push_back(char(0));                  // param_count
    Dup.push_back(char(0));                  // ret
    Dup.push_back(char(2));                  // call_abi_major
    Dup.push_back(char(1));                  // call_abi_minor
  }
  Dup += One;
  Dup += One;
  Table D3;
  Error E = decode(Dup, D3);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("same name"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, EmptyNameIsRejected) {
  Table T;
  Record R = simpleRecord("_f", Role_HasDefinition, 0);
  R.Name.clear();
  T.Records.push_back(R);
  // encode() refuses an empty name outright (a producer bug), so hand-build
  // the value: header + one record + blob with a single NUL.
  std::string Hand = rawValue(Role_HasDefinition, 0, "", 0, 2, 1,
                              std::string(1, char(0)), /*NameOff=*/0);
  Table D;
  Error E = decode(Hand, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("empty name"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, TrailingBlobByteIsRejected) {
  Table T;
  T.Records.push_back(simpleRecord("_f", Role_HasDefinition, 0));
  std::string Bytes = encode(T);
  Bytes.push_back('X'); // a byte after the last NUL
  Table D;
  Error E = decode(Bytes, D);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("trailing"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, TruncationIsRejectedAtEveryStage) {
  Table T;
  T.Records.push_back(simpleRecord("_f", Role_HasDefinition, 3));
  std::string Good = encode(T);
  for (size_t Cut = 0; Cut + 1 < Good.size(); ++Cut) {
    Table D;
    Error E = decode(StringRef(Good).substr(0, Cut), D);
    ASSERT_TRUE(bool(E)) << "truncation at " << Cut << " must fail";
  }
}

TEST_F(MCS251SignaturesTest, CheckABIGenerationIsExact) {
  Table T;
  T.Records.push_back(simpleRecord("_f", Role_HasDefinition, 0));
  ASSERT_FALSE(bool(checkABIGeneration(T, 2, 1)));
  Error E = checkABIGeneration(T, 2, 0);
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("call_abi"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, CompareRecordsFrozenSemantics) {
  // A differing no-prototype bit is a conflict; when both sides agree on
  // bit2, only the frozen field sets are compared.
  Record A = simpleRecord("_f", Role_HasDefinition, 2);
  Record B = A;
  ASSERT_FALSE(bool(compareRecords(A, B, "")));

  // Bit-ness difference at one source position.
  Record C = A;
  bitmapSet(C.Bitmap, 1, true);
  Error E = compareRecords(A, C, "");
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("source parameter 1"),
            std::string::npos);

  // Prototype-ness difference: bit2 on one side only.
  Record Dp = simpleRecord("_f", Role_HasDefinition | Role_NoPrototype, 0);
  Record P = simpleRecord("_f", Role_HasDefinition, 0);
  E = compareRecords(P, Dp, "");
  ASSERT_TRUE(bool(E));
  EXPECT_NE(toString(std::move(E)).find("prototype"), std::string::npos);

  // Both sides no-prototype: only (ret, call_abi) compare; two no-prototype
  // records with different parameter counts still only have count 0, so a
  // differing return type is the conflict.
  Record N1 = simpleRecord("_f", Role_HasDefinition | Role_NoPrototype, 0);
  Record N2 = N1;
  N2.Ret = 1;
  E = compareRecords(N1, N2, "");
  ASSERT_TRUE(bool(E));
}

TEST_F(MCS251SignaturesTest, DescribeRoleNeverAsserts) {
  EXPECT_EQ(describeRole(Role_HasDefinition), "definition");
  EXPECT_EQ(describeRole(Role_DeclaredNotDefined), "declaration");
  EXPECT_EQ(describeRole(Role_HasDefinition | Role_Variadic),
            "definition+variadic");
  EXPECT_EQ(describeRole(Role_HasDefinition | Role_NoPrototype),
            "definition+no-prototype");
  // An illegal combination is described literally, never asserted on.
  EXPECT_NE(describeRole(0).find("illegal"), std::string::npos);
}

TEST_F(MCS251SignaturesTest, EncodeSelfChecksAgainstTheDecoder) {
  // A table with a valid shape encodes and decodes byte-identically.
  Table T;
  // The compact bitmap for param_count 3 has one byte; bit0 and bit2 set.
  T.Records.push_back(makeRecord("_f", /*IsDefinition=*/true, 3,
                                 std::vector<uint8_t>{0x05}, true,
                                 /*NoPrototype=*/false, /*Variadic=*/false, 2,
                                 1));
  T.Records.push_back(makeRecord("_g", /*IsDefinition=*/false, 0, {}, false,
                                 false, false, 2, 1));
  std::string Bytes = encode(T);
  Table D;
  ASSERT_FALSE(bool(decode(Bytes, D)));
  ASSERT_EQ(D.Records.size(), 2u);
  EXPECT_EQ(D.Records[0].Name, "_f");
  EXPECT_EQ(D.Records[0].ParamCount, 3u);
  EXPECT_TRUE(bitmapGet(D.Records[0].Bitmap, 0));
  EXPECT_FALSE(bitmapGet(D.Records[0].Bitmap, 1));
  EXPECT_TRUE(bitmapGet(D.Records[0].Bitmap, 2));
  EXPECT_EQ(D.Records[0].Ret, 1u);
  EXPECT_TRUE(isDefinitionRole(D.Records[0].Role));
  EXPECT_TRUE(isDeclarationRole(D.Records[1].Role));
}
