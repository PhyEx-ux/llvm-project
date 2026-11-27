//===-- MCS251Disassembler.cpp - Disassembler for MCS-251 -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Source-mode instruction decoder for the MCS-251 architecture, mirroring
// MCTargetDesc/MCS251MCCodeEmitter.cpp plus classic 8051 CRT encodings.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MCS251MCTargetDesc.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mcs251-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

static constexpr uint8_t SFR_DPL = 0x82;
static constexpr uint8_t SFR_DPH = 0x83;
static constexpr uint8_t SFR_PSW = 0xD0;
static constexpr uint8_t SFR_B = 0xF0;

static MCRegister decodeR(unsigned Code) {
  if (Code > 15)
    return MCRegister();
  return MCRegister(MCS251::R0 + Code);
}

static MCRegister decodeWR(unsigned Code) {
  if (Code > 15)
    return MCRegister();
  return MCRegister(MCS251::WR0 + Code);
}

static MCRegister decodeDR(unsigned Code) {
  if (Code <= 7)
    return MCRegister(MCS251::DR0 + Code);
  if (Code == 14)
    return MCS251::DR56;
  if (Code == 15)
    return MCS251::DR60;
  return MCRegister();
}

static uint16_t read16BE(ArrayRef<uint8_t> B, size_t Off) {
  return (uint16_t(B[Off]) << 8) | uint16_t(B[Off + 1]);
}

static uint32_t read24BE(ArrayRef<uint8_t> B, size_t Off) {
  return (uint32_t(B[Off]) << 16) | (uint32_t(B[Off + 1]) << 8) |
         uint32_t(B[Off + 2]);
}

