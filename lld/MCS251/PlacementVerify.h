//===- PlacementVerify.h - independent placement verifier ---------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// G11-D (design §3.4, contract §1/§3): the standalone `mcs251-placement-verify`
// verifier.  It re-reads the ACTUAL serialized artifacts -- the final ELF, the
// report text, the complete original object set, every manifest and the link
// map -- and never trusts `LinkerResult.Placement`.  It shares no parse or
// merge oracle with the producer (it does not link lldMCS251).
//===----------------------------------------------------------------------===//

#ifndef LLD_MCS251_PLACEMENTVERIFY_H
#define LLD_MCS251_PLACEMENTVERIFY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lld::mcs251::placementverify {

// The verifier invocation surface (contract §1, R2).  Numeric window
// parameters reuse the linker's own CLI semantics; they are passed
// independently and are never taken from the map's "already placed here".
struct Options {
  std::string Elf;
  std::string Report;
  std::string LinkMap;
  std::vector<std::string> Objects;    // Complete link input set, in order.
  std::vector<std::string> Manifests;  // Every manifest, in link order.

  bool HasFlashBase = false, HasFlashSize = false;
  uint32_t FlashBase = 0, FlashSize = 0;
  bool HasXdataSize = false;
  uint32_t XdataSize = 0;
  bool HasEdataEnd = false;
  uint32_t EdataEnd = 0;
  bool HasIramSize = false;
  uint32_t IramSize = 0;
  std::vector<std::pair<std::string, uint32_t>> AreaStarts;
  std::vector<std::pair<uint32_t, uint32_t>> ReservedData;
};

// Run every numbered check V1-V18.  Returns true on success; on failure the
// frozen `VERIFY FAIL...` body has already been written to `Err` (with no
// `mcs251-lld:` prefix added).  An empty `Objects` set or a missing
// `LinkMap` makes the location-dependent checks fail closed (never skipped).
bool verifyPlacement(const Options &O, llvm::raw_ostream &Err);

// CLI entry used by PlacementVerifyMain.cpp: parse `Args` and verify.  Returns
// the process exit code (0 success, 1 anything else; no partial-success code).
int runPlacementVerify(llvm::ArrayRef<const char *> Args,
                       llvm::raw_ostream &Out, llvm::raw_ostream &Err);

} // namespace lld::mcs251::placementverify

#endif
