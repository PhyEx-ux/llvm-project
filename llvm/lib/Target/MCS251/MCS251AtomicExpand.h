//===-- MCS251AtomicExpand.h - Single-core EA atomic lowering ---*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Lowers LLVM IR atomic operations (fence, atomic load/store, atomicrmw,
// cmpxchg) for the single-core MCS-251 MCU:
//   * fence -> llvm.mcs251.atomic.fence (zero-byte compiler scheduling barrier)
//   * 1-byte atomic load/store -> fenced volatile byte load/store (single byte
//     access cannot tear on MCS-251)
//   * 2-/4-byte atomic load/store and 1-/2-/4-byte atomicrmw/cmpxchg ->
//     bracketed by hardware EA (IE.7, bit 0xAF) save-disable / restore
//     intrinsics (atomic `jbc 0xAF` + conditional `setb 0xAF`), supporting
//     arbitrary 1-byte alignment and all data address spaces (AS0, AS3 __xdata)
//   * >4-byte atomic load/store/xchg/cmpxchg -> generic __atomic_* libcalls
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251ATOMICEXPAND_H
#define LLVM_LIB_TARGET_MCS251_MCS251ATOMICEXPAND_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {
namespace MCS251 {

/// Legacy module pass used by MCS251TargetMachine's TargetPassConfig.
ModulePass *createMCS251AtomicExpandPass();

/// New pass manager wrapper (pipeline name "mcs251-atomic-expand").
class MCS251AtomicExpandPass
    : public RequiredPassInfoMixin<MCS251AtomicExpandPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace MCS251
} // namespace llvm

#endif
