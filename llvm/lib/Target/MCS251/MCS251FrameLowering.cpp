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
#include "llvm/IR/CallingConv.h"

using namespace llvm;

// ISR campaign T05: the A6 fixed save/restore program. Each entry is the
// T04 fixed opcode pair member in A6 order; the pop list is the EXACT
// inverse of the push list, ending with pop psw so that it lands directly
// before the RETI terminator (PSW restored last).
static const unsigned ISRPushSequence[] = {
    MCS251::ISR_PUSH_PSW, MCS251::ISR_PUSH_DR0,  MCS251::ISR_PUSH_DR4,
    MCS251::ISR_PUSH_DR8, MCS251::ISR_PUSH_DR12, MCS251::ISR_PUSH_DR16,
    MCS251::ISR_PUSH_DR20, MCS251::ISR_PUSH_DR24, MCS251::ISR_PUSH_DR28,
    MCS251::ISR_PUSH_DPX};
static const unsigned ISRPopSequence[] = {
    MCS251::ISR_POP_DPX,  MCS251::ISR_POP_DR28, MCS251::ISR_POP_DR24,
    MCS251::ISR_POP_DR20, MCS251::ISR_POP_DR16, MCS251::ISR_POP_DR12,
    MCS251::ISR_POP_DR8,  MCS251::ISR_POP_DR4,  MCS251::ISR_POP_DR0,
    MCS251::ISR_POP_PSW};

// The asynchronous interrupted context is REAL entry state: every push reads
// the register it saves (T04 Uses; never marked undef, never pseudo-defined),
// so the entry block must honestly carry these as live-ins. A and B are
// separately modelled SFRs (no register-file alias to DR8), and DPL/DPH/DPTR
// are separately modelled aliases of the DPX overlay -- each is read by the
// corresponding push and therefore listed itself.
static const MCPhysReg ISRAsyncLiveIns[] = {
    MCS251::PSW,  MCS251::DR0,  MCS251::DR4,  MCS251::DR8,  MCS251::DR12,
    MCS251::DR16, MCS251::DR20, MCS251::DR24, MCS251::DR28, MCS251::DR56,
    MCS251::A,    MCS251::B,    MCS251::DPL,  MCS251::DPH,  MCS251::DPTR};

static bool isISRFunction(const MachineFunction &MF) {
  return MF.getFunction().getCallingConv() == CallingConv::MCS251_INTR;
}

MCS251FrameLowering::MCS251FrameLowering()
    // StackGrowsUp: THE platform direction (upstream templates like
    // MSP430/AVR all grow down; every offset decision below is its mirror).
    // Stack alignment 1: byte-addressed memory, every legal access width is
    // a sequence of byte moves, the DataLayout agrees (S8). LocalAreaOffset
    // 1: the entry SPX byte itself holds the return address, so PEI must
    // start object offsets at +1, not 0.
    : TargetFrameLowering(StackGrowsUp, Align(1), 1, Align(1)) {}

MachineBasicBlock::iterator MCS251FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  // CALLSEQ protects static-parameter setup; it never adjusts the stack.
  assert(MI->getOperand(0).getImm() == 0 &&
         MI->getOperand(1).getImm() == 0 && "unexpected stack arguments");
  return MBB.erase(MI);
}

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
  bool IsISR = isISRFunction(MF);

  if (IsISR) {
    // ISR fixed frame (A6): the 37B software save is emitted BEFORE any
    // SPAdjust or FP action, so PSW is saved before the inc-spx steps can
    // clobber the virtual flags register. The fixed frame never uses the
    // PUSHFP/POPFP anchor pair and never enters MFI StackSize: the 37B live
    // BELOW the local frame, whose layout (ObjectOffset - StackSize from the
    // post-prologue SPX) is untouched by them.
    assert(!FramePtr &&
           "ISR fixed frame has no variable-sized objects (dynamic allocas "
           "are rejected at lowering)");
    if (FramePtr)
      report_fatal_error("MCS251 ISR: variable-sized objects are not "
                         "supported in an interrupt entry");

    for (unsigned Opc : ISRPushSequence)
      BuildMI(MBB, MBBI, DL, TII.get(Opc))
          .setMIFlag(MachineInstr::FrameSetup);

    // The interrupted context is genuinely live at entry (the pushes read
    // it); establish the live-ins truthfully instead of pseudo-defining or
    // undef-ing the asynchronous inputs.
    for (MCPhysReg Reg : ISRAsyncLiveIns)
      if (!MBB.isLiveIn(Reg))
        MBB.addLiveIn(Reg);

    if (StackSize)
      emitSPAdjust(MBB, MBBI, DL, StackSize, /*IsDec=*/false,
                   MachineInstr::FrameSetup);
    return;
  }

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
  bool IsISR = isISRFunction(MF);
  assert(((IsISR && MBBI->getOpcode() == MCS251::RETI) ||
          (!IsISR && MBBI->getOpcode() == MCS251::ERET)) &&
         "ISR exits must end in RETI, ordinary exits in ERET");
  DebugLoc DL = MBBI->getDebugLoc();
  uint64_t StackSize = MFI.getStackSize();
  bool FramePtr = hasFP(MF);

  if (IsISR) {
    // ISR fixed frame exit (A6): first undo the local frame, then restore
    // in strict inverse order, with pop psw last so it sits directly before
    // the RETI terminator. No PUSHFP/POPFP: the fixed frame has no anchor.
    // PEI drives this hook once per returning block, so every ISR exit --
    // early returns included -- gets the complete restore (T05 step 10).
    assert(!FramePtr && "ISR fixed frame has no frame anchor");
    if (StackSize)
      emitSPAdjust(MBB, MBBI, DL, StackSize, /*IsDec=*/true,
                   MachineInstr::FrameDestroy);
    for (unsigned Opc : ISRPopSequence)
      BuildMI(MBB, MBBI, DL, TII.get(Opc))
          .setMIFlag(MachineInstr::FrameDestroy);
    return;
  }

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
