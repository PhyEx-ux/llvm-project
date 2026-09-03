//===-- MCS251InstPrinter.cpp - Print MCS-251 MC instructions ------------===//

#include "MCS251InstPrinter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

#include "MCS251GenAsmWriter.inc"

void MCS251InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << StringRef(getRegisterName(Reg)).lower();
}

void MCS251InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot,
                                  const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}
