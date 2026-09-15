//===-- MCS251TFPUExpand.cpp - post-RA TFPU pseudo expansion -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// G7 S3 (G7-FLOAT-DESIGN-draft.md §2.4): expansion of the TFPU window
// pseudos AFTER register allocation, at the BranchRelaxation layer.
//
// Why post-RA: the whole load-trigger-wait-readback window is one
// MachineInstr during allocation. Its implicit Defs cover the complete
// PSW[4:3]-selected R0-R7 bank window, so the allocator relocates or spills
// every value that would otherwise live across the sequence in those bytes;
// expanding before RA would dissolve that window and silently let live
// values sit inside the coprocessor's operand/result bytes. The explicit
// copies around the pseudo (the dr4/dr0 window loads and the dr4 readback)
// are already ordinary post-RA copies by the time this pass runs.
//
// The expansion itself is only the trigger plus the wait:
//
//   TFPU_<OP>   ==>   mov 0xED,#<cmd>     (TFPU_TRG, bytes 75 ED <cmd>)
//                     nop  x  <N>         (fixed worst-case delay chain)
//
// The wait model is the design's "fixed worst-case delay" (manual 35.3
// clock table; the manual documents NO completion flag and no interrupt
// exists for the TFPU -- design §1.4). One NOP is emitted per worst-case
// clock: a NOP is a structural 1-clock minimum, so the chain always waits
// AT LEAST the worst case (and never less). Selecting the 0x3F PLL
// asynchronous clock leaves a frequency-ratio margin to the startup
// sequence (S4), which the compiler does not insert (design §2.4). The
// wait step is deliberately its own sequence step so the model can be
// revised in this one place if hardware measurement ever finds a pollable
// status bit (risk register, design §5).
//
// A loop form was rejected for now: every one of R0-R7 is owned by the
// coprocessor during the busy period (they hold the operands/result the
// hardware is still reading), so a counter would have to live outside the
// window in a register this pass could no longer reserve after RA.
//
// Ordering: addPreEmitPass runs this BEFORE MCS251BranchRelaxation so the
// NOP chain sizes are visible to the rel8 arithmetic. Nothing after this
// pass changes instruction sizes.
//
// QEMU stc32g144k246 behaviour for a DMAIR write is unproven (G1-0 open
// item): the emitted bytes are validated statically (golden encodings),
// never by execution, until that changes.
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "MCS251InstrInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

#define DEBUG_TYPE "mcs251-tfpu-expand"

