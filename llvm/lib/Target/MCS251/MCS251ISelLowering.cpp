//===-- MCS251ISelLowering.cpp - MCS-251 DAG lowering --------------------===//

#include "MCS251ISelLowering.h"
#include "MCS251.h"
#include "MCS251Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

// Generated from MCS251CallingConv.td; provides RetCC_MCS251().
#define GET_CALLING_CONV_IMPL
#include "MCS251GenCallingConv.inc"

MCS251TargetLowering::MCS251TargetLowering(const TargetMachine &TM,
                                           const MCS251Subtarget &STI)
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i8, &MCS251::GPR8RegClass);
  addRegisterClass(MVT::i16, &MCS251::GPR16RegClass);
  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(MCS251::DR60);
  setBooleanContents(ZeroOrOneBooleanContent);
}

const char *MCS251TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case MCS251ISD::ERET:
    return "MCS251ISD::ERET";
  default:
    return nullptr;
  }
}

SDValue MCS251TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (IsVarArg || !Ins.empty())
    report_fatal_error(
        "minimal MCS251 backend only supports functions without arguments");
  return Chain;
}

bool MCS251TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  if (IsVarArg)
    report_fatal_error(
        "minimal MCS251 backend only supports i8/i16/void return values");
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  if (!CCInfo.CheckReturn(Outs, RetCC_MCS251))
    report_fatal_error(
        "minimal MCS251 backend only supports i8/i16/void return values");
  return true;
}

SDValue MCS251TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  // Assign the return values to the ABI locations: dpl for i8, the
  // dpl:dph pair (modelled as dptr) for i16. Anything else has already
  // been rejected by CanLowerReturn.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_MCS251);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the return registers.
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "MCS251 return values must go to registers");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    // Keep the copies stuck together so nothing gets scheduled in between.
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(MCS251ISD::ERET, DL, MVT::Other, RetOps);
}
