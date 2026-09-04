//===-- MCS251MCCodeEmitter.h - MCS-251 code emitter --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Binary encoding of MCS-251 MCInsts for the object path (Phase 13a).  See
// MCS251MCCodeEmitter.cpp for the encoding conventions (A5 prefix rule,
// register codes, SFR direct addresses, fixup kinds).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCCODEEMITTER_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCCODEEMITTER_H

#include "llvm/MC/MCCodeEmitter.h"

namespace llvm {
class MCContext;
class MCInstrInfo;

class MCS251MCCodeEmitter final : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  MCContext &Ctx;

public:
  MCS251MCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), Ctx(Ctx) {}
  ~MCS251MCCodeEmitter() override = default;

  void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCCODEEMITTER_H
