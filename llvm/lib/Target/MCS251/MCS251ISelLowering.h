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
  CALL
};
} // namespace MCS251ISD

class MCS251TargetLowering final : public TargetLowering {
public:
  MCS251TargetLowering(const TargetMachine &TM, const MCS251Subtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  // The DAG combiner's consecutive-store merge would fuse i8 stores into
  // i32 stores (e.g. an alloca'd byte array initialised element by
  // element). There is no i32 memory instruction and i32 stores are a
  // deliberate loud rejection, so the merge is turned off entirely --
  // the merged-to type would only be legal at i8 anyway.
  bool canMergeStoresTo(unsigned AS, EVT MemVT,
                        const MachineFunction &MF) const override {
    return false;
  }

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerStore(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExtend(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDynamicStackAlloc(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTACKSAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTACKRESTORE(SDValue Op, SelectionDAG &DAG) const;

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
