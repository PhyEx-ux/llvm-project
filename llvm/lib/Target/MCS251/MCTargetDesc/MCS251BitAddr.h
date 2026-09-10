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
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

namespace llvm {
namespace MCS251 {

/// MCSymbolRefExpr specifier for a bit-address operand that names a persistent
/// bit object (BT12). The AsmPrinter's MCInstLower attaches it when the global
/// is a marked bit-object placeholder; the MC emitter then emits a zero field
/// plus R_MCS251_BITADDR8 instead of a constant immediate, and the printer
/// prints the symbol. An arbitrary symbol on a bit operand (a MIR author's
/// mistake) carries no specifier and stays rejected by the immediate validator.
enum {
  S_BITADDR = MCSymbolRefExpr::FirstTargetSpecifier,
};

// Validate a bit-address operand and return its value in [0, 255].  Report a
// fatal error (effective in release builds too) for a non-immediate operand or
// a value outside the 8-bit bit-address space.  A *constant* bit address is
// never symbolic -- it is a constant from the intrinsic or the frontend's fixed
// lvalue -- so an expression here is a backend bug.
inline unsigned getBitAddr(const MCOperand &Op) {
  if (!Op.isImm())
    report_fatal_error("MCS251: bit address must be a constant immediate");
  int64_t V = Op.getImm();
  if (V < 0 || V > 0xff)
    report_fatal_error("MCS251: bit address " + Twine(V) +
                       " is out of range [0, 255]");
  return unsigned(V);
}

// BT12: a bit-address operand may also be a persistent bit-object symbol.  The
// backend then emits a zero-filled field plus R_MCS251_BITADDR8, and the linker
// writes the allocated bit address.  The operand must be a plain symbol
// reference (the contract freezes addend 0) AND carry the MCS251::S_BITADDR
// specifier that MCInstLower attaches only when the named global is a marked
// bit-object placeholder.  A bare symbol on a bit operand (e.g. a function)
// therefore falls through to the constant validator and is rejected.  Shared
// by the object emitter and the text printer so both agree.
inline bool isSymbolicBitAddr(const MCOperand &Op, const MCExpr *&Sym) {
  if (!Op.isExpr())
    return false;
  const MCExpr *E = Op.getExpr();
  const auto *SRE = dyn_cast<MCSymbolRefExpr>(E);
  if (SRE && SRE->getSpecifier() == S_BITADDR) {
    Sym = E;
    return true;
  }
  return false;
}

} // namespace MCS251
} // namespace llvm

#endif
