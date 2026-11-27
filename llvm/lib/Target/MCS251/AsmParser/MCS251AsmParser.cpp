//===-- MCS251AsmParser.cpp - Parse MCS-251 assembly to MCInst ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Target assembly parser for the Intel MCS-251 source-mode ISA, mirroring
// MCTargetDesc/MCS251InstPrinter.cpp and MCTargetDesc/MCS251MCCodeEmitter.cpp.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "mcs251-asm-parser"

namespace {

static bool isRReg(MCRegister Reg) {
  unsigned Id = Reg.id();
  return Id >= MCS251::R0 && Id <= MCS251::R15;
}

static bool isWRReg(MCRegister Reg) {
  unsigned Id = Reg.id();
  return Id >= MCS251::WR0 && Id <= MCS251::WR30;
}

static bool isDRReg(MCRegister Reg) {
  unsigned Id = Reg.id();
  return (Id >= MCS251::DR0 && Id <= MCS251::DR28) || Id == MCS251::DR56 ||
         Id == MCS251::DR60;
}

static std::optional<MCRegister> matchRegisterName(StringRef Name) {
  std::string Lower = Name.lower();
  StringRef S(Lower);

  if (S == "a")
    return MCS251::A;
  if (S == "b")
    return MCS251::B;
  if (S == "dpl")
    return MCS251::DPL;
  if (S == "dph")
    return MCS251::DPH;
  if (S == "dptr")
    return MCS251::DPTR;
  if (S == "dpxl")
    return MCS251::DPXL;
  if (S == "psw")
    return MCS251::PSW;
  if (S == "dpx")
    return MCS251::DR56;
  if (S == "spx")
    return MCS251::DR60;

  if (S.starts_with("r") && S.size() >= 2 && isDigit(S[1])) {
    unsigned Num = 0;
    if (!S.drop_front(1).getAsInteger(10, Num) && Num <= 15)
      return MCRegister(MCS251::R0 + Num);
  }
  if (S.starts_with("wr") && S.size() >= 3 && isDigit(S[2])) {
    unsigned Num = 0;
    if (!S.drop_front(2).getAsInteger(10, Num) && Num <= 30 && (Num % 2) == 0)
      return MCRegister(MCS251::WR0 + (Num / 2));
  }
  if (S.starts_with("dr") && S.size() >= 3 && isDigit(S[2])) {
    unsigned Num = 0;
    if (!S.drop_front(2).getAsInteger(10, Num)) {
      if (Num <= 28 && (Num % 4) == 0)
        return MCRegister(MCS251::DR0 + (Num / 4));
      if (Num == 56)
        return MCS251::DR56;
      if (Num == 60)
        return MCS251::DR60;
    }
  }
  return std::nullopt;
}

class MCS251Operand final : public MCParsedAsmOperand {
public:
  enum KindTy {
    k_Token,
    k_Register,
    k_Imm,       // #<expr>
    k_Expr,      // bare <expr> (direct addr, bit addr, branch/call target)
    k_Mem,       // @reg or @reg+disp / @reg-disp
    k_AtAPlusDPTR
  };

private:
  KindTy Kind;
  SMLoc Start, End;
  std::string Tok;
  MCRegister Reg;
  const MCExpr *Expr = nullptr;
  bool HasDisp = false;

public:
  MCS251Operand(KindTy K, SMLoc S, SMLoc E) : Kind(K), Start(S), End(E) {}

  static std::unique_ptr<MCS251Operand> CreateToken(StringRef Str, SMLoc S,
                                                    SMLoc E) {
    auto Op = std::make_unique<MCS251Operand>(k_Token, S, E);
    Op->Tok = Str.lower();
    return Op;
  }

  static std::unique_ptr<MCS251Operand> CreateReg(MCRegister R, SMLoc S,
                                                  SMLoc E) {
    auto Op = std::make_unique<MCS251Operand>(k_Register, S, E);
    Op->Reg = R;
    return Op;
  }

  static std::unique_ptr<MCS251Operand> CreateImm(const MCExpr *E, SMLoc S,
                                                  SMLoc EndLoc) {
    auto Op = std::make_unique<MCS251Operand>(k_Imm, S, EndLoc);
    Op->Expr = E;
    return Op;
  }

  static std::unique_ptr<MCS251Operand> CreateExpr(const MCExpr *E, SMLoc S,
                                                   SMLoc EndLoc) {
    auto Op = std::make_unique<MCS251Operand>(k_Expr, S, EndLoc);
    Op->Expr = E;
    return Op;
  }

  static std::unique_ptr<MCS251Operand>
  CreateMem(MCRegister Base, const MCExpr *Disp, bool HasDisp, SMLoc S,
            SMLoc EndLoc) {
    auto Op = std::make_unique<MCS251Operand>(k_Mem, S, EndLoc);
    Op->Reg = Base;
    Op->Expr = Disp;
    Op->HasDisp = HasDisp;
    return Op;
  }

  static std::unique_ptr<MCS251Operand> CreateAtAPlusDPTR(SMLoc S, SMLoc E) {
    return std::make_unique<MCS251Operand>(k_AtAPlusDPTR, S, E);
  }

  KindTy getKind() const { return Kind; }
  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Imm || Kind == k_Expr; }
  bool isMem() const override {
    return Kind == k_Mem || Kind == k_AtAPlusDPTR;
  }

  bool isHashImm() const { return Kind == k_Imm; }
  bool isBareExpr() const { return Kind == k_Expr; }
  bool isMemIndirect() const { return Kind == k_Mem; }
  bool isAtAPlusDPTR() const { return Kind == k_AtAPlusDPTR; }
  bool hasDisp() const { return HasDisp; }

