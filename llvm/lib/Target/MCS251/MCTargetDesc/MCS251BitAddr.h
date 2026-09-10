//===-- MCS251BitAddr.h - Shared bit-address operand validation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single validation point for the bit-address field (mcs251_bitaddr) of a
// bit-addressed instruction.  Both MC consumers of that operand must agree:
//
//   * MCS251MCCodeEmitter::putBitAddr encodes the byte for object output, and
//   * MCS251InstPrinter::printBitAddr prints it for `-filetype=asm`.
//
// Historically only the emitter checked the range; the printer merely masked
// with `& 0xff`, so `llc -filetype=asm` silently turned -1/256/300 into
// 0xff/0x00/0x2c and exited 0 (an assert-only guard is compiled out in release
// builds).  Sharing this helper keeps the two paths in lock-step: a bit
// address outside [0, 255] -- or a non-immediate -- is a loud diagnostic on
// both the object and the text-assembly path.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251BITADDR_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251BITADDR_H

#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

namespace llvm {
namespace MCS251 {

// Validate a bit-address operand and return its value in [0, 255].  Report a
// fatal error (effective in release builds too) for a symbolic operand or a
// value outside the 8-bit bit-address space.  A bit address is never symbolic
// -- it is a constant from the intrinsic or the frontend's fixed lvalue -- so
// an expression here is a backend bug.
inline unsigned getBitAddr(const MCOperand &Op) {
  if (!Op.isImm())
    report_fatal_error("MCS251: bit address must be a constant immediate");
  int64_t V = Op.getImm();
  if (V < 0 || V > 0xff)
    report_fatal_error("MCS251: bit address " + Twine(V) +
                       " is out of range [0, 255]");
  return unsigned(V);
}

} // namespace MCS251
} // namespace llvm

#endif
