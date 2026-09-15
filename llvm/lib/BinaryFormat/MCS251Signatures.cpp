//===- MCS251Signatures.cpp - P-4 signature value codec -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encoder and strict decoder for the internal value of Tag 28
// (MCS251_TAG_FUNCTION_SIGNATURES).  The byte layout, the legal role-bit
// combinations and the rejection set are frozen in
// validation/mcs251-models/proposals/P4-SIGNATURE-PROTOCOL-FREEZE.md; see
// llvm/BinaryFormat/MCS251Signatures.h for the grammar.
//
// The reader is total and total-failing: every malformed input produces a
// diagnostic and no partially-populated result.  It never guesses a value and
// never repairs a length/content disagreement, because the record length is
// implicit and therefore cannot carry one.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::MCS251Signatures;

namespace {

/// The single diagnostic constructor, mirroring the attributes codec so both
/// protocols read the same way.
template <typename Fn> llvm::Error makeError(Fn &&Build) {
  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "MCS251 signatures: ";
  Build(OS);
  OS.flush();
  return createStringError(std::errc::invalid_argument, "%s", Msg.c_str());
}

uint8_t readU8(StringRef B, size_t Off) { return uint8_t(B[Off]); }

uint16_t readU16LE(StringRef B, size_t Off) {
  return uint16_t(uint8_t(B[Off])) | (uint16_t(uint8_t(B[Off + 1])) << 8);
}

uint32_t readU32LE(StringRef B, size_t Off) {
  return uint32_t(uint8_t(B[Off])) | (uint32_t(uint8_t(B[Off + 1])) << 8) |
         (uint32_t(uint8_t(B[Off + 2])) << 16) |
         (uint32_t(uint8_t(B[Off + 3])) << 24);
}

} // end anonymous namespace

