//===- LTO.h - LTO / ThinLTO integration for mcs251-lld ---------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLD_MCS251_LTO_H
#define LLD_MCS251_LTO_H

#include "LinkerCore.h"
#include "llvm/Support/raw_ostream.h"

namespace lld::mcs251 {

/// Scan \p Config.Inputs (and archive members) for LLVM bitcode files.
/// When no bitcode is present, returns true immediately with no changes.
/// When bitcode is present, performs Full LTO and/or in-process ThinLTO
/// targeting MCS-251 (enforcing MCS-251 IR contract and Tag 28 signature
/// compatibility), compiles the resulting modules in-memory to MCS-251 ELF
/// objects, appends them to \p Config.MemoryInputs, and strips the consumed
/// bitcode inputs from \p Config.Inputs.
bool runLTOPass(LinkerConfig &Config, unsigned LtoOptLevel,
                llvm::raw_ostream &Err);

} // namespace lld::mcs251

#endif
