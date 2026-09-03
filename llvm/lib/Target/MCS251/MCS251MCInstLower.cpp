//===-- MCS251MCInstLower.cpp - Convert MCS251 MachineInstr to an MCInst -===//

#include "MCS251MCInstLower.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

MCS251MCInstLower::MCS251MCInstLower(AsmPrinter &Printer)
    : Printer(Printer), Ctx(Printer.OutContext) {}

MCSymbol *
MCS251MCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag on GV operand");
  case 0:
    break;
  }
  return Printer.getSymbol(MO.getGlobal());
}

MCSymbol *
MCS251MCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag on external symbol operand");
  case 0:
    break;
  }
  return Printer.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCOperand MCS251MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                                MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

void MCS251MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    switch (MO.getType()) {
    default:
      MI->print(errs());
      llvm_unreachable("MCS251MCInstLower: unsupported operand type");
    case MachineOperand::MO_Register:
      // Ignore all implicit register operands (e.g. the fixed dpl/dph defs
      // of MOV8dpl/MOV8dph, the psw clobber/uses of calls and the jcc
      // family, which the assembler reads from the mnemonic).
      if (MO.isImplicit())
        continue;
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_MachineBasicBlock:
      // Branch target: hand the assembler the block's symbol (same shape as
      // MSP430/AVR).
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_GlobalAddress:
      MCOp = LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO));
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO));
      break;
    case MachineOperand::MO_RegisterMask:
      // The call-preserved mask of ECALL: a codegen-only operand with no
      // assembler representation.
      continue;
    }

    OutMI.addOperand(MCOp);
  }
}
