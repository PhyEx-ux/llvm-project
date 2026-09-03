//===-- MCS251MCInstLower.h - Lower MachineInstr to MCInst ------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251MCINSTLOWER_H
#define LLVM_LIB_TARGET_MCS251_MCS251MCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MachineInstr;
class MachineOperand;
class MCSymbol;

/// Lowers MCS251 MachineInstrs to MCInst records: plain register and
/// immediate operands, branch targets and call targets (symbols).
class LLVM_LIBRARY_VISIBILITY MCS251MCInstLower {
  AsmPrinter &Printer;
  MCContext &Ctx;

  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
  MCOperand LowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

public:
  explicit MCS251MCInstLower(AsmPrinter &Printer);
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
};
} // namespace llvm

#endif
