//===-- MCS251HelperABI.h - registered external helper signatures ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// P-4 freeze: "后端生成的外部 libcall（如现有除法 lowering 生成的外部
// helper）按已登记 helper ABI 自动补签名记录；未登记 ABI 的外部 libcall 硬
// 错".  This is the single registry of the helper ABIs the MCS-251 backend is
// allowed to lower a SelectionDAG operation into.  Every entry names the
// FINAL ELF symbol (target global prefix already applied) and its frozen
// source-level parameter count.
//
// The count is the *source* parameter count of the C helper, not the LLVM
// argument count: all of these helpers are ordinary non-variadic C functions
// whose parameters are integers or float32, so every source parameter is a
// non-bit ("known byte") value.  A helper reached by the backend is recorded
// as a role=declaration record with an all-zero bitmap.
//
// Nothing here may disagree with RuntimeLibcalls.td (the DEFINING point of
// the connected libcall set) or with the runtime sources
// (llvm/lib/Target/MCS251/Runtime for the integer helpers,
// validation/mcs251-runtime for the binary32 helpers).  A libcall that
// RuntimeLibcalls.td exposes but this table does not name is a fail-closed
// "unregistered helper ABI" error in the AsmPrinter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251HELPERABI_H
#define LLVM_LIB_TARGET_MCS251_MCS251HELPERABI_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
namespace MCS251 {

/// One registered external helper ABI.
struct HelperABI {
  /// Final ELF symbol name (global prefix already applied).
  StringRef Symbol;
  /// Number of fixed source parameters (the helpers are non-variadic).
  uint8_t ParamCount;
};

/// The frozen helper registry.  Symbols are the target-mangled `__xxx` forms
/// produced by the `m:s` global prefix; `RuntimeLibcalls.td` names the
/// un-prefixed `_xxx` IR spelling.
inline constexpr HelperABI RegisteredHelperABIs[] = {
    // Integer division/remainder runtime: (u/s)(div/mod)(int/long) of two
    // same-width integer arguments.  Sources: llvm/lib/Target/MCS251/Runtime.
    {"__divuint", 2},
    {"__divulong", 2},
    {"__divsint", 2},
    {"__divslong", 2},
    {"__moduint", 2},
    {"__modulong", 2},
    {"__modsint", 2},
    {"__modslong", 2},
    // IEEE-754 binary32 helpers.  Sources: validation/mcs251-runtime.
    {"__addsf3", 2},
    {"__subsf3", 2},
    {"__mulsf3", 2},
    {"__divsf3", 2},
    {"__negsf2", 1},
    {"__eqsf2", 2},
    {"__nesf2", 2},
    {"__ltsf2", 2},
    {"__lesf2", 2},
    {"__gtsf2", 2},
    {"__gesf2", 2},
    {"__unordsf2", 2},
    {"__floatsisf", 1},
    {"__fixsfsi", 1},
    // G7 S1' (PM ruling 2026-09-15, D1): unsigned i32 <-> f32 pair. Same
    // single-argument DPL:DPH:B:A helper ABI as the signed pair above; the
    // narrow (i8/i16) IR forms promote to these through the generic soft-float
    // legalizer and are not separate symbols.
    {"__floatunsisf", 1},
    {"__fixunssfsi", 1},
};

/// \return the registered helper ABI for the final ELF symbol \p Symbol, or
/// nullptr when the symbol is not a registered helper.
inline const HelperABI *lookupHelperABI(StringRef Symbol) {
  for (const HelperABI &H : RegisteredHelperABIs)
    if (H.Symbol == Symbol)
      return &H;
  return nullptr;
}

} // end namespace MCS251
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCS251HELPERABI_H
