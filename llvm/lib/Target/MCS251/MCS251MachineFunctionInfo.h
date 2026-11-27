//===-- MCS251MachineFunctionInfo.h - MCS-251 machine function info -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_MCS251_MCS251MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include <cstdint>

namespace llvm {

class MCS251MachineFunctionInfo final : public MachineFunctionInfo {
  /// Frame index pointing immediately above the first variadic argument on the
  /// caller's stack (SPX_entry - 2 - FixedStackArgBytes). va_arg steps downward
  /// from this address by each argument's byte size.
  int VarArgsFrameIndex = 0;

  /// Total bytes occupied by this function's incoming fixed stack arguments.
  /// Used to decide whether a direct tail call's outgoing stack arguments fit
  /// inside the caller's incoming argument area.
  uint64_t IncomingStackArgBytes = 0;

public:
  MCS251MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<MCS251MachineFunctionInfo>(*this);
  }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }

  uint64_t getIncomingStackArgBytes() const { return IncomingStackArgBytes; }
  void setIncomingStackArgBytes(uint64_t Bytes) {
    IncomingStackArgBytes = Bytes;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCS251MACHINEFUNCTIONINFO_H
