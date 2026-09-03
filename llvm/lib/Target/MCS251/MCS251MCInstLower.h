//===-- MCS251MCInstLower.h - Lower MachineInstr to MCInst ------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251MCINSTLOWER_H
#define LLVM_LIB_TARGET_MCS251_MCS251MCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class MCContext;
class MCInst;
class MachineInstr;

/// Lowers MCS251 MachineInstrs to MCInst records. Phase 2/3 only needs
/// plain register and immediate operands; everything else is rejected.
class LLVM_LIBRARY_VISIBILITY MCS251MCInstLower {
  MCContext &Ctx;

public:
  explicit MCS251MCInstLower(MCContext &Ctx) : Ctx(Ctx) {}
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
};
} // namespace llvm

#endif
