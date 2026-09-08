//===- LinkerCore.h - MCS251 ELF link semantics -----------------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This interface deliberately has no dependency on the lld flavor driver.  It
// is shaped so the ELF/Arch MCS251 target handler can adopt it upstream.
//===----------------------------------------------------------------------===//

#ifndef LLD_MCS251_LINKERCORE_H
#define LLD_MCS251_LINKERCORE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lld::mcs251 {

struct Range {
  uint32_t Start = 0;
  uint32_t End = 0;
};

// The flavor shell supplies policy defaults. In particular, this core does
// not presume an Intel HEX serializer or a stack-capacity policy.
struct LinkerConfig {
  uint32_t IramSize = 128;
  uint32_t EdataEnd = 0;
  uint32_t StackSize = 0;
  std::vector<std::pair<std::string, uint32_t>> AreaStarts;
  std::vector<Range> ReservedData;
  std::vector<std::string> Inputs;
  bool PrintInput = false;
  bool EnableStackGate = false;
};

struct LinkerResult {
  uint32_t Entry = 0;
  std::map<uint32_t, uint8_t> Image;
  std::string Map;
  std::string InputReport;
};

bool linkCore(LinkerConfig Config, LinkerResult &Result,
              llvm::raw_ostream &Err);

} // namespace lld::mcs251

#endif