class MCS251Disassembler final : public MCDisassembler {
public:
  MCS251Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

DecodeStatus MCS251Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                raw_ostream &CStream) const {
  (void)CStream;
  Size = 0;
  if (Bytes.empty())
    return MCDisassembler::Fail;

  auto AddBranchTarget = [&](int64_t Target, uint64_t OpOff, uint64_t OpSize,
                             uint64_t InstSize) {
    if (!tryAddingSymbolicOperand(MI, Target, Address, /*IsBranch=*/true, OpOff,
                                  OpSize, InstSize))
      MI.addOperand(MCOperand::createImm(Target));
  };

  const uint8_t Op = Bytes[0];

  // Source-mode 0xA5 escape for classic Area-II opcodes (low nibble >= 6).
  if (Op == 0xA5) {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t O2 = Bytes[1];
    if (O2 >= 0xE8 && O2 <= 0xEF) {
      MI.setOpcode(MCS251::MOV8a);
      MI.addOperand(MCOperand::createReg(decodeR(O2 - 0xE8)));
      Size = 2;
      return MCDisassembler::Success;
    }
    if (O2 >= 0xF8 && O2 <= 0xFF) {
      MI.setOpcode(MCS251::MOV8ra);
      MI.addOperand(MCOperand::createReg(decodeR(O2 - 0xF8)));
      Size = 2;
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  switch (Op) {
  // Single-byte instructions.
  case 0x00: MI.setOpcode(MCS251::NOP);     Size = 1; return MCDisassembler::Success;
  case 0x13: MI.setOpcode(MCS251::RRCA);    Size = 1; return MCDisassembler::Success;
  case 0x22: MI.setOpcode(MCS251::RET);     Size = 1; return MCDisassembler::Success;
  case 0x32: MI.setOpcode(MCS251::RETI);    Size = 1; return MCDisassembler::Success;
  case 0x33: MI.setOpcode(MCS251::RLCA);    Size = 1; return MCDisassembler::Success;
  case 0x73: MI.setOpcode(MCS251::JMPIAD);  Size = 1; return MCDisassembler::Success;
  case 0xA3: MI.setOpcode(MCS251::INCDPTR); Size = 1; return MCDisassembler::Success;
  case 0xA4: MI.setOpcode(MCS251::MULAB);   Size = 1; return MCDisassembler::Success;
  case 0xAA: MI.setOpcode(MCS251::ERET);    Size = 1; return MCDisassembler::Success;
  case 0xB3: MI.setOpcode(MCS251::CPLC);    Size = 1; return MCDisassembler::Success;
  case 0xC3: MI.setOpcode(MCS251::CLRC);    Size = 1; return MCDisassembler::Success;
  case 0xD3: MI.setOpcode(MCS251::SETBC);   Size = 1; return MCDisassembler::Success;
  case 0xE0: MI.setOpcode(MCS251::MOVXALD); Size = 1; return MCDisassembler::Success;
  case 0xE4: MI.setOpcode(MCS251::CLRA);    Size = 1; return MCDisassembler::Success;
  case 0xF0: MI.setOpcode(MCS251::MOVXAST); Size = 1; return MCDisassembler::Success;

  // Classic 16-bit CODE jump/call & MOV DPTR, #imm16.
  case 0x02: // ljmp addr16
  case 0x12: // lcall addr16
  {
    if (Bytes.size() < 3)
      return MCDisassembler::Fail;
    MI.setOpcode(Op == 0x02 ? MCS251::LJMP : MCS251::LCALL);
    const uint16_t Target16 = read16BE(Bytes, 1);
    const uint64_t FullTarget = (Address & 0xff0000ULL) | Target16;
    if (!tryAddingSymbolicOperand(MI, FullTarget, Address, /*IsBranch=*/true, 1,
                                  2, 3))
      MI.addOperand(MCOperand::createImm(Target16));
    Size = 3;
    return MCDisassembler::Success;
  }
  case 0x90: // mov dptr, #imm16
    if (Bytes.size() < 3)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOVDPTRri);
    MI.addOperand(MCOperand::createImm(read16BE(Bytes, 1)));
    Size = 3;
    return MCDisassembler::Success;

  // 24-bit absolute jump / call.
  case 0x8A: // ejmp addr24
  case 0x9A: // ecall addr24
  {
    if (Bytes.size() < 4)
      return MCDisassembler::Fail;
    MI.setOpcode(Op == 0x8A ? MCS251::EJMP : MCS251::ECALL);
    AddBranchTarget(read24BE(Bytes, 1), 1, 3, 4);
    Size = 4;
    return MCDisassembler::Success;
  }

  // Indirect call through DR: 99 (dr<<4)|8
  case 0x99: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t Spec = Bytes[1];
    if ((Spec & 0x0f) != 0x08)
      return MCDisassembler::Fail;
    MCRegister Reg = decodeDR(Spec >> 4);
    if (!Reg)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::ECALLr);
    MI.addOperand(MCOperand::createReg(Reg));
    Size = 2;
    return MCDisassembler::Success;
  }

  // Relative 8-bit branches.
  case 0x08: case 0x18: case 0x28: case 0x38:
  case 0x40: case 0x48: case 0x50: case 0x58:
  case 0x68: case 0x78: case 0x80: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    unsigned MCOp = 0;
    switch (Op) {
    case 0x08: MCOp = MCS251::JSLE; break;
    case 0x18: MCOp = MCS251::JSG;  break;
    case 0x28: MCOp = MCS251::JLE;  break;
    case 0x38: MCOp = MCS251::JG;   break;
    case 0x40: MCOp = MCS251::JC;   break;
    case 0x48: MCOp = MCS251::JSL;  break;
    case 0x50: MCOp = MCS251::JNC;  break;
    case 0x58: MCOp = MCS251::JSGE; break;
    case 0x68: MCOp = MCS251::JE;   break;
    case 0x78: MCOp = MCS251::JNE;  break;
    case 0x80: MCOp = MCS251::SJMP; break;
    }
    MI.setOpcode(MCOp);
    const int64_t Target =
        (static_cast<int64_t>(Address) + 2 + static_cast<int8_t>(Bytes[1])) &
        0xffffff;
    AddBranchTarget(Target, 1, 1, 2);
    Size = 2;
    return MCDisassembler::Success;
  }

  // Bit-addressed single-operand instructions.
  case 0x92: case 0xA2: case 0xB2: case 0xC2: case 0xD2: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    unsigned MCOp = 0;
    switch (Op) {
    case 0x92: MCOp = MCS251::MOVBITC; break;
    case 0xA2: MCOp = MCS251::MOVCBIT; break;
    case 0xB2: MCOp = MCS251::CPLBIT;  break;
    case 0xC2: MCOp = MCS251::CLRBIT;  break;
    case 0xD2: MCOp = MCS251::SETBBIT; break;
    }
    MI.setOpcode(MCOp);
    MI.addOperand(MCOperand::createImm(Bytes[1]));
    Size = 2;
    return MCDisassembler::Success;
  }

  // Bit-addressed conditional branches: Op bit rel8 (operand 0 = target, 1 = bit).
  case 0x10: case 0x20: case 0x30: {
    if (Bytes.size() < 3)
      return MCDisassembler::Fail;
    MI.setOpcode(Op == 0x10 ? MCS251::JBC
                            : (Op == 0x20 ? MCS251::JB : MCS251::JNB));
    const int64_t Target =
        (static_cast<int64_t>(Address) + 3 + static_cast<int8_t>(Bytes[2])) &
        0xffffff;
    AddBranchTarget(Target, 2, 1, 3);
    MI.addOperand(MCOperand::createImm(Bytes[1]));
    Size = 3;
    return MCDisassembler::Success;
  }

  // MOV A, #imm8 (0x74) and MOV dir8, #imm8 (0x75).
  case 0x74:
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOVAI);
    MI.addOperand(MCOperand::createImm(Bytes[1]));
    Size = 2;
    return MCDisassembler::Success;

  case 0x75:
    if (Bytes.size() < 3)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOVDIRI);
    MI.addOperand(MCOperand::createImm(Bytes[1]));
    MI.addOperand(MCOperand::createImm(Bytes[2]));
    Size = 3;
    return MCDisassembler::Success;

  // Classic direct PUSH / POP (0xC0 / 0xD0).
  case 0xC0:
  case 0xD0:
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    if (Bytes[1] == SFR_PSW) {
      MI.setOpcode(Op == 0xC0 ? MCS251::ISR_PUSH_PSW : MCS251::ISR_POP_PSW);
    } else {
      MI.setOpcode(Op == 0xC0 ? MCS251::PUSHDIR : MCS251::POPDIR);
      MI.addOperand(MCOperand::createImm(Bytes[1]));
    }
    Size = 2;
    return MCDisassembler::Success;

  // Native PUSH DR / POP DR: CA/DA (dr<<4)|0xB.
  case 0xCA:
  case 0xDA: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t Spec = Bytes[1];
    if ((Spec & 0x0f) != 0x0b)
      return MCDisassembler::Fail;
    MCRegister Reg = decodeDR(Spec >> 4);
    if (!Reg)
      return MCDisassembler::Fail;
    if (Reg == MCS251::DR56) {
      MI.setOpcode(Op == 0xCA ? MCS251::ISR_PUSH_DPX : MCS251::ISR_POP_DPX);
    } else {
      MI.setOpcode(Op == 0xCA ? MCS251::PUSHDR : MCS251::POPDR);
      MI.addOperand(MCOperand::createReg(Reg));
    }
    Size = 2;
    return MCDisassembler::Success;
  }

  // Native 1-bit shifts: 0E (sra), 1E (srl), 3E (sll).
  case 0x0E:
  case 0x1E:
  case 0x3E: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t Spec = Bytes[1];
    const uint8_t Mode = Spec & 0x0f;
    const uint8_t Code = Spec >> 4;
    if (Mode == 0x00) {
      MCRegister Reg = decodeR(Code);
      MI.setOpcode(Op == 0x0E ? MCS251::SRA8
                              : (Op == 0x1E ? MCS251::SRL8 : MCS251::SLL8));
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createReg(Reg));
      Size = 2;
      return MCDisassembler::Success;
    }
    if (Mode == 0x04) {
      MCRegister Reg = decodeWR(Code);
      MI.setOpcode(Op == 0x0E ? MCS251::SRA16
                              : (Op == 0x1E ? MCS251::SRL16 : MCS251::SLL16));
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createReg(Reg));
      Size = 2;
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  // Register-register moves (7C byte, 7D word, 7F dword).
  case 0x7C: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const unsigned DstCode = Bytes[1] >> 4;
    const unsigned SrcCode = Bytes[1] & 0x0f;
    if (DstCode == 11 && SrcCode >= 8) {
      MI.setOpcode(MCS251::MOV8a);
      MI.addOperand(MCOperand::createReg(decodeR(SrcCode)));
    } else if (SrcCode == 11 && DstCode >= 8) {
      MI.setOpcode(MCS251::MOV8ra);
      MI.addOperand(MCOperand::createReg(decodeR(DstCode)));
    } else {
      MI.setOpcode(MCS251::MOV8rr);
      MI.addOperand(MCOperand::createReg(decodeR(DstCode)));
      MI.addOperand(MCOperand::createReg(decodeR(SrcCode)));
    }
    Size = 2;
    return MCDisassembler::Success;
  }
  case 0x7D: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOV16rr);
    MI.addOperand(MCOperand::createReg(decodeWR(Bytes[1] >> 4)));
    MI.addOperand(MCOperand::createReg(decodeWR(Bytes[1] & 0x0f)));
    Size = 2;
    return MCDisassembler::Success;
  }
  case 0x7F: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MCRegister Dst = decodeDR(Bytes[1] >> 4);
    MCRegister Src = decodeDR(Bytes[1] & 0x0f);
    if (!Dst || !Src)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOV32rr);
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(Src));
    Size = 2;
    return MCDisassembler::Success;
  }

  // Native ALU / MUL register-register (tied 2-address: dst, dst, src).
  case 0x2C: case 0x4C: case 0x5C: case 0x6C: case 0x9C: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const unsigned DstCode = Bytes[1] >> 4;
    const unsigned SrcCode = Bytes[1] & 0x0f;
    MCRegister Dst = decodeR(DstCode);
    if (Op == 0x4C && SrcCode == 11) {
      MI.setOpcode(MCS251::OR8a);
      MI.addOperand(MCOperand::createReg(Dst));
      MI.addOperand(MCOperand::createReg(Dst));
      Size = 2;
      return MCDisassembler::Success;
    }
    unsigned MCOp = 0;
    switch (Op) {
    case 0x2C: MCOp = MCS251::ADD8rr; break;
    case 0x4C: MCOp = MCS251::OR8rr;  break;
    case 0x5C: MCOp = MCS251::AND8rr; break;
    case 0x6C: MCOp = MCS251::XOR8rr; break;
    case 0x9C: MCOp = MCS251::SUB8rr; break;
    }
    MI.setOpcode(MCOp);
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(decodeR(SrcCode)));
    Size = 2;
    return MCDisassembler::Success;
  }
  case 0x2D: case 0x4D: case 0x5D: case 0x6D: case 0x9D: case 0xAD: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MCRegister Dst = decodeWR(Bytes[1] >> 4);
    MCRegister Src = decodeWR(Bytes[1] & 0x0f);
    unsigned MCOp = 0;
    switch (Op) {
    case 0x2D: MCOp = MCS251::ADD16rr; break;
    case 0x4D: MCOp = MCS251::OR16rr;  break;
    case 0x5D: MCOp = MCS251::AND16rr; break;
    case 0x6D: MCOp = MCS251::XOR16rr; break;
    case 0x9D: MCOp = MCS251::SUB16rr; break;
    case 0xAD: MCOp = MCS251::MUL16rr; break;
    }
    MI.setOpcode(MCOp);
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(Src));
    Size = 2;
    return MCDisassembler::Success;
  }
  case 0x2F: case 0x9F: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MCRegister Dst = decodeDR(Bytes[1] >> 4);
    MCRegister Src = decodeDR(Bytes[1] & 0x0f);
    if (!Dst || !Src)
      return MCDisassembler::Fail;
    MI.setOpcode(Op == 0x2F ? MCS251::ADD32rr : MCS251::SUB32rr);
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(Dst));
    MI.addOperand(MCOperand::createReg(Src));
    Size = 2;
    return MCDisassembler::Success;
  }

  // Compares (register-register: no tied output).
  case 0xBC:
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::CMP8rr);
    MI.addOperand(MCOperand::createReg(decodeR(Bytes[1] >> 4)));
    MI.addOperand(MCOperand::createReg(decodeR(Bytes[1] & 0x0f)));
    Size = 2;
    return MCDisassembler::Success;
  case 0xBD:
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::CMP16rr);
    MI.addOperand(MCOperand::createReg(decodeWR(Bytes[1] >> 4)));
    MI.addOperand(MCOperand::createReg(decodeWR(Bytes[1] & 0x0f)));
    Size = 2;
    return MCDisassembler::Success;
  case 0xBF: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    MCRegister Lhs = decodeDR(Bytes[1] >> 4);
    MCRegister Rhs = decodeDR(Bytes[1] & 0x0f);
    if (!Lhs || !Rhs)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::CMP32rr);
    MI.addOperand(MCOperand::createReg(Lhs));
    MI.addOperand(MCOperand::createReg(Rhs));
    Size = 2;
    return MCDisassembler::Success;
  }

  // Native ALU and CMP immediates (8-bit mode 0, 16-bit mode 4).
  case 0x2E: case 0x4E: case 0x5E: case 0x6E: case 0x9E: case 0xBE: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t Spec = Bytes[1];
    const uint8_t Mode = Spec & 0x0f;
    const uint8_t Code = Spec >> 4;
    if (Mode == 0x00) {
      if (Bytes.size() < 3)
        return MCDisassembler::Fail;
      MCRegister Reg = decodeR(Code);
      if (Op == 0xBE) {
        MI.setOpcode(MCS251::CMP8ri);
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createImm(Bytes[2]));
      } else {
        unsigned MCOp = 0;
        switch (Op) {
        case 0x2E: MCOp = MCS251::ADD8ri; break;
        case 0x4E: MCOp = MCS251::OR8ri;  break;
        case 0x5E: MCOp = MCS251::AND8ri; break;
        case 0x6E: MCOp = MCS251::XOR8ri; break;
        case 0x9E: MCOp = MCS251::SUB8ri; break;
        }
        MI.setOpcode(MCOp);
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createImm(Bytes[2]));
      }
      Size = 3;
      return MCDisassembler::Success;
    }
    if (Mode == 0x04) {
      if (Bytes.size() < 4)
        return MCDisassembler::Fail;
      MCRegister Reg = decodeWR(Code);
      const uint16_t Imm16 = read16BE(Bytes, 2);
      if (Op == 0xBE) {
        MI.setOpcode(MCS251::CMP16ri);
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createImm(Imm16));
      } else {
        unsigned MCOp = 0;
        switch (Op) {
        case 0x2E: MCOp = MCS251::ADD16ri; break;
        case 0x4E: MCOp = MCS251::OR16ri;  break;
        case 0x5E: MCOp = MCS251::AND16ri; break;
        case 0x6E: MCOp = MCS251::XOR16ri; break;
        case 0x9E: MCOp = MCS251::SUB16ri; break;
        }
        MI.setOpcode(MCOp);
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createImm(Imm16));
      }
      Size = 4;
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  // Displaced memory loads/stores (4 bytes).
  case 0x09: // mov rD, @wrS+dis16
    if (Bytes.size() < 4)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOV8rmD);
    MI.addOperand(MCOperand::createReg(decodeR(Bytes[1] >> 4)));
    MI.addOperand(MCOperand::createReg(decodeWR(Bytes[1] & 0x0f)));
    MI.addOperand(MCOperand::createImm(read16BE(Bytes, 2)));
    Size = 4;
    return MCDisassembler::Success;
  case 0x19: // mov @wrD+dis16, rS
    if (Bytes.size() < 4)
      return MCDisassembler::Fail;
    MI.setOpcode(MCS251::MOV8mrD);
    MI.addOperand(MCOperand::createReg(decodeWR(Bytes[1] & 0x0f)));
    MI.addOperand(MCOperand::createImm(read16BE(Bytes, 2)));
    MI.addOperand(MCOperand::createReg(decodeR(Bytes[1] >> 4)));
    Size = 4;
    return MCDisassembler::Success;
  case 0x29: // mov rD, @drS+dis16
  case 0x69: // mov wrD, @drS+dis16
  {
    if (Bytes.size() < 4)
      return MCDisassembler::Fail;
    MCRegister Base = decodeDR(Bytes[1] & 0x0f);
    if (!Base)
      return MCDisassembler::Fail;
    MI.setOpcode(Op == 0x29 ? MCS251::MOV8rmP : MCS251::MOV16rmS);
    MI.addOperand(MCOperand::createReg(Op == 0x29 ? decodeR(Bytes[1] >> 4)
                                                  : decodeWR(Bytes[1] >> 4)));
    MI.addOperand(MCOperand::createReg(Base));
    MI.addOperand(MCOperand::createImm(static_cast<int16_t>(read16BE(Bytes, 2))));
    Size = 4;
    return MCDisassembler::Success;
  }
  case 0x39: // mov @drD+dis16, rS
  case 0x79: // mov @drD+dis16, wrS
  {
    if (Bytes.size() < 4)
      return MCDisassembler::Fail;
    MCRegister Base = decodeDR(Bytes[1] & 0x0f);
    if (!Base)
      return MCDisassembler::Fail;
    MI.setOpcode(Op == 0x39 ? MCS251::MOV8mrP : MCS251::MOV16mrS);
    MI.addOperand(MCOperand::createReg(Base));
    MI.addOperand(MCOperand::createImm(static_cast<int16_t>(read16BE(Bytes, 2))));
    MI.addOperand(MCOperand::createReg(Op == 0x39 ? decodeR(Bytes[1] >> 4)
                                                  : decodeWR(Bytes[1] >> 4)));
    Size = 4;
    return MCDisassembler::Success;
  }

  // 0x0B (INC / MOV wr, @dr) and 0x1B (DEC / MOV @dr, wr).
  case 0x0B:
  case 0x1B: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const bool IsDec = Op == 0x1B;
    const uint8_t Spec = Bytes[1];
    const uint8_t Code = Spec >> 4;
    const uint8_t Mode = Spec & 0x0f;
    static constexpr uint8_t StepTab[3] = {1, 2, 4};
    if (Mode <= 2) {
      MCRegister Reg = decodeR(Code);
      MI.setOpcode(IsDec ? MCS251::DEC8ri : MCS251::INC8ri);
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createImm(StepTab[Mode]));
      Size = 2;
      return MCDisassembler::Success;
    }
    if (Mode >= 4 && Mode <= 6) {
      MCRegister Reg = decodeWR(Code);
      MI.setOpcode(IsDec ? MCS251::DEC16ri : MCS251::INC16ri);
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createImm(StepTab[Mode - 4]));
      Size = 2;
      return MCDisassembler::Success;
    }
    if (Mode >= 0xC && Mode <= 0xE) {
      MCRegister Reg = decodeDR(Code);
      if (!Reg)
        return MCDisassembler::Fail;
      if (Reg == MCS251::DR60) {
        if (Mode == 0xC)
          MI.setOpcode(IsDec ? MCS251::DECSPX1 : MCS251::INCSPX1);
        else if (Mode == 0xD)
          MI.setOpcode(IsDec ? MCS251::DECSPX2 : MCS251::INCSPX2);
        else
          MI.setOpcode(IsDec ? MCS251::DECSPX4 : MCS251::INCSPX4);
      } else {
        MI.setOpcode(IsDec ? MCS251::DEC32ri : MCS251::INC32ri);
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createReg(Reg));
        MI.addOperand(MCOperand::createImm(StepTab[Mode - 0xC]));
      }
      Size = 2;
      return MCDisassembler::Success;
    }
    if (Mode == 0x0A) {
      if (Bytes.size() < 3)
        return MCDisassembler::Fail;
      MCRegister Base = decodeDR(Code);
      if (!Base || (Bytes[2] & 0x0f) != 0)
        return MCDisassembler::Fail;
      MCRegister WReg = decodeWR(Bytes[2] >> 4);
      if (!IsDec) {
        MI.setOpcode(MCS251::MOV16rmS);
        MI.addOperand(MCOperand::createReg(WReg));
        MI.addOperand(MCOperand::createReg(Base));
        MI.addOperand(MCOperand::createImm(0));
      } else {
        MI.setOpcode(MCS251::MOV16mrS);
        MI.addOperand(MCOperand::createReg(Base));
        MI.addOperand(MCOperand::createImm(0));
        MI.addOperand(MCOperand::createReg(WReg));
      }
      Size = 3;
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  // 0x7E: MOV reg, #imm / dir8 / @wr / @dr.
  case 0x7E: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t Spec = Bytes[1];
    const uint8_t Code = Spec >> 4;
    const uint8_t Mode = Spec & 0x0f;
    switch (Mode) {
    case 0x00: // mov rN, #imm8
      if (Bytes.size() < 3)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOV8ri);
      MI.addOperand(MCOperand::createReg(decodeR(Code)));
      MI.addOperand(MCOperand::createImm(Bytes[2]));
      Size = 3;
      return MCDisassembler::Success;
    case 0x01: // mov rN, dir8 (including dpl/dph/b)
      if (Bytes.size() < 3)
        return MCDisassembler::Fail;
      if (Bytes[2] == SFR_DPL) {
        MI.setOpcode(MCS251::MOV8rdpl);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      } else if (Bytes[2] == SFR_DPH) {
        MI.setOpcode(MCS251::MOV8rdph);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      } else if (Bytes[2] == SFR_B) {
        MI.setOpcode(MCS251::MOV8rb);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      } else {
        MI.setOpcode(MCS251::MOV8di);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
        MI.addOperand(MCOperand::createImm(Bytes[2]));
      }
      Size = 3;
      return MCDisassembler::Success;
    case 0x04: // mov wrN, #imm16
      if (Bytes.size() < 4)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOV16ri);
      MI.addOperand(MCOperand::createReg(decodeWR(Code)));
      MI.addOperand(MCOperand::createImm(read16BE(Bytes, 2)));
      Size = 4;
      return MCDisassembler::Success;
    case 0x08: { // mov drN, #imm16
      if (Bytes.size() < 4)
        return MCDisassembler::Fail;
      MCRegister Reg = decodeDR(Code);
      if (!Reg)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOVDRri);
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createImm(read16BE(Bytes, 2)));
      Size = 4;
      return MCDisassembler::Success;
    }
    case 0x09: // mov rD, @wrS
      if (Bytes.size() < 3 || (Bytes[2] & 0x0f) != 0)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOV8rm);
      MI.addOperand(MCOperand::createReg(decodeR(Bytes[2] >> 4)));
      MI.addOperand(MCOperand::createReg(decodeWR(Code)));
      Size = 3;
      return MCDisassembler::Success;
    case 0x0B: { // mov rD, @drS
      if (Bytes.size() < 3 || (Bytes[2] & 0x0f) != 0)
        return MCDisassembler::Fail;
      MCRegister Base = decodeDR(Code);
      if (!Base)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOV8rmP);
      MI.addOperand(MCOperand::createReg(decodeR(Bytes[2] >> 4)));
      MI.addOperand(MCOperand::createReg(Base));
      MI.addOperand(MCOperand::createImm(0));
      Size = 3;
      return MCDisassembler::Success;
    }
    default:
      return MCDisassembler::Fail;
    }
  }

  // 0x7A: MOV dir8/@wr/@dr, reg and MOVH dr, #imm16.
  case 0x7A: {
    if (Bytes.size() < 2)
      return MCDisassembler::Fail;
    const uint8_t Spec = Bytes[1];
    const uint8_t Code = Spec >> 4;
    const uint8_t Mode = Spec & 0x0f;
    switch (Mode) {
    case 0x01: // mov dir8, rS (including dpl/dph/b)
      if (Bytes.size() < 3)
        return MCDisassembler::Fail;
      if (Bytes[2] == SFR_DPL) {
        MI.setOpcode(MCS251::MOV8dpl);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      } else if (Bytes[2] == SFR_DPH) {
        MI.setOpcode(MCS251::MOV8dph);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      } else if (Bytes[2] == SFR_B) {
        MI.setOpcode(MCS251::MOV8b);
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      } else {
        MI.setOpcode(MCS251::MOV8id);
        MI.addOperand(MCOperand::createImm(Bytes[2]));
        MI.addOperand(MCOperand::createReg(decodeR(Code)));
      }
      Size = 3;
      return MCDisassembler::Success;
    case 0x09: // mov @wrD, rS
      if (Bytes.size() < 3 || (Bytes[2] & 0x0f) != 0)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOV8mr);
      MI.addOperand(MCOperand::createReg(decodeWR(Code)));
      MI.addOperand(MCOperand::createReg(decodeR(Bytes[2] >> 4)));
      Size = 3;
      return MCDisassembler::Success;
    case 0x0B: { // mov @drD, rS
      if (Bytes.size() < 3 || (Bytes[2] & 0x0f) != 0)
        return MCDisassembler::Fail;
      MCRegister Base = decodeDR(Code);
      if (!Base)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOV8mrP);
      MI.addOperand(MCOperand::createReg(Base));
      MI.addOperand(MCOperand::createImm(0));
      MI.addOperand(MCOperand::createReg(decodeR(Bytes[2] >> 4)));
      Size = 3;
      return MCDisassembler::Success;
    }
    case 0x0C: { // movh drN, #imm16 (tied $old = $dst)
      if (Bytes.size() < 4)
        return MCDisassembler::Fail;
      MCRegister Reg = decodeDR(Code);
      if (!Reg)
        return MCDisassembler::Fail;
      MI.setOpcode(MCS251::MOVHDRi);
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createReg(Reg));
      MI.addOperand(MCOperand::createImm(read16BE(Bytes, 2)));
      Size = 4;
      return MCDisassembler::Success;
    }
    default:
      return MCDisassembler::Fail;
    }
  }

  default:
    return MCDisassembler::Fail;
  }
}

} // namespace

static MCDisassembler *createMCS251Disassembler(const Target &T,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  return new MCS251Disassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheMCS251Target(),
                                         createMCS251Disassembler);
}
