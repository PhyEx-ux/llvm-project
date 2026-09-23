//===-- MCS251BranchRelaxation.cpp - post-layout long branches --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// E1 (assessment 2026-09-10): the jcc family and sjmp are 2-byte
// instructions with a signed rel8 displacement measured from the byte after
// the instruction, so they only reach +/-128 bytes; ejmp carries a 24-bit
// absolute target and reaches anywhere.
//
// The FinalizeISel long-branch expansion (expandLongConditionalBranch)
// keeps its skip displacement at a fixed 4 bytes only while the skip block
// stays layout-adjacent to the branch.  MachineBlockPlacement is free to
// displace the skip block -- observed in practice once a function grows
// past a few hundred blocks (its chain formation moves a displaced skip
// block away even with uniform successor probabilities) -- and then the
// rel8 no longer reaches, failing assembly with "MCS251 PC-relative branch
// out of range".
//
// This pass runs in addPreEmitPass: after MachineBlockPlacement, when the
// layout is final, and before any pass that could change instruction or
// block sizes again.  It measures every rel8 branch exactly (MCS251InstrInfo::
// getInstSizeInBytes mirrors the MC emitter; the AsmPrinter's exact-size
// verification polices that table on object output) and rewrites the
// out-of-range ones into equivalent always-reachable forms:
//
//   ... jCC dest    ==>   ... jCC tramp      (same opcode/condition, near
//       ejmp    far    ==>     ejmp    far      target; the trampoline block
//     dest:                  tramp:               is created 4 bytes below
//                              ejmp dest         the branch, so the rel8
//                                                is in range by
//                                                construction)
//
//   sjmp far       ==>     ejmp far
//
// The branch condition is deliberately NOT inverted: its taken edge keeps
// pointing at the same destination, only via the trampoline.  Inverting
// would swap the two CFG edges and silently change the program.
//
// Every trampoline is a fresh 4-byte block inserted directly after its
// branch, which shifts all later offsets; the scan therefore repeats until
// nothing changes.  Each rewrite moves its own branch into a permanently
// in-range form (the trampoline is by construction 4 bytes from its branch,
// and ejmp reaches anywhere), so the loop terminates.  A debug-only final
// check verifies every rel8 branch is in range; the MC assembler backend
// remains the loud backstop for anything that slips through.
//
// Only the three terminator shapes this backend emits are recognized
// (bare ejmp/sjmp, the jcc+ejmp long-branch pair, and a lone jcc with an
// implicit fallthrough edge).  Branches with a symbolic (non-MBB) target
// are MIR-only relocation probes and are left untouched.  The generic
// TargetInstrInfo::analyzeBranch hooks are deliberately not implemented --
// analyzable terminators would silently activate the generic branch folder
// and tail duplication, a pipeline-wide codegen change this backend has not
// signed up for.
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "MCS251InstrInfo.h"
#include "MCS251RegisterInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "mcs251-branch-relaxation"

namespace {
class MCS251BranchRelaxation final : public MachineFunctionPass {
public:
  static char ID;

  MCS251BranchRelaxation() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "MCS251 Branch Relaxation";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  // One block's contribution to the branch arithmetic: encoded size and
  // offset from the start of the function.
  struct BlockInfo {
    uint64_t Size = 0;
    uint64_t Offset = 0;
  };

  void computeBlockSizes(MachineFunction &MF);
  void adjustBlockOffsets(MachineFunction &MF);
  unsigned getInstrOffset(const MachineInstr &MI) const;

  // rel8 is measured from the byte after the 2-byte instruction.
  bool isInRange(const MachineInstr &Br, const MachineBasicBlock &Dest) const;

  bool relaxBranches(MachineFunction &MF);
  void relaxConditional(MachineFunction &MF, MachineInstr &Br,
                        MachineBasicBlock &Dest) const;
  void relaxUnconditional(MachineInstr &Br, MachineBasicBlock &Dest) const;

  MachineFunction *MF = nullptr;
  const MCS251InstrInfo *TII = nullptr;
  SmallVector<BlockInfo, 32> Blocks;

#ifndef NDEBUG
  void verify(MachineFunction &MF) const;
#endif
};
} // namespace

char MCS251BranchRelaxation::ID = 0;

INITIALIZE_PASS(MCS251BranchRelaxation, DEBUG_TYPE,
                "MCS251 Branch Relaxation", false, false)

void MCS251BranchRelaxation::computeBlockSizes(MachineFunction &MF) {
  Blocks.assign(MF.getNumBlockIDs(), BlockInfo());
  for (MachineBasicBlock &MBB : MF) {
    uint64_t Size = 0;
    for (const MachineInstr &MI : MBB)
      Size += TII->getInstSizeInBytes(MI);
    Blocks[MBB.getNumber()].Size = Size;
  }
}

void MCS251BranchRelaxation::adjustBlockOffsets(MachineFunction &MF) {
  uint64_t Offset = 0;
  for (MachineBasicBlock &MBB : MF) {
    // No basic block alignment is produced by this target, but honor the
    // field anyway so a future loop-alignment change cannot silently break
    // the arithmetic.
    Offset = alignTo(Offset, MBB.getAlignment());
    Blocks[MBB.getNumber()].Offset = Offset;
    Offset += Blocks[MBB.getNumber()].Size;
  }
}

unsigned
MCS251BranchRelaxation::getInstrOffset(const MachineInstr &MI) const {
  const MachineBasicBlock *MBB = MI.getParent();
  uint64_t Offset = Blocks[MBB->getNumber()].Offset;
  for (const MachineInstr &I : *MBB) {
    if (&I == &MI)
      break;
    Offset += TII->getInstSizeInBytes(I);
  }
  return Offset;
}

