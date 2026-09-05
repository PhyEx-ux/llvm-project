//===-- MCS251FixupKinds.h - MCS-251 fixup kinds ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MCS-251 object path supports full-word/address and byte-of24 relocations,
// matching the two ASxxxx R-record modes sdas251 emits for compiler output
// (relocation-map.tsv, validated against reloc-consumer.rel):
//
//   fixup_mcs251_16  ->  mode 0x002 (symbol) / 0x000 (area): a 16-bit
//                        big-endian value, e.g. `mov wr4, #_sym`.
//   fixup_mcs251_24  ->  mode 0x082 (symbol) / 0x080 (area): a complete
//                        24-bit big-endian address, e.g. ecall/ejmp targets.
//
// Phase 11 address materialisation additionally uses lo/mid/hi byte-of24
// modes 0x103/0x183/0x383 (symbol), 0x101/0x181/0x381 (area). There is no
// symbolic displacement relocation: the register base carries the symbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251FIXUPKINDS_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace MCS251 {

enum Fixups {
  // ASxxxx R mode 0x02: a 16-bit big-endian symbol/area value.
  fixup_mcs251_16 = FirstTargetFixupKind,
  // ASxxxx R mode 0x82: a complete 24-bit big-endian symbol/area value.
  fixup_mcs251_24,
  // One selected byte of a full symbol+addend (ASxxxx R_BYTE|R_BYT3).
  fixup_mcs251_lo8,
  fixup_mcs251_mid8,
  fixup_mcs251_hi8,
  NumTargetFixupKinds,
};

} // namespace MCS251
} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251FIXUPKINDS_H
