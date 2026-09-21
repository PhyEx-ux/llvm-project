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
class Module;
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

//===----------------------------------------------------------------------===//
// WP4: the clang backend's contract-check entry points.
//
// clang/lib/CodeGen/BackendUtil.cpp drives the MCS-251 contract check itself
// (so a deliberate capability rejection is reported through clang's
// DiagnosticsEngine instead of report_fatal_error), and it does so
// UNCONDITIONALLY: the call is compiled in whatever LLVM_TARGETS_TO_BUILD
// says, and the decision to act on it is a runtime triple test
// (`TM->getTargetTriple().getArch() == Triple::mcs251`).
//
// The implementations live in the MCS251 target library, which a build
// without the MCS251 backend does not link. The declarations therefore live
// here, in the always-linked TargetParser library, and the definitions are
// split accordingly:
//   * the deferral flag is pure state with no target dependency -- it is
//     defined here, so both sides resolve it in every configuration;
//   * the module check needs the target's contract-check implementation --
//     it is reached through a hook the MCS251 target library registers for
//     itself, and this library's entry point is a safe no-op when no checker
//     is registered.
// The alternative -- linking every clang build against MCS251CodeGen -- would
// add a link dependency on a target the configuration does not contain.
//===----------------------------------------------------------------------===//

/// WP4: set/read the "the clang backend owns the contract verdict" deferral.
/// While set, the MCS-251 target machine mounts no contract-check passes, so
/// the backend's own pre-optimization and pre-codegen checks are the only
/// ones that run. Process-global on purpose: one cc1 compiles one module per
/// process and the flag is set/cleared around the pipeline; llc never sets it.
bool setContractCheckDeferred(bool Deferred);
bool isContractCheckDeferred();

/// The module-contract checker's signature. Returns a printable message
/// (empty when the module satisfies the contract).
using ModuleContractCheckerFn = std::string (*)(const Module &M,
                                                bool CheckArithmetic);

/// Install the checker. Called by the MCS251 target library for its own
/// build; a build without that library never calls it.
void registerModuleContractChecker(ModuleContractCheckerFn Fn);

/// True when a checker has been registered (i.e. the MCS251 backend is part
/// of this build).
bool hasModuleContractChecker();

/// WP4 clang-path entry point: the same verdict as the target's own module
/// contract check, but returning a printable message (empty on success)
/// instead of an Error. Returns an empty string when no checker is
/// registered: a build without the MCS251 backend cannot reach the call site
/// with an MCS-251 target machine anyway (there is no such target to create),
/// so "no verdict" is never a silent acceptance of a real module.
std::string verifyModuleContractMessage(const Module &M, bool CheckArithmetic);

} // namespace MCS251
} // namespace llvm

#endif