  StringRef getToken() const { return Tok; }
  MCRegister getReg() const override { return Reg; }
  const MCExpr *getExpr() const { return Expr; }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      OS << "Token(" << Tok << ")";
      break;
    case k_Register:
      OS << "Reg(" << Reg.id() << ")";
      break;
    case k_Imm:
      OS << "Imm(#";
      if (Expr)
        MAI.printExpr(OS, *Expr);
      OS << ")";
      break;
    case k_Expr:
      OS << "Expr(";
      if (Expr)
        MAI.printExpr(OS, *Expr);
      OS << ")";
      break;
    case k_Mem:
      OS << "Mem(@" << Reg.id();
      if (HasDisp && Expr) {
        OS << "+";
        MAI.printExpr(OS, *Expr);
      }
      OS << ")";
      break;
    case k_AtAPlusDPTR:
      OS << "AtAPlusDPTR";
      break;
    }
  }
};

class MCS251AsmParser final : public MCTargetAsmParser {
  MCAsmParser &Parser;

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

  bool evalConst(const MCExpr *E, int64_t &Val) const {
    return E && E->evaluateAsAbsolute(Val);
  }

  void addExprOrImm(MCInst &Inst, const MCExpr *E) const {
    int64_t V = 0;
    if (evalConst(E, V))
      Inst.addOperand(MCOperand::createImm(V));
    else
      Inst.addOperand(MCOperand::createExpr(E));
  }

  bool checkUImm8(const MCS251Operand &Op, uint64_t &Out,
                  StringRef What) {
    int64_t V = 0;
    if (!evalConst(Op.getExpr(), V))
      return Error(Op.getStartLoc(), "expected constant " + What);
    if (V < 0 || V > 0xff)
      return Error(Op.getStartLoc(), What + " out of range [0, 255]");
    Out = uint64_t(V);
    return false;
  }

  bool checkImm8(const MCS251Operand &Op, int64_t &Out) {
    if (!evalConst(Op.getExpr(), Out))
      return Error(Op.getStartLoc(),
                   "symbolic 8-bit immediate is not supported on MCS251");
    if (Out < -128 || Out > 255)
      return Error(Op.getStartLoc(), "8-bit immediate out of range");
    return false;
  }

  bool checkDisp16(const MCS251Operand &Op, int64_t &Out, bool SignedOnly) {
    if (!Op.hasDisp()) {
      Out = 0;
      return false;
    }
    if (!evalConst(Op.getExpr(), Out))
      return Error(Op.getStartLoc(),
                   "displacement must be a constant 16-bit integer");
    if (SignedOnly ? (Out < -32768 || Out > 32767)
                   : (Out < -32768 || Out > 65535))
      return Error(Op.getStartLoc(), "16-bit displacement out of range");
    return false;
  }

  bool isTargetTakingMnemonic(StringRef Mnemonic) const {
    return StringSwitch<bool>(Mnemonic)
        .Cases({"je", "jne", "jc", "jnc", "jg", "jle", "jsl", "jsge", "jsg",
                "jsle", "sjmp", "ejmp", "ljmp", "lcall", "ecall"},
               true)
        .Default(false);
  }

  bool parseMemOperand(OperandVector &Operands) {
    SMLoc Start = getLexer().getLoc();
    Parser.Lex(); // consume '@'

    if (getLexer().isNot(AsmToken::Identifier))
      return Error(getLexer().getLoc(),
                   "expected register after '@' in indirect operand");

    SMLoc RegLoc = getLexer().getLoc();
    StringRef RegName = getLexer().getTok().getIdentifier();
    std::string Lower = RegName.lower();

    if (Lower == "a") {
      Parser.Lex(); // consume 'a'
      if (getLexer().isNot(AsmToken::Plus))
        return Error(getLexer().getLoc(), "expected '+dptr' after '@a'");
      Parser.Lex(); // consume '+'
      if (getLexer().isNot(AsmToken::Identifier) ||
          getLexer().getTok().getIdentifier().lower() != "dptr")
        return Error(getLexer().getLoc(), "expected 'dptr' after '@a+'");
      SMLoc End = getLexer().getTok().getEndLoc();
      Parser.Lex();
      Operands.push_back(MCS251Operand::CreateAtAPlusDPTR(Start, End));
      return false;
    }

    auto MaybeReg = matchRegisterName(RegName);
    if (!MaybeReg ||
        (!isWRReg(*MaybeReg) && !isDRReg(*MaybeReg) && *MaybeReg != MCS251::DPTR))
      return Error(RegLoc, "invalid base register after '@'");

    SMLoc End = getLexer().getTok().getEndLoc();
    Parser.Lex(); // consume base register

    const MCExpr *Disp = nullptr;
    bool HasDisp = false;
    if (*MaybeReg != MCS251::DPTR &&
        (getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Minus))) {
      HasDisp = true;
      if (getLexer().is(AsmToken::Plus))
        Parser.Lex(); // consume '+'
      // Keep '-' in the stream so parseExpression parses it as unary minus.
      if (getParser().parseExpression(Disp, End))
        return true;
    }

