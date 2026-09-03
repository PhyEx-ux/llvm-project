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
  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
    break;
  }

  if (IsVarArg)
    report_fatal_error("minimal MCS251 backend does not support variadic "
                       "functions");

  if (Ins.empty())
    return Chain;

  MachineFunction &MF = DAG.getMachineFunction();

  // CC_MCS251 deliberately offers only the single first-argument slot; the
  // SDCC ABI for a second argument (or any wider type) uses static OSEG
  // overlay slots (_FUNCNAME_PARM_n), which this minimal backend does not
  // implement. Check count and types up front: an unhandled argument would
  // otherwise die inside CCState::AnalyzeFormalArguments with the generic
  // "unable to allocate function argument" message instead of pointing at
  // the real (OSEG) limitation.
  if (Ins.size() > 1 || (Ins[0].VT != MVT::i8 && Ins[0].VT != MVT::i16))
    report_fatal_error("minimal MCS251 backend only supports zero or one "
                       "i8/i16 argument; SDCC multi-arg ABI uses static "
                       "OSEG overlay slots (not yet supported)");

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_MCS251);

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    // Defensive: CC_MCS251 assigns every handled argument to a register;
    // a memory location would mean the CC and the pre-check above disagree.
    if (!VA.isRegLoc())
      report_fatal_error(
          "minimal MCS251 backend only supports zero or one i8/i16 argument; "
          "SDCC multi-arg ABI uses static OSEG overlay slots (not yet "
          "supported)");

    // The argument arrives in a reserved SFR (dpl / dptr); addLiveIn hands
    // its value to the DAG as a virtual register of the allocatable class.
    // It does not emit any copy by itself: SelectionDAGISel calls
    // MachineRegisterInfo::EmitLiveInCopies at the end of instruction
    // selection, which materialises a phys-to-virt COPY ($dpl/$dptr -> the
    // live-in vreg) at the top of the entry block. The coalescer cannot join
    // that COPY (dpl/dptr are reserved and not in GPR8/GPR16), so it survives
    // register allocation and is expanded through copyPhysReg by
    // ExpandPostRAPseudos (TargetInstrInfo::lowerCopy).
    const TargetRegisterClass *RC;
    switch (VA.getLocVT().SimpleTy) {
    case MVT::i8:
      RC = &MCS251::GPR8RegClass;
      break;
    case MVT::i16:
      RC = &MCS251::GPR16RegClass;
      break;
    default:
      report_fatal_error(
          "minimal MCS251 backend only supports zero or one i8/i16 argument; "
          "SDCC multi-arg ABI uses static OSEG overlay slots (not yet "
          "supported)");
    }

    Register VReg = MF.addLiveIn(VA.getLocReg(), RC);
    // No Glue on the argument path (unlike returns, nothing needs to keep
    // these copies adjacent to a terminator).
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
    InVals.push_back(ArgValue);
  }

  return Chain;
}

bool MCS251TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  if (IsVarArg)
    // The first rejection point for variadic functions: CanLowerReturn runs
    // (from FunctionLoweringInfo) before LowerFormalArguments, so name the
    // actual limitation here rather than talking about return values.
    report_fatal_error("minimal MCS251 backend does not support variadic "
                       "functions");
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
