//===-- MCS251InstPrinter.h - Print MCS-251 MC instructions ----*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251INSTPRINTER_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251INSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {
class MCS251InstPrinter final : public MCInstPrinter {
public:
  MCS251InstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                    const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const override;
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  static const char *getRegisterName(MCRegister Reg);

  void printRegName(raw_ostream &O, MCRegister Reg) override;
  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override;
};
} // namespace llvm

#endif