bool MCS251BranchRelaxation::isInRange(const MachineInstr &Br,
                                       const MachineBasicBlock &Dest) const {
  // rel8 is measured from the byte after the instruction. The jcc family and
  // sjmp are 2 bytes; the bit-test branches (jb/jnb/jbc) are 3 bytes
  // (opcode + bit address + rel8). Ask the size table instead of assuming 2.
  unsigned BrSize = TII->getInstSizeInBytes(Br);
  int64_t Disp = static_cast<int64_t>(Blocks[Dest.getNumber()].Offset) -
                 static_cast<int64_t>(getInstrOffset(Br) + BrSize);
  return isInt<8>(Disp);
}

bool MCS251BranchRelaxation::runOnMachineFunction(MachineFunction &mf) {
  MF = &mf;
  TII = static_cast<const MCS251InstrInfo *>(
      MF->getSubtarget().getInstrInfo());

  bool Changed = false;
  while (relaxBranches(*MF))
    Changed = true;

#ifndef NDEBUG
  verify(*MF);
#endif
  return Changed;
}

bool MCS251BranchRelaxation::relaxBranches(MachineFunction &MF) {
  computeBlockSizes(MF);
  adjustBlockOffsets(MF);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.terminators()) {
      unsigned Opc = MI.getOpcode();
      if (!MCS251InstrInfo::isCondBranchOpcode(Opc) &&
          Opc != MCS251::SJMP)
        continue; // ejmp reaches anywhere; everything else is not a branch.
      // Symbolic branch targets (MIR-only relocation probes) are not CFG
      // branches and never participate in relaxation.
      if (!MI.getOperand(0).isMBB())
        continue;
      MachineBasicBlock &Dest = *MI.getOperand(0).getMBB();
      if (isInRange(MI, Dest))
        continue;
      if (Opc == MCS251::SJMP)
        relaxUnconditional(MI, Dest);
      else
        relaxConditional(MF, MI, Dest);
      // Offsets shifted; rescan from the start of the function.
      return true;
    }
  }
  return false;
}

//   ... jCC dest    ==>   ... jCC tramp      (same opcode/condition, near
//       <other                <other           target: the taken edge keeps
//        terminators>          terminators>    pointing at the same block,
//   dest:                 tramp:                  only via the trampoline.
//                            ejmp dest           Inverting the condition
//                                                 would swap the two CFG
//                                                 edges and change the
//                                                 program.)
//
// The lone-fallthrough-conditional shape (no trailing ejmp) has its other
// CFG edge materialized as an explicit ejmp first, so the retargeted branch
// cannot steal the fallthrough edge.
void MCS251BranchRelaxation::relaxConditional(
    MachineFunction &MF, MachineInstr &Br, MachineBasicBlock &Dest) const {
  MachineBasicBlock &MBB = *Br.getParent();
  DebugLoc DL = Br.getDebugLoc();

  bool HasFarEjmp = false;
  for (MachineInstr &I : MBB.terminators()) {
    if (&I != &Br && I.getOpcode() == MCS251::EJMP) {
      HasFarEjmp = true;
      break;
    }
  }
  if (!HasFarEjmp) {
    MachineBasicBlock *Fall = MBB.getNextNode();
    if (!Fall || !MBB.isSuccessor(Fall))
      reportFatalUsageError("MCS251: out-of-range conditional branch without an "
                         "explicit far edge cannot be relaxed");
    BuildMI(MBB, MBB.end(), DL, TII->get(MCS251::EJMP)).addMBB(Fall);
  }

  // The trampoline lives directly after MBB; the 4-byte ejmp in front of it
  // makes the retargeted rel8 displacement a fixed 4 bytes -- the same
  // invariant the FinalizeISel expansion was designed around, now enforced
  // at the very end of the pipeline by construction.
  MachineBasicBlock &Tramp = *MF.CreateMachineBasicBlock();
  MF.insert(++MBB.getIterator(), &Tramp);

  BuildMI(Tramp, Tramp.end(), DL, TII->get(MCS251::EJMP)).addMBB(&Dest);

  // Trampolines define no registers and use none, so no live-ins are
  // needed; successors: MBB -> Tramp (was -> Dest), Tramp -> Dest.
  MBB.replaceSuccessor(&Dest, &Tramp);
  Tramp.addSuccessor(&Dest);

  // In-place retarget: same opcode and condition, only a near destination.
  Br.getOperand(0).setMBB(&Tramp);
}

//   sjmp dest ==> ejmp dest
void MCS251BranchRelaxation::relaxUnconditional(
    MachineInstr &Br, MachineBasicBlock &Dest) const {
  MachineBasicBlock &MBB = *Br.getParent();
  DebugLoc DL = Br.getDebugLoc();
  BuildMI(MBB, Br, DL, TII->get(MCS251::EJMP)).addMBB(&Dest);
  Br.eraseFromParent();
}

#ifndef NDEBUG
void MCS251BranchRelaxation::verify(MachineFunction &MF) const {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.terminators()) {
      unsigned Opc = MI.getOpcode();
      if (!MCS251InstrInfo::isCondBranchOpcode(Opc) && Opc != MCS251::SJMP)
        continue;
      if (!MI.getOperand(0).isMBB())
        continue;
      const MachineBasicBlock &Dest = *MI.getOperand(0).getMBB();
      if (!isInRange(MI, Dest))
        reportFatalInternalError("MCS251: rel8 branch still out of range after "
                                 "relaxation");
    }
  }
}
#endif

MachineFunctionPass *llvm::createMCS251BranchRelaxationPass() {
  return new MCS251BranchRelaxation();
}