    Operands.push_back(
        MCS251Operand::CreateMem(*MaybeReg, Disp, HasDisp, Start, End));
    return false;
  }

  bool parseOperand(OperandVector &Operands, bool ForceExpr) {
    SMLoc Start = getLexer().getLoc();

    if (getLexer().is(AsmToken::At))
      return parseMemOperand(Operands);

    if (getLexer().is(AsmToken::Hash)) {
      Parser.Lex(); // consume '#'
      const MCExpr *Expr = nullptr;
      SMLoc End;
      if (getParser().parseExpression(Expr, End))
        return true;
      Operands.push_back(MCS251Operand::CreateImm(Expr, Start, End));
      return false;
    }

    if (!ForceExpr && getLexer().is(AsmToken::Identifier)) {
      StringRef Ident = getLexer().getTok().getIdentifier();
      AsmToken Next = getLexer().peekTok();
      bool IsStandalone = Next.is(AsmToken::Comma) ||
                          Next.is(AsmToken::EndOfStatement) ||
                          Next.is(AsmToken::Eof);
      if (IsStandalone) {
        std::string Lower = Ident.lower();
        SMLoc End = getLexer().getTok().getEndLoc();
        if (Lower == "c" || Lower == "ab") {
          Parser.Lex();
          Operands.push_back(MCS251Operand::CreateToken(Lower, Start, End));
          return false;
        }
        if (auto Reg = matchRegisterName(Ident)) {
          Parser.Lex();
          Operands.push_back(MCS251Operand::CreateReg(*Reg, Start, End));
          return false;
        }
      }
    }

    const MCExpr *Expr = nullptr;
    SMLoc End;
    if (getParser().parseExpression(Expr, End))
      return true;
    Operands.push_back(MCS251Operand::CreateExpr(Expr, Start, End));
    return false;
  }

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override {
    if (tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
      return false;
    return Error(StartLoc, "invalid MCS251 register name");
  }

  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override {
    StartLoc = getLexer().getLoc();
    if (getLexer().isNot(AsmToken::Identifier))
      return ParseStatus::NoMatch;
    auto MaybeReg = matchRegisterName(getLexer().getTok().getIdentifier());
    if (!MaybeReg)
      return ParseStatus::NoMatch;
    Reg = *MaybeReg;
    EndLoc = getLexer().getTok().getEndLoc();
    Parser.Lex();
    return ParseStatus::Success;
  }

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override {
    std::string Lower = Name.lower();
    Operands.push_back(MCS251Operand::CreateToken(
        Lower, NameLoc, SMLoc::getFromPointer(NameLoc.getPointer() + Name.size())));

    if (getLexer().is(AsmToken::EndOfStatement)) {
      Parser.Lex();
      return false;
    }

    bool ForceTargetExpr = isTargetTakingMnemonic(Lower);
    bool IsBitBranch = (Lower == "jb" || Lower == "jnb" || Lower == "jbc");

    unsigned OpIdx = 0;
    while (true) {
      bool ForceExpr = (ForceTargetExpr && OpIdx == 0) ||
                       (IsBitBranch && OpIdx == 1);
      if (parseOperand(Operands, ForceExpr))
        return true;
      ++OpIdx;
      if (getLexer().is(AsmToken::Comma)) {
        Parser.Lex();
        continue;
      }
      break;
    }

    if (getLexer().isNot(AsmToken::EndOfStatement))
      return Error(getLexer().getLoc(), "unexpected token in operand list");
    Parser.Lex();
    return false;
  }

  ParseStatus parseDirective(AsmToken DirectiveID) override {
    std::string Dir = DirectiveID.getString().lower();
    if (Dir == ".db" || Dir == ".dw") {
      unsigned Size = (Dir == ".db") ? 1 : 2;
      if (getLexer().isNot(AsmToken::EndOfStatement)) {
        while (true) {
          const MCExpr *Val = nullptr;
          if (getParser().parseExpression(Val))
            return ParseStatus::Failure;
          getStreamer().emitValue(Val, Size, DirectiveID.getLoc());
          if (getLexer().isNot(AsmToken::Comma))
            break;
          Parser.Lex();
        }
      }
      if (getLexer().isNot(AsmToken::EndOfStatement))
        return Error(getLexer().getLoc(),
                     "unexpected token in " + Dir + " directive");
      Parser.Lex();
      return ParseStatus::Success;
    }
    if (Dir == ".ds") {
      int64_t Count = 0;
      if (getParser().parseAbsoluteExpression(Count))
        return ParseStatus::Failure;
      if (Count < 0)
        return Error(DirectiveID.getLoc(), ".ds count must be non-negative");
      if (getLexer().isNot(AsmToken::EndOfStatement))
        return Error(getLexer().getLoc(), "unexpected token in .ds directive");
      Parser.Lex();
      getStreamer().emitZeros(uint64_t(Count));
      return ParseStatus::Success;
    }
    return ParseStatus::NoMatch;
  }

  void convertToMapAndConstraints(unsigned Kind,
                                  const OperandVector &Operands) override {}

  bool emitInst(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out) {
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  }

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override {
    if (Operands.empty())
      return Error(IDLoc, "empty instruction");

    const auto &Op0 = static_cast<const MCS251Operand &>(*Operands[0]);
    StringRef M = Op0.getToken();
    const size_t NumOps = Operands.size() - 1;
    auto op = [&](size_t I) -> const MCS251Operand & {
      return static_cast<const MCS251Operand &>(*Operands[I + 1]);
    };

    MCInst Inst;

    // --------------------------------------------------------- Zero-operand --
    if (M == "nop" || M == "eret" || M == "reti" || M == "ret") {
      if (NumOps != 0)
        return Error(op(0).getStartLoc(),
                     "instruction '" + M + "' takes no operands");
      unsigned Opc = StringSwitch<unsigned>(M)
                         .Case("nop", MCS251::NOP)
                         .Case("eret", MCS251::ERET)
                         .Case("reti", MCS251::RETI)
                         .Case("ret", MCS251::RET);
      Inst.setOpcode(Opc);
      return emitInst(Inst, IDLoc, Out);
    }

    // --------------------------------------------------- Branch & Call ops --
    if (unsigned BrOpc = StringSwitch<unsigned>(M)
                             .Case("je", MCS251::JE)
                             .Case("jne", MCS251::JNE)
                             .Case("jc", MCS251::JC)
                             .Case("jnc", MCS251::JNC)
                             .Case("jg", MCS251::JG)
                             .Case("jle", MCS251::JLE)
                             .Case("jsl", MCS251::JSL)
                             .Case("jsge", MCS251::JSGE)
                             .Case("jsg", MCS251::JSG)
                             .Case("jsle", MCS251::JSLE)
                             .Case("sjmp", MCS251::SJMP)
                             .Case("ejmp", MCS251::EJMP)
                             .Case("ljmp", MCS251::LJMP)
                             .Case("lcall", MCS251::LCALL)
                             .Default(0)) {
      if (NumOps != 1 || !op(0).isBareExpr())
        return Error(IDLoc, "expected single target operand for '" + M + "'");
      Inst.setOpcode(BrOpc);
      addExprOrImm(Inst, op(0).getExpr());
      return emitInst(Inst, IDLoc, Out);
    }

    if (M == "ecall") {
      if (NumOps != 1)
        return Error(IDLoc, "expected single operand for 'ecall'");
      if (op(0).isMemIndirect() && isDRReg(op(0).getReg()) && !op(0).hasDisp()) {
        Inst.setOpcode(MCS251::ECALLr);
        Inst.addOperand(MCOperand::createReg(op(0).getReg()));
        return emitInst(Inst, IDLoc, Out);
      }
      if (op(0).isBareExpr()) {
        Inst.setOpcode(MCS251::ECALL);
        addExprOrImm(Inst, op(0).getExpr());
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(op(0).getStartLoc(),
                   "expected target address or @drN for 'ecall'");
    }

    if (M == "jmp") {
      if (NumOps == 1 && op(0).isAtAPlusDPTR()) {
        Inst.setOpcode(MCS251::JMPIAD);
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(IDLoc, "only 'jmp @a+dptr' is supported for 'jmp'");
    }

    if (M == "jb" || M == "jnb" || M == "jbc") {
      if (NumOps != 2 || !op(0).isBareExpr() || !op(1).isBareExpr())
        return Error(IDLoc, "expected bit address and target for '" + M + "'");
      uint64_t Bit = 0;
      if (checkUImm8(op(0), Bit, "bit address"))
        return true;
      unsigned Opc = (M == "jb") ? MCS251::JB
                                 : (M == "jnb" ? MCS251::JNB : MCS251::JBC);
      Inst.setOpcode(Opc);
      // Operand 0 is branch target, operand 1 is bit address (MCS251InstrInfo.td).
      addExprOrImm(Inst, op(1).getExpr());
      Inst.addOperand(MCOperand::createImm(Bit));
      return emitInst(Inst, IDLoc, Out);
    }

    // --------------------------------------------------- Bit & Rotate ops --
    if (M == "setb" || M == "clr" || M == "cpl") {
      if (NumOps != 1)
        return Error(IDLoc, "expected single operand for '" + M + "'");
      if (op(0).isToken() && op(0).getToken() == "c") {
        Inst.setOpcode(M == "setb" ? MCS251::SETBC
                                   : (M == "clr" ? MCS251::CLRC : MCS251::CPLC));
        return emitInst(Inst, IDLoc, Out);
      }
      if (M == "clr" && op(0).isReg() && op(0).getReg() == MCS251::A) {
        Inst.setOpcode(MCS251::CLRA);
        return emitInst(Inst, IDLoc, Out);
      }
      if (op(0).isBareExpr()) {
        uint64_t Bit = 0;
        if (checkUImm8(op(0), Bit, "bit address"))
          return true;
        Inst.setOpcode(M == "setb"
                           ? MCS251::SETBBIT
                           : (M == "clr" ? MCS251::CLRBIT : MCS251::CPLBIT));
        Inst.addOperand(MCOperand::createImm(Bit));
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(op(0).getStartLoc(), "invalid operand for '" + M + "'");
    }

    if (M == "rrc" || M == "rlc") {
      if (NumOps == 1 && op(0).isReg() && op(0).getReg() == MCS251::A) {
        Inst.setOpcode(M == "rrc" ? MCS251::RRCA : MCS251::RLCA);
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(IDLoc, "expected 'a' operand for '" + M + "'");
    }

    // ------------------------------------------------------ Native shifts --
    if (M == "sll" || M == "srl" || M == "sra") {
      if (NumOps != 1 || !op(0).isReg())
        return Error(IDLoc, "expected rN or wrN operand for '" + M + "'");
      MCRegister Reg = op(0).getReg();
      if (isRReg(Reg)) {
        Inst.setOpcode(M == "sll" ? MCS251::SLL8
                                  : (M == "srl" ? MCS251::SRL8 : MCS251::SRA8));
      } else if (isWRReg(Reg)) {
        Inst.setOpcode(M == "sll"
                           ? MCS251::SLL16
                           : (M == "srl" ? MCS251::SRL16 : MCS251::SRA16));
      } else {
        return Error(op(0).getStartLoc(),
                     "shift operand must be an 8-bit rN or 16-bit wrN register");
      }
      Inst.addOperand(MCOperand::createReg(Reg));
      Inst.addOperand(MCOperand::createReg(Reg));
      return emitInst(Inst, IDLoc, Out);
    }

    // ---------------------------------------------------------- Push / Pop --
    if (M == "push" || M == "pop") {
      const bool IsPush = (M == "push");
      if (NumOps != 1)
        return Error(IDLoc, "expected single operand for '" + M + "'");
      if (op(0).isReg() && op(0).getReg() == MCS251::PSW) {
        Inst.setOpcode(IsPush ? MCS251::ISR_PUSH_PSW : MCS251::ISR_POP_PSW);
        return emitInst(Inst, IDLoc, Out);
      }
      if (op(0).isReg() && isDRReg(op(0).getReg())) {
        Inst.setOpcode(IsPush ? MCS251::PUSHDR : MCS251::POPDR);
        Inst.addOperand(MCOperand::createReg(op(0).getReg()));
        return emitInst(Inst, IDLoc, Out);
      }
      if (op(0).isBareExpr()) {
        uint64_t Dir = 0;
        if (checkUImm8(op(0), Dir, "direct address"))
          return true;
        Inst.setOpcode(IsPush ? MCS251::PUSHDIR : MCS251::POPDIR);
        Inst.addOperand(MCOperand::createImm(Dir));
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(op(0).getStartLoc(), "invalid operand for '" + M + "'");
    }

    // ---------------------------------------------------------- Inc / Dec --
    if (M == "inc" || M == "dec") {
      const bool IsDec = (M == "dec");
      if (!IsDec && NumOps == 1 && op(0).isReg() &&
          op(0).getReg() == MCS251::DPTR) {
        Inst.setOpcode(MCS251::INCDPTR);
        return emitInst(Inst, IDLoc, Out);
      }
      if ((NumOps != 1 && NumOps != 2) || !op(0).isReg())
        return Error(IDLoc, "expected register (and optional #1/#2/#4 step) for '" +
                                M + "'");
      int64_t Step = 1;
      if (NumOps == 2) {
        if (!op(1).isHashImm())
          return Error(op(1).getStartLoc(),
                       "expected immediate step (#1, #2, or #4)");
        if (!evalConst(op(1).getExpr(), Step) ||
            (Step != 1 && Step != 2 && Step != 4))
          return Error(op(1).getStartLoc(),
                       "inc/dec immediate step must be 1, 2, or 4");
      }
      MCRegister Reg = op(0).getReg();
      if (isRReg(Reg))
        Inst.setOpcode(IsDec ? MCS251::DEC8ri : MCS251::INC8ri);
      else if (isWRReg(Reg))
        Inst.setOpcode(IsDec ? MCS251::DEC16ri : MCS251::INC16ri);
      else if (isDRReg(Reg))
        Inst.setOpcode(IsDec ? MCS251::DEC32ri : MCS251::INC32ri);
      else
        return Error(op(0).getStartLoc(), "invalid register for '" + M + "'");
      Inst.addOperand(MCOperand::createReg(Reg));
      Inst.addOperand(MCOperand::createReg(Reg));
      Inst.addOperand(MCOperand::createImm(Step));
      return emitInst(Inst, IDLoc, Out);
    }

    // ----------------------------------------------------------- Multiply --
    if (M == "mul") {
      if (NumOps == 1 && op(0).isToken() && op(0).getToken() == "ab") {
        Inst.setOpcode(MCS251::MULAB);
        return emitInst(Inst, IDLoc, Out);
      }
      if (NumOps == 2 && op(0).isReg() && op(1).isReg() &&
          isWRReg(op(0).getReg()) && isWRReg(op(1).getReg())) {
        Inst.setOpcode(MCS251::MUL16rr);
        Inst.addOperand(MCOperand::createReg(op(0).getReg()));
        Inst.addOperand(MCOperand::createReg(op(0).getReg()));
        Inst.addOperand(MCOperand::createReg(op(1).getReg()));
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(IDLoc, "expected 'mul ab' or 'mul wrD, wrS'");
    }

    // -------------------------------------------------------- ALU & Compare --
    if (M == "add" || M == "sub" || M == "anl" || M == "orl" || M == "xrl") {
      if (NumOps != 2 || !op(0).isReg())
        return Error(IDLoc, "expected two operands for '" + M + "'");
      MCRegister Dst = op(0).getReg();

      // Special case: orl rD, a
      if (M == "orl" && isRReg(Dst) && op(1).isReg() &&
          op(1).getReg() == MCS251::A) {
        Inst.setOpcode(MCS251::OR8a);
        Inst.addOperand(MCOperand::createReg(Dst));
        Inst.addOperand(MCOperand::createReg(Dst));
        return emitInst(Inst, IDLoc, Out);
      }

      if (op(1).isReg()) {
        MCRegister Src = op(1).getReg();
        unsigned Opc = 0;
        if (isRReg(Dst) && isRReg(Src)) {
          Opc = StringSwitch<unsigned>(M)
                    .Case("add", MCS251::ADD8rr)
                    .Case("sub", MCS251::SUB8rr)
                    .Case("anl", MCS251::AND8rr)
                    .Case("orl", MCS251::OR8rr)
                    .Case("xrl", MCS251::XOR8rr);
        } else if (isWRReg(Dst) && isWRReg(Src)) {
          Opc = StringSwitch<unsigned>(M)
                    .Case("add", MCS251::ADD16rr)
                    .Case("sub", MCS251::SUB16rr)
                    .Case("anl", MCS251::AND16rr)
                    .Case("orl", MCS251::OR16rr)
                    .Case("xrl", MCS251::XOR16rr);
        } else if (isDRReg(Dst) && isDRReg(Src) && (M == "add" || M == "sub")) {
          Opc = (M == "add") ? MCS251::ADD32rr : MCS251::SUB32rr;
        }
        if (!Opc)
          return Error(op(1).getStartLoc(),
                       "mismatched or unsupported register widths for '" + M +
                           "'");
        Inst.setOpcode(Opc);
        Inst.addOperand(MCOperand::createReg(Dst));
        Inst.addOperand(MCOperand::createReg(Dst));
        Inst.addOperand(MCOperand::createReg(Src));
        return emitInst(Inst, IDLoc, Out);
      }

      if (op(1).isHashImm()) {
        if (isRReg(Dst)) {
          int64_t Imm = 0;
          if (checkImm8(op(1), Imm))
            return true;
          unsigned Opc = StringSwitch<unsigned>(M)
                             .Case("add", MCS251::ADD8ri)
                             .Case("sub", MCS251::SUB8ri)
                             .Case("anl", MCS251::AND8ri)
                             .Case("orl", MCS251::OR8ri)
                             .Case("xrl", MCS251::XOR8ri);
          Inst.setOpcode(Opc);
          Inst.addOperand(MCOperand::createReg(Dst));
          Inst.addOperand(MCOperand::createReg(Dst));
          Inst.addOperand(MCOperand::createImm(Imm & 0xff));
          return emitInst(Inst, IDLoc, Out);
        }
        if (isWRReg(Dst)) {
          unsigned Opc = StringSwitch<unsigned>(M)
                             .Case("add", MCS251::ADD16ri)
                             .Case("sub", MCS251::SUB16ri)
                             .Case("anl", MCS251::AND16ri)
                             .Case("orl", MCS251::OR16ri)
                             .Case("xrl", MCS251::XOR16ri);
          Inst.setOpcode(Opc);
          Inst.addOperand(MCOperand::createReg(Dst));
          Inst.addOperand(MCOperand::createReg(Dst));
          addExprOrImm(Inst, op(1).getExpr());
          return emitInst(Inst, IDLoc, Out);
        }
        return Error(op(0).getStartLoc(),
                     "immediate ALU operand requires rN or wrN destination");
      }
      return Error(op(1).getStartLoc(), "invalid source operand for '" + M + "'");
    }

    if (M == "cmp") {
      if (NumOps != 2 || !op(0).isReg())
        return Error(IDLoc, "expected two operands for 'cmp'");
      MCRegister LHS = op(0).getReg();
      if (op(1).isReg()) {
        MCRegister RHS = op(1).getReg();
        if (isRReg(LHS) && isRReg(RHS))
          Inst.setOpcode(MCS251::CMP8rr);
        else if (isWRReg(LHS) && isWRReg(RHS))
          Inst.setOpcode(MCS251::CMP16rr);
        else if (isDRReg(LHS) && isDRReg(RHS))
          Inst.setOpcode(MCS251::CMP32rr);
        else
          return Error(op(1).getStartLoc(), "mismatched register widths for 'cmp'");
        Inst.addOperand(MCOperand::createReg(LHS));
        Inst.addOperand(MCOperand::createReg(RHS));
        return emitInst(Inst, IDLoc, Out);
      }
      if (op(1).isHashImm()) {
        if (isRReg(LHS)) {
          int64_t Imm = 0;
          if (checkImm8(op(1), Imm))
            return true;
          Inst.setOpcode(MCS251::CMP8ri);
          Inst.addOperand(MCOperand::createReg(LHS));
          Inst.addOperand(MCOperand::createImm(Imm & 0xff));
          return emitInst(Inst, IDLoc, Out);
        }
        if (isWRReg(LHS)) {
          Inst.setOpcode(MCS251::CMP16ri);
          Inst.addOperand(MCOperand::createReg(LHS));
          addExprOrImm(Inst, op(1).getExpr());
          return emitInst(Inst, IDLoc, Out);
        }
        return Error(op(0).getStartLoc(),
                     "immediate 'cmp' requires rN or wrN operand");
      }
      return Error(op(1).getStartLoc(), "invalid second operand for 'cmp'");
    }

    // --------------------------------------------------------- MOVX / MOVH --
    if (M == "movx") {
      if (NumOps != 2)
        return Error(IDLoc, "expected two operands for 'movx'");
      if (op(0).isReg() && op(0).getReg() == MCS251::A &&
          op(1).isMemIndirect() && op(1).getReg() == MCS251::DPTR) {
        Inst.setOpcode(MCS251::MOVXALD);
        return emitInst(Inst, IDLoc, Out);
      }
      if (op(0).isMemIndirect() && op(0).getReg() == MCS251::DPTR &&
          op(1).isReg() && op(1).getReg() == MCS251::A) {
        Inst.setOpcode(MCS251::MOVXAST);
        return emitInst(Inst, IDLoc, Out);
      }
      return Error(IDLoc, "only 'movx a, @dptr' and 'movx @dptr, a' are supported");
    }

    if (M == "movh") {
      if (NumOps != 2 || !op(0).isReg() || !isDRReg(op(0).getReg()) ||
          !op(1).isHashImm())
        return Error(IDLoc, "expected 'movh drN, #imm16'");
      Inst.setOpcode(MCS251::MOVHDRi);
      Inst.addOperand(MCOperand::createReg(op(0).getReg()));
      Inst.addOperand(MCOperand::createReg(op(0).getReg()));
      addExprOrImm(Inst, op(1).getExpr());
      return emitInst(Inst, IDLoc, Out);
    }

    // ----------------------------------------------------------------- MOV --
    if (M == "mov") {
      if (NumOps != 2)
        return Error(IDLoc, "expected two operands for 'mov'");
      const MCS251Operand &Dst = op(0);
      const MCS251Operand &Src = op(1);

      // Carry bit moves: mov c, bit8 / mov bit8, c
      if (Dst.isToken() && Dst.getToken() == "c" && Src.isBareExpr()) {
        uint64_t Bit = 0;
        if (checkUImm8(Src, Bit, "bit address"))
          return true;
        Inst.setOpcode(MCS251::MOVCBIT);
        Inst.addOperand(MCOperand::createImm(Bit));
        return emitInst(Inst, IDLoc, Out);
      }
      if (Dst.isBareExpr() && Src.isToken() && Src.getToken() == "c") {
        uint64_t Bit = 0;
        if (checkUImm8(Dst, Bit, "bit address"))
          return true;
        Inst.setOpcode(MCS251::MOVBITC);
        Inst.addOperand(MCOperand::createImm(Bit));
        return emitInst(Inst, IDLoc, Out);
      }

      // Destination is a register.
      if (Dst.isReg()) {
        MCRegister DR = Dst.getReg();

        // mov dptr, #imm16 / #sym
        if (DR == MCS251::DPTR && Src.isHashImm()) {
          Inst.setOpcode(MCS251::MOVDPTRri);
          addExprOrImm(Inst, Src.getExpr());
          return emitInst(Inst, IDLoc, Out);
        }

        // mov a, rS / mov a, #imm8
        if (DR == MCS251::A) {
          if (Src.isReg() && isRReg(Src.getReg())) {
            Inst.setOpcode(MCS251::MOV8a);
            Inst.addOperand(MCOperand::createReg(Src.getReg()));
            return emitInst(Inst, IDLoc, Out);
          }
          if (Src.isHashImm()) {
            int64_t Imm = 0;
            if (checkImm8(Src, Imm))
              return true;
            Inst.setOpcode(MCS251::MOVAI);
            Inst.addOperand(MCOperand::createImm(Imm & 0xff));
            return emitInst(Inst, IDLoc, Out);
          }
        }

        // mov b/dpl/dph/dpxl, rS
        if ((DR == MCS251::B || DR == MCS251::DPL || DR == MCS251::DPH ||
             DR == MCS251::DPXL) &&
            Src.isReg() && isRReg(Src.getReg())) {
          Inst.setOpcode(DR == MCS251::B     ? MCS251::MOV8b
                         : DR == MCS251::DPL ? MCS251::MOV8dpl
                         : DR == MCS251::DPH ? MCS251::MOV8dph
                                             : MCS251::MOV8dpxl);
          Inst.addOperand(MCOperand::createReg(Src.getReg()));
          return emitInst(Inst, IDLoc, Out);
        }

        // mov rD, ...
        if (isRReg(DR)) {
          if (Src.isReg()) {
            MCRegister SR = Src.getReg();
            if (isRReg(SR)) {
              Inst.setOpcode(MCS251::MOV8rr);
              Inst.addOperand(MCOperand::createReg(DR));
              Inst.addOperand(MCOperand::createReg(SR));
              return emitInst(Inst, IDLoc, Out);
            }
            if (SR == MCS251::A || SR == MCS251::B || SR == MCS251::DPL ||
                SR == MCS251::DPH) {
              Inst.setOpcode(SR == MCS251::A     ? MCS251::MOV8ra
                             : SR == MCS251::B   ? MCS251::MOV8rb
                             : SR == MCS251::DPL ? MCS251::MOV8rdpl
                                                 : MCS251::MOV8rdph);
              Inst.addOperand(MCOperand::createReg(DR));
              return emitInst(Inst, IDLoc, Out);
            }
          }
          if (Src.isHashImm()) {
            int64_t Imm = 0;
            if (checkImm8(Src, Imm))
              return true;
            Inst.setOpcode(MCS251::MOV8ri);
            Inst.addOperand(MCOperand::createReg(DR));
            Inst.addOperand(MCOperand::createImm(Imm & 0xff));
            return emitInst(Inst, IDLoc, Out);
          }
          if (Src.isBareExpr()) {
            uint64_t Dir = 0;
            if (checkUImm8(Src, Dir, "direct address"))
              return true;
            Inst.setOpcode(MCS251::MOV8di);
            Inst.addOperand(MCOperand::createReg(DR));
            Inst.addOperand(MCOperand::createImm(Dir));
            return emitInst(Inst, IDLoc, Out);
          }
          if (Src.isMemIndirect()) {
            if (isWRReg(Src.getReg())) {
              if (!Src.hasDisp()) {
                Inst.setOpcode(MCS251::MOV8rm);
                Inst.addOperand(MCOperand::createReg(DR));
                Inst.addOperand(MCOperand::createReg(Src.getReg()));
                return emitInst(Inst, IDLoc, Out);
              }
              int64_t Disp = 0;
              if (checkDisp16(Src, Disp, /*SignedOnly=*/false))
                return true;
              Inst.setOpcode(MCS251::MOV8rmD);
              Inst.addOperand(MCOperand::createReg(DR));
              Inst.addOperand(MCOperand::createReg(Src.getReg()));
              Inst.addOperand(MCOperand::createImm(Disp & 0xffff));
              return emitInst(Inst, IDLoc, Out);
            }
            if (isDRReg(Src.getReg())) {
              int64_t Disp = 0;
              if (checkDisp16(Src, Disp, /*SignedOnly=*/true))
                return true;
              Inst.setOpcode(MCS251::MOV8rmP);
              Inst.addOperand(MCOperand::createReg(DR));
              Inst.addOperand(MCOperand::createReg(Src.getReg()));
              Inst.addOperand(MCOperand::createImm(Disp));
              return emitInst(Inst, IDLoc, Out);
            }
          }
        }

        // mov wrD, ...
        if (isWRReg(DR)) {
          if (Src.isReg() && isWRReg(Src.getReg())) {
            Inst.setOpcode(MCS251::MOV16rr);
            Inst.addOperand(MCOperand::createReg(DR));
            Inst.addOperand(MCOperand::createReg(Src.getReg()));
            return emitInst(Inst, IDLoc, Out);
          }
          if (Src.isHashImm()) {
            Inst.setOpcode(MCS251::MOV16ri);
            Inst.addOperand(MCOperand::createReg(DR));
            addExprOrImm(Inst, Src.getExpr());
            return emitInst(Inst, IDLoc, Out);
          }
          if (Src.isMemIndirect() && isDRReg(Src.getReg())) {
            int64_t Disp = 0;
            if (checkDisp16(Src, Disp, /*SignedOnly=*/true))
              return true;
            Inst.setOpcode(MCS251::MOV16rmS);
            Inst.addOperand(MCOperand::createReg(DR));
            Inst.addOperand(MCOperand::createReg(Src.getReg()));
            Inst.addOperand(MCOperand::createImm(Disp));
            return emitInst(Inst, IDLoc, Out);
          }
        }

        // mov drD, ...
        if (isDRReg(DR)) {
          if (Src.isReg() && isDRReg(Src.getReg())) {
            Inst.setOpcode(MCS251::MOV32rr);
            Inst.addOperand(MCOperand::createReg(DR));
            Inst.addOperand(MCOperand::createReg(Src.getReg()));
            return emitInst(Inst, IDLoc, Out);
          }
          if (Src.isHashImm()) {
            Inst.setOpcode(MCS251::MOVDRri);
            Inst.addOperand(MCOperand::createReg(DR));
            addExprOrImm(Inst, Src.getExpr());
            return emitInst(Inst, IDLoc, Out);
          }
        }
      }

      // Destination is direct address: mov dir8, rS / mov dir8, #imm8
      if (Dst.isBareExpr()) {
        uint64_t Dir = 0;
        if (checkUImm8(Dst, Dir, "direct address"))
          return true;
        if (Src.isReg() && isRReg(Src.getReg())) {
          Inst.setOpcode(MCS251::MOV8id);
          Inst.addOperand(MCOperand::createImm(Dir));
          Inst.addOperand(MCOperand::createReg(Src.getReg()));
          return emitInst(Inst, IDLoc, Out);
        }
        if (Src.isHashImm()) {
          int64_t Imm = 0;
          if (checkImm8(Src, Imm))
            return true;
          Inst.setOpcode(MCS251::MOVDIRI);
          Inst.addOperand(MCOperand::createImm(Dir));
          Inst.addOperand(MCOperand::createImm(Imm & 0xff));
          return emitInst(Inst, IDLoc, Out);
        }
      }

      // Destination is indirect memory: @wr[+disp] or @dr[±disp]
      if (Dst.isMemIndirect() && Src.isReg()) {
        MCRegister SR = Src.getReg();
        if (isWRReg(Dst.getReg()) && isRReg(SR)) {
          if (!Dst.hasDisp()) {
            Inst.setOpcode(MCS251::MOV8mr);
            Inst.addOperand(MCOperand::createReg(Dst.getReg()));
            Inst.addOperand(MCOperand::createReg(SR));
            return emitInst(Inst, IDLoc, Out);
          }
          int64_t Disp = 0;
          if (checkDisp16(Dst, Disp, /*SignedOnly=*/false))
            return true;
          Inst.setOpcode(MCS251::MOV8mrD);
          Inst.addOperand(MCOperand::createReg(Dst.getReg()));
          Inst.addOperand(MCOperand::createImm(Disp & 0xffff));
          Inst.addOperand(MCOperand::createReg(SR));
          return emitInst(Inst, IDLoc, Out);
        }
        if (isDRReg(Dst.getReg())) {
          int64_t Disp = 0;
          if (checkDisp16(Dst, Disp, /*SignedOnly=*/true))
            return true;
          if (isRReg(SR)) {
            Inst.setOpcode(MCS251::MOV8mrP);
            Inst.addOperand(MCOperand::createReg(Dst.getReg()));
            Inst.addOperand(MCOperand::createImm(Disp));
            Inst.addOperand(MCOperand::createReg(SR));
            return emitInst(Inst, IDLoc, Out);
          }
          if (isWRReg(SR)) {
            Inst.setOpcode(MCS251::MOV16mrS);
            Inst.addOperand(MCOperand::createReg(Dst.getReg()));
            Inst.addOperand(MCOperand::createImm(Disp));
            Inst.addOperand(MCOperand::createReg(SR));
            return emitInst(Inst, IDLoc, Out);
          }
        }
      }

      return Error(IDLoc, "invalid operands for 'mov'");
    }

    return Error(IDLoc, "unknown MCS251 instruction '" + M + "'");
  }

public:
  MCS251AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                  const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
  }
};

} // namespace

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251AsmParser() {
  RegisterMCAsmParser<MCS251AsmParser> X(getTheMCS251Target());
}
