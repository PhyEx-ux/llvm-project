//===-- MCS251TargetParser.cpp - MCS-251 layout description --------------===//
#include "llvm/TargetParser/MCS251TargetParser.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"

#include <algorithm>

using namespace llvm;
using namespace llvm::MCS251;

namespace {
constexpr PointerSpec CompatibilityPointers[] = {
    {0, 32, 8, 8, 32},
};

constexpr PointerSpec TinyPointers[] = {
    {0, 16, 8, 8, 16},  {1, 16, 8, 8, 16}, {2, 16, 8, 8, 16},
    {3, 32, 8, 8, 32},  {4, 32, 8, 8, 32}, {6, 16, 8, 8, 16},
    {7, 32, 8, 8, 32},  {8, 16, 8, 8, 16}, {9, 32, 8, 8, 32},
};

constexpr PointerSpec SmallPointers[] = {
    {0, 32, 8, 8, 32},  {1, 16, 8, 8, 16}, {2, 16, 8, 8, 16},
    {3, 32, 8, 8, 32},  {4, 32, 8, 8, 32}, {6, 16, 8, 8, 16},
    {7, 32, 8, 8, 32},  {8, 16, 8, 8, 16}, {9, 32, 8, 8, 32},
};

constexpr StringLiteral CompatibilityLayout =
    "E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8";
constexpr StringLiteral TinyLayout =
    "E-m:s-p:16:8:8:16-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0";
constexpr StringLiteral SmallLayout =
    "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0";
} // namespace

std::optional<LayoutDesc>
llvm::MCS251::getLayoutDesc(ASLayoutVersion Version, AS0PointerBits AS0Bits) {
  if (Version == ASLayoutVersion::Compatibility) {
    if (AS0Bits != AS0PointerBits::Bits32)
      return std::nullopt;
    return LayoutDesc{CompatibilityLayout, CompatibilityPointers,
                      /*ProgramAS=*/0, /*AllocaAS=*/0, /*GlobalAS=*/0};
  }
  if (Version != ASLayoutVersion::V2)
    return std::nullopt;

  if (AS0Bits == AS0PointerBits::Bits16)
    return LayoutDesc{TinyLayout, TinyPointers, /*ProgramAS=*/4,
                      /*AllocaAS=*/0, /*GlobalAS=*/0};
  if (AS0Bits == AS0PointerBits::Bits32)
    return LayoutDesc{SmallLayout, SmallPointers, /*ProgramAS=*/4,
                      /*AllocaAS=*/0, /*GlobalAS=*/0};
  return std::nullopt;
}

StringRef llvm::MCS251::getCompatibilityDataLayout() {
  return CompatibilityLayout;
}

bool llvm::MCS251::parseMemoryContract(StringRef Text,
                                       MemoryContract &Contract) {
  SmallVector<StringRef, 5> Fields;
  Text.split(Fields, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
  if (Fields.size() != 5)
    return false;

  unsigned Values[5] = {};
  for (unsigned I = 0; I != 5; ++I)
    if (Fields[I].getAsInteger(10, Values[I]))
      return false;

  Contract.TransportVersion = Values[0];
  Contract.ASLayoutVersion = Values[1];
  Contract.AS0PointerBits = Values[2];
  Contract.DefaultPlacement = Values[3];
  Contract.ExecutionContract = Values[4];
  return true;
}

bool llvm::MCS251::isValidMemoryContract(const MemoryContract &Contract) {
  if (Contract.TransportVersion != 1 ||
      (Contract.ASLayoutVersion != 1 && Contract.ASLayoutVersion != 2) ||
      (Contract.AS0PointerBits != 16 && Contract.AS0PointerBits != 32) ||
      (Contract.DefaultPlacement != 1 && Contract.DefaultPlacement != 3 &&
       Contract.DefaultPlacement != 8) ||
      Contract.ExecutionContract != 1)
    return false;
  if (Contract.ASLayoutVersion == 1)
    return Contract.AS0PointerBits == 32 && Contract.DefaultPlacement == 8;
  // The 16-bit AS0 v2 ABI has only the two internal placement policies.
  // ExternalData requires a far 32-bit AS0 pointer and is not a fallback model.
  if (Contract.AS0PointerBits == 16 && Contract.DefaultPlacement == 3)
    return false;
  return true;
}

std::string llvm::MCS251::formatMemoryContract(
    const MemoryContract &Contract) {
  return (Twine(Contract.TransportVersion) + "," +
          Twine(Contract.ASLayoutVersion) + "," +
          Twine(Contract.AS0PointerBits) + "," +
          Twine(Contract.DefaultPlacement) + "," +
          Twine(Contract.ExecutionContract))
      .str();
}

// P12-3: this spelling and the formatter below live in the TargetParser
// library (always linked, including into builds without the MCS251 backend)
// because clang's BackendUtil references the formatter unconditionally.
StringRef llvm::MCS251::getMCS251ContractFeaturePrefix() {
  return "+mcs251-memory-contract=";
}

std::string llvm::MCS251::formatMemoryContractFeature(
    const MemoryContract &Contract) {
  std::string Numeric = formatMemoryContract(Contract);
  std::replace(Numeric.begin(), Numeric.end(), ',', '-');
  return (Twine(getMCS251ContractFeaturePrefix()) + Numeric).str();
}

bool llvm::MCS251::isSupportedLayout(StringRef DataLayout) {
  return DataLayout == CompatibilityLayout || DataLayout == TinyLayout ||
         DataLayout == SmallLayout;
}
