//===-- MCS251FrameLowering.h - MCS-251 frame lowering ----------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251FRAMELOWERING_H
#define LLVM_LIB_TARGET_MCS251_MCS251FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class MCS251FrameLowering final : public TargetFrameLowering {
protected:
  // A frame pointer only exists for functions with variable-sized objects
  // (dynamic allocas move SPX at the alloca site, so the static frame must
  // be referenced from the dr16 anchor instead of SPX). Every other
  // function addresses its frame purely SPX-relative, SDCC-style.
  bool hasFPImpl(const MachineFunction &MF) const override;

public:
  MCS251FrameLowering();

  void emitPrologue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  MachineBasicBlock::iterator eliminateCallFramePseudoInstr(
      MachineFunction &MF, MachineBasicBlock &MBB,
      MachineBasicBlock::iterator MI) const override;

  // Stack-grows-up mirror of the default formula. The default
  // getFrameIndexReference computes ObjectOffset + StackSize - ... which is
  // the DOWN-growing layout; for our up-growing stack the frame top
  // (SPX after the prologue, or the dr16 anchor) sits ABOVE the objects, so
  // the reference is ObjectOffset - StackSize (a negative displacement).
  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  // Emits an SPX adjustment of `Amount` bytes as a sequence of the only
  // legal inc/dec immediates (4/2/1 steps, the SDCC prologue template).
  // Large frames simply get a longer sequence; a `mov drX,#imm; add/sub
  // dr60,drX` large-adjustment scheme exists in the ISA but would clobber
  // an allocatable drX in the post-RA prologue, so it needs a reserved
  // scratch DR first (future work -- frames here are bounded by the
  // ~200-byte SSEG anyway).
  void emitSPAdjust(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                    const DebugLoc &DL, uint64_t Amount, bool IsDec,
                    MachineInstr::MIFlag Flag) const;
};
} // namespace llvm

#endif
