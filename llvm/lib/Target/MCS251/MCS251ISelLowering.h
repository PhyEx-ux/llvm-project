//===-- MCS251ISelLowering.h - MCS-251 DAG lowering ------------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251ISELLOWERING_H
#define LLVM_LIB_TARGET_MCS251_MCS251ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class MCS251Subtarget;

namespace MCS251ISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  ERET
};
} // namespace MCS251ISD

class MCS251TargetLowering final : public TargetLowering {
public:
  MCS251TargetLowering(const TargetMachine &TM, const MCS251Subtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  SDValue LowerFormalArguments(
      SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
      const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
      SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals,
                      const SDLoc &DL, SelectionDAG &DAG) const override;
};
} // namespace llvm

#endif
