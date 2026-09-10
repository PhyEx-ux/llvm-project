//===-- MCS251InstrInfo.h - MCS-251 instruction information ----*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251INSTRINFO_H
#define LLVM_LIB_TARGET_MCS251_MCS251INSTRINFO_H

#include "MCS251RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "MCS251GenInstrInfo.inc"

namespace llvm {
class MCS251Subtarget;

class MCS251InstrInfo final : public MCS251GenInstrInfo {
  const MCS251RegisterInfo RI;

  [[noreturn]] void reportBadSpillClass(const TargetRegisterClass *RC) const;

public:
  MCS251InstrInfo(const MCS251Subtarget &STI, unsigned PointerBits);
  const MCS251RegisterInfo &getRegisterInfo() const { return RI; }

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, Register SrcReg,
                           bool IsKill, int FrameIndex,
                           const TargetRegisterClass *RC, Register VReg,
                           MachineInstr::MIFlag Flags) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            Register VReg, unsigned SubReg,
                            MachineInstr::MIFlag Flags) const override;

  //===--------------------------------------------------------------------===//
  // Branch encoding facts shared with the post-layout branch relaxation
  // pass (MCS251BranchRelaxation, E1).  The jcc family and sjmp are 2-byte
  // rel8 branches (+/-128 bytes around the instruction), while ejmp carries
  // a 24-bit target and reaches anywhere.
  //===--------------------------------------------------------------------===//

  static bool isCondBranchOpcode(unsigned Opc);
  static bool isUncondBranchOpcode(unsigned Opc);

  // Exact encoded size of every target instruction, mirroring the MC code
  // emitter byte-for-byte.  getInstSizeVerifyMode asks the AsmPrinter to
  // check each declared size against the emitted byte count on object
  // output -- via ExactSizeAlways, so the check also runs in release
  // builds (plain ExactSize is +asserts-only upstream semantics) -- so any
  // future drift between the two tables fails loudly instead of silently
  // corrupting branch displacement computation.
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;
  InstSizeVerifyMode
  getInstSizeVerifyMode(const MachineInstr &MI) const override;

  // Cross-check the tablegen MCInstrDesc `Size` of the bit-addressed
  // instruction family against the exact encoded size computed here.  The
  // AsmPrinter's ExactSize verification only compares EMITTED BYTES against
  // getInstSizeInBytes; it never looks at the TD Size, so a drifted
  // `let Size = N` on SETBBIT/JB/... would otherwise go unnoticed.  Run by
  // the machine verifier (llc -verify-machineinstrs).
  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;
};
} // namespace llvm

#endif
