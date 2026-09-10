//===-- MCS251InstPrinter.h - Print MCS-251 MC instructions ----*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251INSTPRINTER_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251INSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {
class MCExpr;
class MCS251InstPrinter final : public MCInstPrinter {
public:
  MCS251InstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                    const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const override;
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  static const char *getRegisterName(MCRegister Reg, unsigned AltIdx);

  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printImm8(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printImm16(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printDis16(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printDir8(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  // Bit address of a bit-addressed operand (`setb 0x00`): the numeric bit
  // address itself, never the backing byte. Validated (range + immediate) by
  // the shared MCS251::getBitAddr helper, so the text path rejects the same
  // inputs as the object emitter.
  void printBitAddr(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  // Frame-slot address @dr60/@dr16<+/-displacement> (mcs251_stack operand,
  // two MC operands: base register + signed 16-bit displacement).
  void printStackAddr(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printSymbolImm(raw_ostream &O, const MCExpr *Expr);

  void printRegName(raw_ostream &O, MCRegister Reg) override;
  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override;
};
} // namespace llvm

#endif
