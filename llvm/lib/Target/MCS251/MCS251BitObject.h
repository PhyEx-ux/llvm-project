//===- MCS251BitObject.h - MCS-251 bit-object handle predicate -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single recognition point for a persistent/static `bit` object placeholder
// (BIT-TASK-BREAKDOWN.md BT12, P09).  A bit object reaches the backend as an
// ordinary default-address-space i8 GlobalVariable that carries the structural
// global attribute `mcs251-bit-object`.
//
// The placeholder is object identity only.  It is never allocated as a byte
// (no DSEG/XINIT/CSEG data), it has no byte address, and ordinary
// load/store/GEP/cast/ptrtoint/initializer escapes are rejected by the contract
// verifier.  The AsmPrinter diverts a defined placeholder into a `.mcs251.bit`
// kind-1 record, and the MC layer references it only through a symbolic
// R_MCS251_BITADDR8 field.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251BITOBJECT_H
#define LLVM_LIB_TARGET_MCS251_MCS251BITOBJECT_H

#include "llvm/BinaryFormat/MCS251Bit.h"
#include "llvm/IR/GlobalVariable.h"

namespace llvm {
namespace MCS251 {

/// \return true when \p GV is a bit-object placeholder carrying the frozen
/// structural attribute.  Never a name/section test.
inline bool isBitObjectGlobal(const GlobalVariable &GV) {
  return GV.hasAttribute(MCS251Bit::BitObjectAttrName);
}

} // end namespace MCS251
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCS251BITOBJECT_H
