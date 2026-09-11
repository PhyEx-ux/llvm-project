//===-- MCS251RegisterInfo.cpp - MCS-251 register information ------------===//

#include "MCS251RegisterInfo.h"
#include "MCS251.h"
#include "MCS251FrameLowering.h"
#include "MCS251InstrInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "MCS251GenRegisterInfo.inc"

MCS251RegisterInfo::MCS251RegisterInfo(unsigned PointerBits)
    : MCS251GenRegisterInfo(0), PointerBits(PointerBits) {
  assert((PointerBits == 16 || PointerBits == 32) &&
         "unexpected MCS251 pointer width");
}

const MCPhysReg *
MCS251RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  static const MCPhysReg NoCalleeSaved[] = {0};
  return NoCalleeSaved;
}

// The caller-side view of the same ABI fact: which registers survive an
// ecall. CSR_MCS251 (MCS251RegisterInfo.td) keeps the callee-saved list
// empty -- everything is caller-saved -- with dr60 (spx) as the sole
// OtherPreserved register, so the generated CSR_MCS251_RegMask preserves
// exactly one register (DR60) and its subregisters -- seven bits: DR60,
// R60-R63 and WR60/WR62. LowerCall attaches this mask to the
// MCS251ISD::CALL node.
const uint32_t *MCS251RegisterInfo::getCallPreservedMask(
    const MachineFunction &MF, CallingConv::ID CC) const {
  switch (CC) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    // Fast deliberately shares the C physical ABI, including its clobbers.
    return CSR_MCS251_RegMask;
  }
}

BitVector MCS251RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // DR60 (alias spx) is the extended stack pointer. DR56 (alias dpx) is the
  // extended data pointer, reserved for future indirect addressing use. The
  // alias machinery keeps wr56-wr62 and r56-r63 unallocatable as well.
  Reserved.set(MCS251::DR60);
  Reserved.set(MCS251::DR56);
  // Dynamic-frame anchor. Unlike DPX it does not alias DPL/DPH arguments.
  Reserved.set(MCS251::DR16);
  // DPL/DPH are the ABI return-value locations and DPTR is their 16-bit
  // overlay; none of them belong to an allocatable register class. Reserve
  // them anyway so that no allocator can ever hand them out.
  Reserved.set(MCS251::DPL);
  Reserved.set(MCS251::DPH);
  Reserved.set(MCS251::DPTR);
  // DPXL (SFR 0x84) is the MOVX @DPTR region register re-pointed by every
  // AS3 access sequence (X2-1). Class-less like DPL/DPH, reserved the same
  // way; the backend never assumes the region survives any event.
  Reserved.set(MCS251::DPXL);
  // A (ACC, SFR 0xe0) and B (SFR 0xf0) are fixed ABI byte locations.
  // They intentionally remain separate from their R11/R10 byte aliases.
  Reserved.set(MCS251::A);
  Reserved.set(MCS251::B);
  // PSW is the virtual 8-bit flags register (CY/AC/OV/N/Z; see
  // MCS251RegisterInfo.td for the PSW/PSW1 physical-layout ruling). It is a
  // member of no register class and is only ever referenced implicitly via
  // Defs/Uses. It is call-clobbered.
  Reserved.set(MCS251::PSW);
  return Reserved;
}

const TargetRegisterClass *
MCS251RegisterInfo::getPointerRegClass(unsigned Kind) const {
  return PointerBits == 16 ? &MCS251::GPR16RegClass : &MCS251::GPR32RegClass;
}

