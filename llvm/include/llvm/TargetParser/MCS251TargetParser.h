//===-- MCS251TargetParser.h - MCS-251 layout description -------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
#ifndef LLVM_TARGETPARSER_MCS251TARGETPARSER_H
#define LLVM_TARGETPARSER_MCS251TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
namespace MCS251 {

enum class ASLayoutVersion : uint8_t { Compatibility = 1, V2 = 2 };

enum class AS0PointerBits : uint8_t { Bits16 = 16, Bits32 = 32 };

struct PointerSpec {
  unsigned AddressSpace;
  unsigned Size;
  unsigned ABIAlignment;
  unsigned PreferredAlignment;
  unsigned IndexSize;
};

struct LayoutDesc {
  StringRef DataLayout;
  ArrayRef<PointerSpec> Pointers;
  unsigned ProgramAS;
  unsigned AllocaAS;
  unsigned GlobalAS;
};

/// Numeric transport contract shared by Clang, LLVM code generation and tools.
/// A zero transport version is the explicitly unspecified state.
struct MemoryContract {
  unsigned TransportVersion = 0;
  unsigned ASLayoutVersion = 0;
  unsigned AS0PointerBits = 0;
  unsigned DefaultPlacement = 0;
  unsigned ExecutionContract = 0;

  bool isSpecified() const { return TransportVersion != 0; }
};

/// Parse the five comma-separated numeric contract fields.
bool parseMemoryContract(StringRef Text, MemoryContract &Contract);

/// Return true only for the currently supported numeric contract.
bool isValidMemoryContract(const MemoryContract &Contract);

/// Return the canonical wire representation of a contract.
std::string formatMemoryContract(const MemoryContract &Contract);

/// The target-feature key spelling "+mcs251-memory-contract=" (P12-3: the
/// single spelling lives in the always-linked TargetParser library, so
/// builds without the MCS251 backend -- which still link clang's
/// unconditional BackendUtil reference -- resolve it).
StringRef getMCS251ContractFeaturePrefix();

/// Format a contract as the target-feature transport spelling
/// "+mcs251-memory-contract=v-t-as0-p-e" (dash separated, because feature
/// strings are comma-split). Single source of this spelling: clang's
/// BackendUtil emits it and the MCS251 target machine parses it.
std::string formatMemoryContractFeature(const MemoryContract &Contract);

/// Return the canonical MCS-251 data-layout description for the requested
/// numeric layout. Both 16-bit and 32-bit AS0 v2 variants are executable.
std::optional<LayoutDesc> getLayoutDesc(ASLayoutVersion Version,
                                        AS0PointerBits AS0Bits);

/// Return true if the layout is one of the three canonical MCS-251 layouts.
bool isSupportedLayout(StringRef DataLayout);

/// Return the canonical compatibility layout used by Triple::computeDataLayout.
StringRef getCompatibilityDataLayout();

} // namespace MCS251
} // namespace llvm

#endif
