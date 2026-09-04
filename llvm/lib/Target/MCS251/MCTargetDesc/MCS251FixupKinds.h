//===-- MCS251FixupKinds.h - MCS-251 fixup kinds ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MCS-251 object path (Phase 13a) needs exactly two relocation kinds,
// matching the two ASxxxx R-record modes sdas251 emits for compiler output
// (relocation-map.tsv, validated against reloc-consumer.rel):
//
//   fixup_mcs251_16  ->  mode 0x002 (symbol) / 0x000 (area): a 16-bit
//                        big-endian value, e.g. `mov wr4, #_sym`.
//   fixup_mcs251_24  ->  mode 0x082 (symbol) / 0x080 (area): a complete
//                        24-bit big-endian address, e.g. ecall/ejmp targets.
//
// Deliberately absent (see relocation-map.tsv): there is no dis16 symbolic
// fixup (an @wr+_sym displacement has no ASxxxx relocation; the base
// register carries the symbol), and the byte-of-24-bit modes (0x103/0x183/
// 0x383) are not needed because symbolic 8-bit immediates are rejected.
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
  NumTargetFixupKinds,
};

} // namespace MCS251
} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251FIXUPKINDS_H
