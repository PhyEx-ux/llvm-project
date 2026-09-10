//===-- MCS251MCInstLower.cpp - Convert MCS251 MachineInstr to an MCInst -===//

#include "MCS251MCInstLower.h"
#include "MCS251BitObject.h"
#include "MCS251InstrInfo.h"
#include "MCTargetDesc/MCS251BitAddr.h"
#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/GlobalVariable.h"
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
                                                MCSymbol *Sym,
                                                bool IsBitAddrPos) const {
  // BT12 (defense in depth): a global that is a persistent bit-object
  // placeholder is identity, not a byte address. It is legal only as the
  // bit-address operand of a bit-addressed instruction; there it is tagged
  // with S_BITADDR so the MC emitter writes the zero-filled bit-address field
  // plus R_MCS251_BITADDR8. The PRIMARY guard is the whole-function boundary
  // check in MCS251AsmPrinter::verifyFinalMachineBoundary (which also catches
  // the `&flag` external-symbol and inline-asm spellings this path cannot
  // see); this second layer keeps the target lowering itself from ever
  // producing an ordinary address operand for a handle. The diagnostic names
  // the IR global, matching the boundary layer.
  const bool IsBitObject =
      MO.isGlobal() && isa<GlobalVariable>(MO.getGlobal()) &&
      MCS251::isBitObjectGlobal(cast<GlobalVariable>(*MO.getGlobal()));
  if (IsBitObject) {
    const auto *GV = cast<GlobalVariable>(MO.getGlobal());
    if (!IsBitAddrPos)
      report_fatal_error(
          "MCS251: bit object '" + GV->getName() +
          "' may only be used as the bit-address operand of a bit instruction");
    if (MO.getOffset())
      // The frozen protocol requires r_addend 0 (BIT-OBJECT-CONTRACT.md §4.2).
      report_fatal_error("MCS251: bit object '" + GV->getName() +
                         "' bit-address operand must have no addend");
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(Sym, MCS251::S_BITADDR, Ctx));
  }

  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

void MCS251MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());
  const unsigned Opc = MI->getOpcode();

  for (const MachineOperand &MO : MI->operands()) {
    // The MI operand index (not the emitted MCInst index) decides whether this
    // operand is the instruction's bit-address field.
    const bool IsBitAddrPos = MCS251InstrInfo::isBitAddrOperand(Opc, MO.getOperandNo());
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
      MCOp = LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO), IsBitAddrPos);
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO), IsBitAddrPos);
      break;
    case MachineOperand::MO_RegisterMask:
      // The call-preserved mask of ECALL: a codegen-only operand with no
      // assembler representation.
      continue;
    }

    OutMI.addOperand(MCOp);
  }
}
