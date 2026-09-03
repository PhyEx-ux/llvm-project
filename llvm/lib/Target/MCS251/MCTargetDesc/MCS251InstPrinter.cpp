//===-- MCS251InstPrinter.cpp - Print MCS-251 MC instructions ------------===//

#include "MCS251InstPrinter.h"
#include "MCS251MCTargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"

using namespace llvm;

#include "MCS251GenAsmWriter.inc"

void MCS251InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << StringRef(getRegisterName(Reg, MCS251::NoRegAltName)).lower();
}

void MCS251InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }
  // All immediates in the current instruction set carry their own
  // PrintMethod (printImm8/printImm16), so this is a defensive fallback.
  if (Op.isImm()) {
    O << "#0x" << Twine::utohexstr(Op.getImm());
    return;
  }
  llvm_unreachable("MCS251InstPrinter: unsupported operand kind");
}

void MCS251InstPrinter::printImm8(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  // Mask off the sign extension: a negative i8 constant arrives here
  // sign-extended to 64 bits but must print as its byte pattern.
  O << format("#0x%02x", MI->getOperand(OpNo).getImm() & 0xff);
}

void MCS251InstPrinter::printImm16(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  O << format("#0x%04x", MI->getOperand(OpNo).getImm() & 0xffff);
}

void MCS251InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot,
                                  const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}