llvm::Error MCS251Signatures::decode(StringRef Value, Table &Out) {
  Out = Table();

  // Freeze: the empty set is exactly `01 00 00 00`; a zero-length value is
  // NOT a legal spelling of it.  The header is unconditional.
  if (Value.empty())
    return makeError([](raw_ostream &OS) {
      OS << "a zero-length value is not a legal spelling of the empty "
            "signature set (use 01 00 00 00)";
    });
  if (Value.size() < 4)
    return makeError([&](raw_ostream &OS) {
      OS << "value is shorter than the 4-byte header (" << Value.size()
         << " bytes)";
    });

  uint8_t Version = readU8(Value, 0);
  if (Version != ValueVersion)
    return makeError([&](raw_ostream &OS) {
      OS << "value version must be " << unsigned(ValueVersion) << ", got "
         << unsigned(Version);
    });
  uint8_t Flags = readU8(Value, 1);
  if (Flags != 0)
    return makeError([&](raw_ostream &OS) {
      OS << "reserved flags must be 0, got " << unsigned(Flags);
    });
  uint16_t Count = readU16LE(Value, 2);

  size_t Pos = 4;
  for (uint16_t I = 0; I != Count; ++I) {
    if (Value.size() - Pos < 9)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " is truncated before its fixed fields";
      });
    Record R;
    R.NameOffset = readU32LE(Value, Pos);
    R.Role = readU8(Value, Pos + 4);
    R.ParamCount = readU8(Value, Pos + 5);
    Pos += 6;
    // The bitmap length is implicit: ceil(param_count/8).  The fixed fields
    // after it are ret, call_abi_major, call_abi_minor.
    size_t BitmapLen = bitmapBytes(R.ParamCount);
    if (Value.size() - Pos < BitmapLen + 3)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " is truncated in its bitmap or tail";
      });
    R.Bitmap.assign(Value.begin() + Pos, Value.begin() + Pos + BitmapLen);
    Pos += BitmapLen;
    R.Ret = readU8(Value, Pos);
    R.CallABIMajor = readU8(Value, Pos + 1);
    R.CallABIMinor = readU8(Value, Pos + 2);
    Pos += 3;

    // role bits 4..7 are reserved.
    if (R.Role & Role_ReservedMask)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " sets reserved role bits "
           << format_hex(R.Role & Role_ReservedMask, 4);
      });
    // role&3 must be exactly 1 (only defined) or 2 (only declared).
    uint8_t Def = R.Role & Role_DefinitionMask;
    if (Def != Role_HasDefinition && Def != Role_DeclaredNotDefined)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " has illegal role combination "
           << format_hex(R.Role & Role_DefinitionMask, 4)
           << " (role&3 must be 1 = definition or 2 = declaration)";
      });

    // bit2 no-prototype: bit3 clear, and the bitmap all zero.  The revision
    // of the zero-parameter protocol (PM 2026-09-15, after review) lets a K&R
    // record carry its real parameter list -- a K&R definition like
    // `int f(x) int x;` is written (bit2=1, param_count=1) so a prototyped
    // counterpart `int f(void)` is refused instead of silently matching.
    // `__bit` parameters remain impossible on a K&R definition (Sema N14), so
    // every bitmap bit of a bit2=1 record must still be zero.
    if (R.Role & Role_NoPrototype) {
      if (R.Role & Role_Variadic)
        return makeError([&](raw_ostream &OS) {
          // The diagnostic sentence is kept verbatim identical to the llc
          // side (MCS251AsmPrinter metadata check) so both validation paths
          // report the same wording.
          OS << "record " << I
             << ": a no-prototype record cannot also be variadic";
        });
      for (unsigned Bit = 0; Bit != R.ParamCount; ++Bit)
        if (R.Bitmap[Bit / 8] & (1u << (Bit % 8)))
          return makeError([&](raw_ostream &OS) {
            // Verbatim identical to the llc side (MCS251AsmPrinter).
            OS << "record " << I << ": a no-prototype record cannot set a "
               << "__bit bit-ness for source parameter " << Bit
               << " (K&R parameters cannot be __bit)";
          });
    }

    // Bitmap tail bits beyond param_count must be zero.
    for (unsigned Bit = R.ParamCount; Bit != BitmapLen * 8; ++Bit)
      if (R.Bitmap[Bit / 8] & (1u << (Bit % 8)))
        return makeError([&](raw_ostream &OS) {
          OS << "record " << I << " has a set bitmap bit " << Bit
             << " beyond param_count " << unsigned(R.ParamCount);
        });

    if (R.Ret > 1)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " has ret " << unsigned(R.Ret)
           << " (must be 0 or 1)";
      });

    Out.Records.push_back(std::move(R));
  }

  // --- blob -------------------------------------------------------------
  // The blob begins exactly after the last record and holds `Count`
  // NUL-terminated names, concatenated in record order.  Enforcing the
  // sequential layout in one place rejects every name_off rule at once:
  // offset past the end, offset into the middle of a string, an empty name,
  // a missing terminator, and trailing bytes after the last name.
  StringRef Blob = Value.substr(Pos);
  size_t Cursor = 0;
  for (uint16_t I = 0; I != Count; ++I) {
    Record &R = Out.Records[I];
    if (Cursor >= Blob.size())
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " name_off " << R.NameOffset
           << " is past the end of the " << Blob.size() << "-byte blob";
      });
    if (R.NameOffset != Cursor)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " name_off " << R.NameOffset
           << " does not point at the first byte of its blob string "
              "(expected offset "
           << Cursor << ")";
      });
    size_t Nul = Blob.find('\0', Cursor);
    if (Nul == StringRef::npos)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " name is not NUL-terminated";
      });
    if (Nul == Cursor)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " has an empty name";
      });
    R.Name = Blob.substr(Cursor, Nul - Cursor).str();
    Cursor = Nul + 1;
  }
  if (Cursor != Blob.size())
    return makeError([&](raw_ostream &OS) {
      OS << "blob has " << (Blob.size() - Cursor)
         << " trailing byte(s) after the last name";
    });

  // Duplicate names (equal string content) across records are rejected.
  for (size_t I = 0; I != Out.Records.size(); ++I)
    for (size_t J = I + 1; J != Out.Records.size(); ++J)
      if (Out.Records[I].Name == Out.Records[J].Name)
        return makeError([&](raw_ostream &OS) {
          OS << "records " << I << " and " << J << " carry the same name '"
             << Out.Records[I].Name << "'";
        });

  return Error::success();
}

