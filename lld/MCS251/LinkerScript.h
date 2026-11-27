//===- LinkerScript.h - GNU-style linker script support for MCS251 -*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLD_MCS251_LINKERSCRIPT_H
#define LLD_MCS251_LINKERSCRIPT_H

#include "LinkerCore.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lld::mcs251 {

struct ParsedLinkerScript {
  std::optional<uint32_t> FlashBase;
  std::optional<uint32_t> FlashSize;
  std::optional<uint32_t> IramSize;
  std::optional<uint32_t> EdataEnd;
  std::optional<uint32_t> XdataSize;
  std::optional<std::string> EntrySymbol;

  std::vector<std::pair<std::string, uint32_t>> AreaStarts;
  std::vector<Range> ReservedData;
  std::vector<std::string> SearchDirs;
  std::vector<std::string> ExternSymbols;
  // Extra inputs from INPUT(...) / GROUP(...); entries starting with "-l" are
  // library references resolved via -L/SEARCH_DIR.
  std::vector<std::string> ExtraInputs;
  std::vector<std::string> KeepSectionPatterns;
  std::vector<LinkerConfig::ScriptSymbol> Symbols;
  // All script files read (including recursive INCLUDEs), lexically absolute.
  std::vector<std::string> LoadedFiles;
};

/// Parse one GNU-style MCS-251 linker script (-T / --script) into \p Out.
/// Multiple scripts may be parsed sequentially into the same \p Out.
bool parseLinkerScriptFile(llvm::StringRef Path, ParsedLinkerScript &Out,
                           llvm::raw_ostream &Err);

} // namespace lld::mcs251

#endif
