//===-- MCS251InstPrinter.cpp - Print MCS-251 MC instructions ------------===//

#include "MCS251InstPrinter.h"
#include "MCS251MCTargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
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
  // Branch targets (MCSymbolRefExpr of the block label), same shape as
  // upstream MSP430's printOperand.
  assert(Op.isExpr() && "unknown operand kind in printOperand");
  MAI.printExpr(O, *Op.getExpr());
}

void MCS251InstPrinter::printImm8(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isExpr()) {
    printSymbolImm(O, Op.getExpr());
    return;
  }
  // Mask off the sign extension: a negative i8 constant arrives here
  // sign-extended to 64 bits but must print as its byte pattern.
  O << format("#0x%02x", Op.getImm() & 0xff);
}

void MCS251InstPrinter::printImm16(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isExpr()) {
    printSymbolImm(O, Op.getExpr());
    return;
  }
  O << format("#0x%04x", Op.getImm() & 0xffff);
}

// Symbolic immediate (`mov wr6, #_sym`): a bare MCSymbolRefExpr prints
// `#_sym`; a symbol+offset binary expr is parenthesised so it stays a single
// operand for the assembler (`#(_sym+1)`). Both forms are
// sdas251/linker-verified (Phase 8 measurements).
void MCS251InstPrinter::printSymbolImm(raw_ostream &O, const MCExpr *Expr) {
  O << '#';
  bool Paren = isa<MCBinaryExpr>(Expr);
  if (Paren)
    O << '(';
  MAI.printExpr(O, *Expr);
  if (Paren)
    O << ')';
}

// Constant displacement of an @wr+dis16 address (`mov r10, @wr6+0x1234`).
// Part of the address syntax, so no '#' prefix. Always an immediate;
// displacements are folded by LowerLoad/LowerStore, never symbolic.
void MCS251InstPrinter::printDis16(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  assert(MI->getOperand(OpNo).isImm() && "dis16 must be an immediate");
  O << format("0x%04x", MI->getOperand(OpNo).getImm() & 0xffff);
}

// Direct address (`mov r10, 0x30`; 0x80-0xff is the SFR space, see the
// comment on MOV8di in MCS251InstrInfo.td). Part of the address syntax, so
// no '#' prefix.
void MCS251InstPrinter::printDir8(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  assert(MI->getOperand(OpNo).isImm() && "dir8 must be an immediate");
  O << format("0x%02x", MI->getOperand(OpNo).getImm() & 0xff);
}

// Frame-slot address (mcs251_stack operand): OpNo is the base register
// (dr60 or dr16), OpNo+1 the signed 16-bit displacement. Prints SDCC-style:
// a zero displacement prints nothing (`@dr60`), otherwise +0x%04x / -0x%04x.
// The SIGN IS LOAD-BEARING: sdas251 parses an @DRk displacement as a
// positive 24-bit value, so a negative displacement must never be emitted
// as its two's-complement bit pattern (which is why printDis16's
// & 0xffff masking is wrong here).
void MCS251InstPrinter::printStackAddr(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Disp = MI->getOperand(OpNo + 1);
  assert(Base.isReg() && Disp.isImm() && "stack address is reg+imm");
  O << '@';
  printRegName(O, Base.getReg());
  int64_t D = (int16_t)Disp.getImm();
  if (D == 0)
    return;
  if (D > 0)
    O << format("+0x%04x", (unsigned)D);
  else
    O << format("-0x%04x", (unsigned)-D);
}

void MCS251InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot,
                                  const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  if (MI->getOpcode() == MCS251::MOVADDR32) {
    // ASxxxx has byte-of-24 relocations, but no high-word relocation for
    // MOVH's outrw operand. Spell the real MOV/MOVH bytes explicitly so the
    // assembler selects each byte AFTER the linker's full symbol+addend sum.
    unsigned Reg = MRI.getEncodingValue(MI->getOperand(0).getReg()) / 4;
    const MCExpr *Expr = MI->getOperand(1).getExpr();
    O << format("\t.db 0x7e, 0x%02x, (", (Reg << 4) | 8);
    MAI.printExpr(O, *Expr);
    O << ") >> 8, (";
    MAI.printExpr(O, *Expr);
    O << ")\n";
    O << format("\t.db 0x7a, 0x%02x, 0x00, (", (Reg << 4) | 12);
    MAI.printExpr(O, *Expr);
    O << ") >> 16";
  } else {
    printInstruction(MI, Address, O);
  }
  printAnnotation(O, Annot);
}