llvm::Error MCS251Signatures::compareRecords(const Record &A, const Record &B,
                                             StringRef Context) {
  // Frozen "比较语义", with the zero-parameter compatibility exception
  // documented below: a differing no-prototype bit is itself a conflict, and
  // it changes which fields are compared when both sides agree.
  const bool ANoProto = hasNoPrototype(A.Role);
  const bool BNoProto = hasNoPrototype(B.Role);
  auto conflict = [&](StringRef Field, const Twine &AV, const Twine &BV) {
    return makeError([&](raw_ostream &OS) {
      OS << "records '" << A.Name << "' have a " << Field << " conflict";
      if (!Context.empty())
        OS << " (" << Context << ")";
      OS << ": " << AV << " vs " << BV;
    });
  };

  // Zero-parameter compatibility (PM ruling 2026-09-15, 方案 B): when BOTH
  // sides have param_count 0, a differing bit2 carries no information -- a
  // `()` and a `(void)` zero-parameter function have the identical runtime
  // ABI (nothing to marshal), and writers older than the zero-parameter rule
  // (which always wrote bit2 for K&R) must stay linkable against new ones and
  // vice versa.  Such a pair falls through to the bit2=0 branch, which for
  // param_count 0 compares exactly (ret, variadic, call_abi) -- a strict
  // subset of the shared ABI, so nothing is silently accepted that the old
  // bit2=1 branch would have caught.  A bit2 difference with ANY parameter
  // count above zero remains a conflict: the writer records bit2 verbatim
  // (a K&R definition keeps its real parameter list), so the sides really
  // disagree on how arguments are marshalled.
  const bool ZeroParamsBothSides = A.ParamCount == 0 && B.ParamCount == 0;
  if (ANoProto != BNoProto && !ZeroParamsBothSides) {
    return makeError([&](raw_ostream &OS) {
      OS << "records '" << A.Name
         << "' disagree on prototype-ness (one side is K&R no-prototype, the "
            "other is prototyped)";
      if (!Context.empty())
        OS << " (" << Context << ")";
    });
  }

  if (ANoProto && BNoProto) {
    // Both sides bit2=1: compare (ret, call_abi) only.
    if (A.Ret != B.Ret)
      return conflict("return type", Twine(unsigned(A.Ret)),
                      Twine(unsigned(B.Ret)));
  } else {
    // Both sides bit2=0: compare (param_count, bitmap, ret, call_abi, bit3).
    if (A.ParamCount != B.ParamCount)
      return conflict("parameter count", Twine(unsigned(A.ParamCount)),
                      Twine(unsigned(B.ParamCount)));
    for (unsigned I = 0; I != A.ParamCount; ++I)
      if (bitmapGet(A.Bitmap, I) != bitmapGet(B.Bitmap, I))
        return makeError([&](raw_ostream &OS) {
          OS << "records '" << A.Name << "' disagree on source parameter " << I
             << " bit-ness (bit vs non-bit)";
          if (!Context.empty())
            OS << " (" << Context << ")";
        });
    if (A.Ret != B.Ret)
      return conflict("return type", Twine(unsigned(A.Ret)),
                      Twine(unsigned(B.Ret)));
    if (isVariadic(A.Role) != isVariadic(B.Role))
      return makeError([&](raw_ostream &OS) {
        OS << "records '" << A.Name
           << "' disagree on variadic-ness (bit3 differs)";
        if (!Context.empty())
          OS << " (" << Context << ")";
      });
  }

  if (A.CallABIMajor != B.CallABIMajor || A.CallABIMinor != B.CallABIMinor)
    return makeError([&](raw_ostream &OS) {
      OS << "records '" << A.Name << "' call_abi conflict";
      if (!Context.empty())
        OS << " (" << Context << ")";
      OS << ": " << unsigned(A.CallABIMajor) << "." << unsigned(A.CallABIMinor)
         << " vs " << unsigned(B.CallABIMajor) << "."
         << unsigned(B.CallABIMinor);
    });

  return Error::success();
}

