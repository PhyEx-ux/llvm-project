//===-- MCS251MCInstLower.cpp - Convert MCS251 MachineInstr to an MCInst -===//

#include "MCS251MCInstLower.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

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
      // of MOV8dpl/MOV8dph, which the assembler reads from the mnemonic).
      if (MO.isImplicit())
        continue;
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    }

    OutMI.addOperand(MCOp);
  }
}
