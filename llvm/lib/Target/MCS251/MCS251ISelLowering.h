//===-- MCS251ISelLowering.h - MCS-251 DAG lowering ------------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251ISELLOWERING_H
#define LLVM_LIB_TARGET_MCS251_MCS251ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class MCS251Subtarget;

namespace MCS251ISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  ERET,
  CALL,
  // Independent interrupt return (ISR campaign T04). Built only by
  // LowerReturn for CallingConv::MCS251_INTR (T05); ordinary returns keep
  // ERET. Chain-only node, selected by the RETI machine instruction
  // (MCS251InstrInfo.td). Never substitute ERET for it or vice versa: RETI
  // restores the saved status register (PSW1) and pops the interrupt
  // controller in-service state, which ERET must not do.
  RETI
};
} // namespace MCS251ISD

class MCS251TargetLowering final : public TargetLowering {
public:
  MCS251TargetLowering(const TargetMachine &TM, const MCS251Subtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;
  EVT getSetCCResultType(const DataLayout &, LLVMContext &, EVT) const override {
    return MVT::i8;
  }

  // BRJT (design §3.2.6, rev3): backend-independent jump-table qualification
  // for the `-mcs251-jump-tables` opt-in. Implements predicates E2 (cond is an
  // integer scalar of 8/16/32 bits), E3 (table entry count Range <= 86, the
  // 8-bit A-register x3 index invariant) and E5 (density with the upstream
  // 10/40 optsize tier) WITHOUT the base class's `OptForSize ||` short-circuit
  // of the range cap, which would let optsize/minsize functions build tables
  // of any size (measured P9-C/P9-D).
  bool isSuitableForJumpTable(const SwitchInst *SI, uint64_t NumCases,
                              uint64_t Range, ProfileSummaryInfo *PSI,
                              BlockFrequencyInfo *BFI) const override;

  // Keep explicit byte ordering and avoid merging across the target-specific
  // SFR-direct versus indirect-address distinction.
  bool canMergeStoresTo(unsigned AS, EVT MemVT,
                        const MachineFunction &MF) const override {
    return false;
  }

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *) const override {
    return true;
  }
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  // BRJT (design §3.2.1): the fixed `jmp @a+dptr` dispatch sequence for an
  // E2/E3/E5-qualified jump-table cluster (`-mcs251-jump-tables` opt-in).
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerStore(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExtend(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSignExtendInReg(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShift(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerMul32(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerArithmetic32(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLogical32(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDynamicStackAlloc(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTACKSAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTACKRESTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAddrSpaceCast(SDValue Op, SelectionDAG &DAG) const;

  // G2 B-S2 static-slot variadic ABI (G2-VARIADIC-DESIGN-draft.md R3
  // §4.3.4/§4.3.5).  va_start stores the {first continuation slot address,
  // 0} pair through the va_list pointer; va_end is a chain-keeping no-op;
  // va_copy duplicates the whole pair; a residual llvm.va_arg (clang now
  // lowers va_arg itself via MCS251ABIInfo::EmitVAArg) fails closed.
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVACOPY(SDValue Op, SelectionDAG &DAG) const;

  // MCS251 bit-access intrinsics (BIT task BT03):
  //   llvm.mcs251.bit.read/set/clear/toggle
  // set/clear/toggle are chain-only INTRINSIC_VOID nodes; read is an
  // INTRINSIC_W_CHAIN with an i1 result that the type legalizer promotes to
  // i8 via ReplaceNodeResults.  Both entry points build the same fixed
  // MOV C,bit + materialise or single RMW instruction shape.
  SDValue LowerBitIntrinsic(SDValue Op, SelectionDAG &DAG) const;
  void ReplaceBitReadResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                             SelectionDAG &DAG) const;

  // G7 S3 (G7-FLOAT-DESIGN-draft.md §2.4): the TFPU intrinsic family
  // (llvm.mcs251.tfpu.*, i32 bit-pattern signature). Builds the glued
  // CopyToReg(dr4[/dr0]) -> TFPU_<OP> pseudo -> CopyFromReg(dr4) window;
  // the pseudo itself is expanded after register allocation by
  // MCS251TFPUExpand (mov 0xED,#cmd + fixed worst-case NOP chain).
  SDValue LowerTFPUIntrinsic(SDValue Op, SelectionDAG &DAG) const;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool IsVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;

  SDValue LowerFormalArguments(
      SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
      const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
      SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals,
                      const SDLoc &DL, SelectionDAG &DAG) const override;

private:
  MachineBasicBlock *emitVariableShift(MachineInstr &MI,
                                       MachineBasicBlock *BB) const;

  // Expands a BRCC/BRCC8S pseudo into the three-part long conditional
  // branch `jCCinv SkipMBB; ejmp TrueMBB; SkipMBB:` (jcc only reaches rel8;
  // the skip target is the 4-byte ejmp directly below, always in range).
  // Everything after the pseudo (the EJMP of the explicit false edge) moves
  // into SkipMBB, and every CFG successor except the true edge transfers to
  // SkipMBB. Returns SkipMBB for the FinalizeISel scan to continue in.
  MachineBasicBlock *
  expandLongConditionalBranch(MachineInstr &MI, MachineBasicBlock *BB,
                              MachineBasicBlock *TrueMBB,
                              unsigned SkipBranchOpcode) const;
};
} // namespace llvm

#endif