namespace {

// The TFPU command code and fixed worst-case clock count of every
// connected pseudo (manual 35-TFPU-*.md 35.3 table; worst case of each
// range). cmd is the immediate written to DMAIR (0xED), WaitClks the NOP
// count of the delay chain.
struct TFPUCommand {
  uint8_t Cmd;
  unsigned WaitClks;
};

// add 0x1C 31~40 / sub 0x1D 31~40 / mul 0x1E 26~34 / div 0x1F 58~67
// sqrt 0x20 50-54 / sin 0x2D 32~270 / cos 0x2E 32~270
// tan 0x2F 58~258 / atan 0x30 62~175
static bool getTFPUCommand(unsigned Opcode, TFPUCommand &Out) {
  switch (Opcode) {
  case MCS251::TFPU_ADD:
    Out = {0x1C, 40};
    return true;
  case MCS251::TFPU_SUB:
    Out = {0x1D, 40};
    return true;
  case MCS251::TFPU_MUL:
    Out = {0x1E, 34};
    return true;
  case MCS251::TFPU_DIV:
    Out = {0x1F, 67};
    return true;
  case MCS251::TFPU_SQRT:
    Out = {0x20, 54};
    return true;
  case MCS251::TFPU_SIN:
    Out = {0x2D, 270};
    return true;
  case MCS251::TFPU_COS:
    Out = {0x2E, 270};
    return true;
  case MCS251::TFPU_TAN:
    Out = {0x2F, 258};
    return true;
  case MCS251::TFPU_ATAN:
    Out = {0x30, 175};
    return true;
  default:
    return false;
  }
}

class MCS251TFPUExpand final : public MachineFunctionPass {
public:
  static char ID;
  MCS251TFPUExpand() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "MCS-251 TFPU pseudo expansion";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    const MCS251InstrInfo *TII =
        static_cast<const MCS251InstrInfo *>(MF.getSubtarget().getInstrInfo());
    bool Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto MI = MBB.begin(), ME = MBB.end(); MI != ME;) {
        MachineInstr &I = *MI;
        ++MI;
        // Window loads first: TFPU_LD_AR / TFPU_LD_BR become the plain
        // register moves of design §2.4 step 1. The operand register is
        // already allocated in GPR32Win (dr12) and lives outside the R0-R7
        // window, so the move is the ONLY write into the coprocessor's
        // operand bytes and it happens here, inside the window.
        //
        // The kill flag is taken from the pseudo's own operand, never forced:
        // when both command operands are the SAME value (tfpu_mul(x, x)) the
        // allocator gives both loads the same register and marks only the
        // second use as killing. Forcing kill on both would make the second
        // read a use of a register already killed by the first.
        switch (I.getOpcode()) {
        case MCS251::TFPU_LD_AR: {
          const MachineOperand &Src = I.getOperand(0);
          BuildMI(MBB, I, I.getDebugLoc(), TII->get(MCS251::MOV32rr),
                  MCS251::DR4)
              .addReg(Src.getReg(), getKillRegState(Src.isKill()));
          I.eraseFromParent();
          Changed = true;
          continue;
        }
        case MCS251::TFPU_LD_BR: {
          const MachineOperand &Src = I.getOperand(0);
          BuildMI(MBB, I, I.getDebugLoc(), TII->get(MCS251::MOV32rr),
                  MCS251::DR0)
              .addReg(Src.getReg(), getKillRegState(Src.isKill()));
          I.eraseFromParent();
          Changed = true;
          continue;
        }
        // The window readback (blocker-1 fix, readback half): TFPU_RD_AR
        // becomes `mov $dst, dr4` HERE -- after the wait chain, before any
        // consumer. The destination is an explicit vreg in GPR32Win (dr12,
        // outside the R0-R7 window), so the allocator cannot fold this read
        // into the consumer the way it folded the old COPY-from-DR4: the
        // dr4 lanes are snapshotted inside the window, not at the first use.
        case MCS251::TFPU_RD_AR: {
          Register Dst = I.getOperand(0).getReg();
          BuildMI(MBB, I, I.getDebugLoc(), TII->get(MCS251::MOV32rr), Dst)
              .addReg(MCS251::DR4);
          I.eraseFromParent();
          Changed = true;
          continue;
        }
        default:
          break;
        }
        TFPUCommand Cmd;
        if (!getTFPUCommand(I.getOpcode(), Cmd))
          continue;
        // Trigger first: the manual's mandatory immediate-addressed write
        // `MOV DMAIR,#cmd` (75 ED <cmd>). hasSideEffects on TFPU_TRG keeps
        // two sequences from ever merging.
        BuildMI(MBB, I, I.getDebugLoc(), TII->get(MCS251::TFPU_TRG))
            .addImm(Cmd.Cmd);
        // Then the fixed worst-case wait: one NOP per worst-case clock.
        for (unsigned N = 0; N < Cmd.WaitClks; ++N)
          BuildMI(MBB, I, I.getDebugLoc(), TII->get(MCS251::NOP));
        I.eraseFromParent();
        Changed = true;
      }
    }
    return Changed;
  }
};

} // namespace

char MCS251TFPUExpand::ID = 0;

MachineFunctionPass *llvm::createMCS251TFPUExpandPass() {
  return new MCS251TFPUExpand();
}

INITIALIZE_PASS(MCS251TFPUExpand, DEBUG_TYPE, "MCS-251 TFPU pseudo expansion",
                false, false)