// Frame-index elimination (Phase 9).
//
// While unresolved, a frame-relative instruction carries its stack address
// as one EXTRA operand pair over the eliminated form: the displacement slot
// of the mcs251_stack operand holds (FrameIndex, offset) instead of a bare
// immediate (upstream FI convention -- the offset operand is folded away
// here). Two instruction shapes reach this code:
//
//   MOV8rmF/MOV8mrF/MOV16rmF/MOV16mrF  [.., base, FI, off, ..]
//   ADD16fi                            [dst, lhs, FI, off]
//
// The final displacement is
//
//   dis = (ObjectOffset - StackSize) + off
//
// (getFrameIndexReference; negative for a stack growing up, see
// MCS251FrameLowering). Signed-16-bit range only: the @DRk displacement
// encoding and the `add wr,#imm16` immediate are both 16-bit, so an
// out-of-range frame rejects loudly instead of silently truncating. A
// large-frame scheme (dpx base + materialised offset) is future work.
bool MCS251RegisterInfo::eliminateFrameIndex(
    MachineBasicBlock::iterator II, int SPAdj, unsigned FIOperandNum,
    RegScavenger *RS) const {
  assert(SPAdj == 0 && "MCS251 has no SP-adjusting call sequences");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const TargetFrameLowering *TFL = MF.getSubtarget().getFrameLowering();

  int64_t Off = MI.getOperand(FIOperandNum + 1).getImm();
  int FI = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  int64_t Dis = TFL->getFrameIndexReference(MF, FI, FrameReg).getFixed() + Off;
  if (!isInt<16>(Dis))
    report_fatal_error("MCS251: frame offset out of the signed 16-bit "
                       "displacement range (large frames need a dpx-based "
                       "addressing scheme, not yet supported)");

  switch (MI.getOpcode()) {
  default:
    report_fatal_error("MCS251: unknown instruction with a frame index");
  case MCS251::MOV32rmF:
  case MCS251::MOV32mrF: {
    // Keep a full DR definition/use throughout allocation. Only now, with
    // physical registers and final offsets, expose the native WR lanes.
    if (!isInt<16>(Dis + 2))
      report_fatal_error("MCS251: i32 spill exceeds signed16 frame displacement");
    bool Load = MI.getOpcode() == MCS251::MOV32rmF;
    const MachineOperand &Value = MI.getOperand(Load ? 0 : 3);
    Register DR = Value.getReg();
    assert(DR.isPhysical() && "frame elimination must follow allocation");
    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
    for (unsigned I = 0; I < 2; ++I) {
      Register WR = getSubReg(DR, I == 0 ? MCS251::sub_hi16 : MCS251::sub_lo16);
      MachineInstrBuilder MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
          TII->get(Load ? MCS251::MOV16rmS : MCS251::MOV16mrS));
      if (Load)
        MIB.addReg(WR, RegState::Define | getDeadRegState(Value.isDead()));
      MIB.addReg(FrameReg).addImm(Dis + 2 * I);
      if (!Load)
        MIB.addReg(WR, getKillRegState(Value.isKill()));
      for (MachineMemOperand *MMO : MI.memoperands())
        MIB.addMemOperand(MF.getMachineMemOperand(MMO, 2 * I, /*Size=*/2));
      MIB.setMIFlags(MI.getFlags());
    }
    MI.eraseFromParent();
    return true;
  }
  case MCS251::MOV8rmF:
  case MCS251::MOV8mrF:
  case MCS251::MOV16rmF:
  case MCS251::MOV16mrF: {
    unsigned Opc;
    switch (MI.getOpcode()) {
    case MCS251::MOV8rmF: Opc = MCS251::MOV8rmS; break;
    case MCS251::MOV8mrF: Opc = MCS251::MOV8mrS; break;
    case MCS251::MOV16rmF: Opc = MCS251::MOV16rmS; break;
    default: Opc = MCS251::MOV16mrS; break;
    }
    MI.setDesc(MF.getSubtarget().getInstrInfo()->get(Opc));
    // [.., base(placeholder dr60), FI, off, ..] -> [.., FrameReg, dis, ..]
    assert(MI.getOperand(FIOperandNum - 1).isReg() &&
           "frame base must precede the frame-index operand");
    MI.getOperand(FIOperandNum - 1).ChangeToRegister(FrameReg, /*isDef=*/false);
    MI.getOperand(FIOperandNum).ChangeToImmediate(Dis);
    MI.removeOperand(FIOperandNum + 1);
    return false;
  }
  case MCS251::ADD16fi:
    // Frame-index pointer materialisation: morph into the plain immediate
    // form. add wr,#imm16 wraps mod 2^16, which is the correct region-00
    // behaviour for the negative displacement (Phase 8 ruling). This path
    // reads SPX at runtime, so it is only valid while SPX is the frame
    // reference -- a var-sized function would need to materialise the dr16
    // anchor (not yet implemented).
    if (MF.getFrameInfo().hasVarSizedObjects())
      report_fatal_error("MCS251: taking the address of a frame object in a "
                         "function with dynamic allocas is not supported "
                         "(needs an anchor read, not yet implemented)");
    MI.setDesc(
        MF.getSubtarget().getInstrInfo()->get(MCS251::ADD16ri));
    MI.getOperand(FIOperandNum).ChangeToImmediate(Dis & 0xffff);
    MI.removeOperand(FIOperandNum + 1);
    return false;
  }
}

Register
MCS251RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // Mirrors MCS251FrameLowering::getFrameIndexReference: SPX is the frame
  // reference unless dynamic allocas moved it, in which case the dr56
  // anchor holds the stable frame top.
  return MF.getFrameInfo().hasVarSizedObjects() ? MCS251::DR16
                                                : MCS251::DR60;
}
