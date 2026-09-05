//===-- MCS251FrameLowering.cpp - MCS-251 frame lowering -----------------===//
//
// Frame model (measurement-verified, see the Phase 9 block comment in
// MCS251InstrInfo.td): the stack grows UP and SPX points at the top-most
// used byte. ecall has already pushed the 3-byte return address, so a
// frame's objects live at [SPX_entry+1 .. SPX_entry+StackSize] after the
// prologue's `inc spx,#StackSize`.
//
// PEI offsets: TargetFrameLowering is constructed StackGrowsUp with
// LocalAreaOffset = 1, so PEI lays objects out at offsets O in [1, StackSize]
// counting from the function-entry SPX. An object's address is therefore
// SPX_entry + O = SPX_after + (O - StackSize): a NEGATIVE displacement from
// the post-prologue SPX, matching the SDCC `@spx-0x....` access style that
// eliminateFrameIndex produces.
//
// Variable-sized objects (dynamic allocas) adjust SPX at the alloca site
// (LowerDYNAMIC_STACKALLOC), which would invalidate every static
// displacement. Such functions set hasFP(): the frame top is anchored in
// reserved dr16 right after the prologue, all frame references switch to
// @dr16-relative displacements (same O - StackSize formula -- the anchor is
// taken after `push dr16`, so anchor and concept addresses both shift by 4),
// and the epilogue restores SPX from the anchor before `dec spx`/`eret`.
// eret pops [SPX-2..SPX] (QEMU mcs251_return_extended), which is why SPX
// must be EXACTLY restored -- a dynamic alloca without restore would make
// eret pop alloca garbage as the return address.

#include "MCS251FrameLowering.h"
#include "MCS251.h"
#include "MCS251InstrInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

MCS251FrameLowering::MCS251FrameLowering()
    // StackGrowsUp: THE platform direction (upstream templates like
    // MSP430/AVR all grow down; every offset decision below is its mirror).
    // Stack alignment 1: byte-addressed memory, every legal access width is
    // a sequence of byte moves, the DataLayout agrees (S8). LocalAreaOffset
    // 1: the entry SPX byte itself holds the return address, so PEI must
    // start object offsets at +1, not 0.
    : TargetFrameLowering(StackGrowsUp, Align(1), 1, Align(1)) {}

bool MCS251FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return MF.getFrameInfo().hasVarSizedObjects();
}

void MCS251FrameLowering::emitSPAdjust(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       const DebugLoc &DL, uint64_t Amount,
                                       bool IsDec,
                                       MachineInstr::MIFlag Flag) const {
  const MCS251InstrInfo &TII = *static_cast<const MCS251InstrInfo *>(
      MBB.getParent()->getSubtarget().getInstrInfo());
  unsigned Step4 = IsDec ? MCS251::DECSPX4 : MCS251::INCSPX4;
  unsigned Step2 = IsDec ? MCS251::DECSPX2 : MCS251::INCSPX2;
  unsigned Step1 = IsDec ? MCS251::DECSPX1 : MCS251::INCSPX1;
  for (unsigned I = 0; I < Amount / 4; ++I)
    BuildMI(MBB, MI, DL, TII.get(Step4)).setMIFlag(Flag);
  if (Amount % 4 >= 2)
    BuildMI(MBB, MI, DL, TII.get(Step2)).setMIFlag(Flag);
  if (Amount % 4 == 1 || Amount % 4 == 3)
    BuildMI(MBB, MI, DL, TII.get(Step1)).setMIFlag(Flag);
}

void MCS251FrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  assert(&MF.front() == &MBB && "Shrink-wrapping not supported");
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCS251InstrInfo &TII = *static_cast<const MCS251InstrInfo *>(
      MF.getSubtarget().getInstrInfo());

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  uint64_t StackSize = MFI.getStackSize();
  bool FramePtr = hasFP(MF);

  if (FramePtr) {
    // Private anchor convention: LLVM reserves DR16; the supported SDCC
    // port never allocates R16-R31. Only LLVM var-sized functions use DR16,
    // and they save/restore it for nested calls. Handwritten callees must
    // preserve DR16 too. DPX/DR56 is unsuitable: DPL/DPH alias it and both
    // argument reads and call-result writes would destroy a DPX anchor.
    BuildMI(MBB, MBBI, DL, TII.get(MCS251::PUSHFP))
        .setMIFlag(MachineInstr::FrameSetup);
  }

  if (StackSize)
    emitSPAdjust(MBB, MBBI, DL, StackSize, /*IsDec=*/false,
                 MachineInstr::FrameSetup);

  if (FramePtr) {
    // Anchor the frame top AFTER the push and the allocation. From here on
    // eliminateFrameIndex references the frame through dr16.
    BuildMI(MBB, MBBI, DL, TII.get(MCS251::SETFP))
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

void MCS251FrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCS251InstrInfo &TII = *static_cast<const MCS251InstrInfo *>(
      MF.getSubtarget().getInstrInfo());

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  assert(MBBI->getOpcode() == MCS251::ERET &&
         "Can only insert epilogue into returning blocks");
  DebugLoc DL = MBBI->getDebugLoc();
  uint64_t StackSize = MFI.getStackSize();
  bool FramePtr = hasFP(MF);

  if (FramePtr) {
    // Restore SPX from the anchor in one step (this also discards any
    // dynamic-alloca offset), landing on the frame top as it was after the
    // prologue.
    BuildMI(MBB, MBBI, DL, TII.get(MCS251::RESTORESP))
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  if (StackSize)
    emitSPAdjust(MBB, MBBI, DL, StackSize, /*IsDec=*/true,
                 MachineInstr::FrameDestroy);

  if (FramePtr) {
    // Restore the caller's anchor; SPX ends at the function-entry value,
    // so eret pops the return address saved by ecall.
    BuildMI(MBB, MBBI, DL, TII.get(MCS251::POPFP))
        .setMIFlag(MachineInstr::FrameDestroy);
  }
}

StackOffset MCS251FrameLowering::getFrameIndexReference(
    const MachineFunction &MF, int FI, Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // The frame reference base: SPX (post-prologue frame top) normally, the
  // dr16 anchor when dynamic allocas move SPX. This mirrors
  // getFrameRegister(), kept in sync for the generic passes that consult
  // that one.
  FrameReg = MFI.hasVarSizedObjects() ? Register(MCS251::DR16)
                                      : Register(MCS251::DR60);
  // Stack-grows-up mirror of the default (down-growing) formula: the base
  // points at the frame TOP while the objects start one byte above the
  // entry SPX, hence ObjectOffset - StackSize (negative or zero, within
  // [1 - StackSize, 0]).
  return StackOffset::getFixed((int64_t)MFI.getObjectOffset(FI) -
                               (int64_t)MFI.getStackSize());
}
