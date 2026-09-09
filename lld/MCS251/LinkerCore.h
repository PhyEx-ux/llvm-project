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
  // E3/M4 Code ROM gate.  Off by default: the build layer owns the decision
  // to pass a flash window, the linker never presumes one.  When on, every
  // occupied CODE-class section (HOME/VECS/BOOT/CSEG/XINIT) must lie entirely
  // within [FlashBase, FlashBase + FlashSize).  Holes inside an area remain
  // legal (sparse layout is a design feature); only occupied sections are
  // checked.  XSEG is XDATA NOBITS in a separate address space (SPEC §4.1:
  // no ROM load bytes) and is not gated.  The window is plain numbers only.
  bool FlashGate = false;
  uint32_t FlashBase = 0;
  uint32_t FlashSize = 0;
  std::vector<std::pair<std::string, uint32_t>> AreaStarts;
  std::vector<Range> ReservedData;
  std::vector<std::string> Inputs;
  bool PrintInput = false;
  bool EnableStackGate = false;
  // E5 traceability switch.  Off by default: the frozen release artifacts pin
  // the exact output bytes (realhw-demo/release/manifest.json hashes the
  // linked ELF and the map), so emitting a final symbol table and map
  // function rows must be an explicit opt-in, never a silent default.
  bool KeepSymbols = false;
};

// E5: one final-ELF symbol collected after layout.  Synth marks the
// synthesised boundary/stack symbols; they resolve to SHN_ABS in the output
// because they belong to no loadable output section.
struct OutputSymbol {
  std::string Name;
  uint32_t Address = 0;
  uint32_t Size = 0;
  uint8_t Bind = 0; // ELF::STB_LOCAL / ELF::STB_GLOBAL
  uint8_t Type = 0; // ELF::STT_NOTYPE / ELF::STT_OBJECT / ELF::STT_FUNC
  bool Synth = false;
};

struct LinkerResult {
  uint32_t Entry = 0;
  std::map<uint32_t, uint8_t> Image;
  std::string Map;
  std::string InputReport;
  // E5: final symbols with post-layout addresses, always collected so the
  // flavor shell can decide whether to serialize them.
  std::vector<OutputSymbol> Symbols;
};

bool linkCore(LinkerConfig Config, LinkerResult &Result,
              llvm::raw_ostream &Err);

} // namespace lld::mcs251

#endif