std::string MCS251Signatures::describeRole(uint8_t Role) {
  std::string Out;
  auto Add = [&](StringRef S) {
    if (!Out.empty())
      Out += "+";
    Out += S.str();
  };
  switch (Role & Role_DefinitionMask) {
  case Role_HasDefinition:
    Add("definition");
    break;
  case Role_DeclaredNotDefined:
    Add("declaration");
    break;
  default:
    Add("illegal-definition-bits");
    break;
  }
  if (Role & Role_NoPrototype)
    Add("no-prototype");
  if (Role & Role_Variadic)
    Add("variadic");
  uint8_t Reserved = Role & Role_ReservedMask;
  if (Reserved) {
    Out += " reserved=0x";
    raw_string_ostream OS(Out);
    OS << format_hex(Reserved, 2);
    OS.flush();
  }
  return Out;
}

Record MCS251Signatures::makeRecord(StringRef Name, bool IsDefinition,
                                    unsigned ParamCount,
                                    const std::vector<uint8_t> &Bitmap,
                                    bool Ret, bool NoPrototype, bool Variadic,
                                    uint8_t CallABIMajor,
                                    uint8_t CallABIMinor) {
  Record R;
  R.Name = Name.str();
  R.Role = IsDefinition ? Role_HasDefinition : Role_DeclaredNotDefined;
  if (NoPrototype)
    R.Role |= Role_NoPrototype;
  if (Variadic)
    R.Role |= Role_Variadic;
  R.ParamCount = uint8_t(ParamCount);
  R.Bitmap.assign(bitmapBytes(R.ParamCount), 0);
  for (unsigned I = 0; I != ParamCount && I / 8 < R.Bitmap.size(); ++I)
    if (bitmapGet(Bitmap, I))
      R.Bitmap[I / 8] = uint8_t(R.Bitmap[I / 8] | uint8_t(1u << (I % 8)));
  R.Ret = Ret ? 1 : 0;
  R.CallABIMajor = CallABIMajor;
  R.CallABIMinor = CallABIMinor;
  return R;
}

llvm::Error MCS251Signatures::checkABIGeneration(const Table &T, uint8_t Major,
                                                 uint8_t Minor) {
  for (size_t I = 0; I != T.Records.size(); ++I) {
    const Record &R = T.Records[I];
    if (R.CallABIMajor != Major || R.CallABIMinor != Minor)
      return makeError([&](raw_ostream &OS) {
        OS << "record " << I << " ('" << R.Name << "') call_abi "
           << unsigned(R.CallABIMajor) << "." << unsigned(R.CallABIMinor)
           << " disagrees with the object identity CallABI "
           << unsigned(Major) << "." << unsigned(Minor);
      });
  }
  return Error::success();
}

std::string MCS251Signatures::encode(const Table &T) {
  std::string Out;
  Out.push_back(char(ValueVersion));
  Out.push_back(char(0)); // flags
  Out.push_back(char(T.Records.size() & 0xff));
  Out.push_back(char((T.Records.size() >> 8) & 0xff));

  std::string Blob;
  for (const Record &R : T.Records) {
    if (R.Name.empty())
      report_fatal_error("MCS251 signatures: refusing to encode an empty "
                         "function name");
    uint32_t NameOff = uint32_t(Blob.size());
    auto LE = [&](uint32_t V) {
      Out.push_back(char(V & 0xff));
      Out.push_back(char((V >> 8) & 0xff));
      Out.push_back(char((V >> 16) & 0xff));
      Out.push_back(char((V >> 24) & 0xff));
    };
    LE(NameOff);
    Out.push_back(char(R.Role));
    Out.push_back(char(R.ParamCount));
    for (size_t B = 0; B != bitmapBytes(R.ParamCount); ++B)
      Out.push_back(char(B < R.Bitmap.size() ? R.Bitmap[B] : 0));
    Out.push_back(char(R.Ret));
    Out.push_back(char(R.CallABIMajor));
    Out.push_back(char(R.CallABIMinor));
    Blob.append(R.Name);
    Blob.push_back('\0');
  }
  Out.append(Blob);

  // The writer must never disagree with the reader: decode our own output.
  Table Check;
  if (llvm::Error E = decode(Out, Check)) {
    report_fatal_error(Twine("MCS251 signatures: encoder produced a value the "
                             "strict decoder rejects: ") +
                       toString(std::move(E)));
  }
  return Out;
}
