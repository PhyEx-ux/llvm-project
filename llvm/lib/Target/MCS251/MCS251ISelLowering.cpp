//===-- MCS251ISelLowering.cpp - MCS-251 DAG lowering --------------------===//

#include "MCS251ISelLowering.h"
#include "MCS251.h"
#include "MCS251Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
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

  // Phase 6: comparisons and branches. BR_CC is custom-lowered into a cmp
  // plus the BRCC long-branch pseudo (see LowerBR_CC). Declaring it Custom
  // also enables the DAG combiner's BRCOND(setcc) -> BR_CC fold, which is
  // the only way a `br i1 (icmp ...)` reaches this code. SETCC (materialised
  // compare result) and BRCOND (branch on a non-icmp i1) are out of scope
  // and rejected with a clear message in LowerOperation.
  setOperationAction(ISD::BR_CC, MVT::i8, Custom);
  setOperationAction(ISD::BR_CC, MVT::i16, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  setOperationAction(ISD::SETCC, MVT::i8, Custom);
  setOperationAction(ISD::SETCC, MVT::i16, Custom);
}

const char *MCS251TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case MCS251ISD::ERET:
    return "MCS251ISD::ERET";
  default:
    return nullptr;
  }
}

SDValue MCS251TargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("custom operation has no registered lowering");
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::BRCOND:
  case ISD::SETCC:
    report_fatal_error(
        "MCS251: Phase 6 supports only BR_CC (branch on icmp); "
        "SETCC/BRCOND arrive in a later phase");
  }
}

//===----------------------------------------------------------------------===//
//  Branch lowering (Phase 6)
//===----------------------------------------------------------------------===//
//
// The jcc family only reaches rel8 (+/-128), so every conditional branch is
// emitted as the three-part long branch
//
//     jCCinv  skip      2 bytes, skip is 4 bytes below: always in range
//     ejmp    target    4 bytes (opcode 8A + addr24), reaches anywhere
//   skip:
//
// There is no branch relaxation infrastructure in this backend, so the
// conservative always-expand form is the correct baseline (SDCC emits the
// same shape). The expansion happens in two places: LowerBR_CC emits the
// cmp (or defers everything, for signed i8) and the BRCC/BRCC8S pseudo;
// EmitInstrWithCustomInserter (run from FinalizeISel, when the whole
// machine CFG exists) splits the block and lowers the pseudo.

// Signed-ness of an integer SETCC (the O/U-prefixed codes are float-only
// orderings and never appear for integers; signedness is that of the base
// code).
static bool isSignedIntegerCond(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE:
    return true;
  default:
    return false;
  }
}

// The `jCCinv skip` opcode for a same-width compare: it must jump exactly
// when the original condition is false. icmp->jcc map (QEMU-run-verified,
// manual truth tables agree) plus the complements
// eq<->ne, ult<->uge, ugt<->ule, slt<->sge, sgt<->sle:
//   eq->JE(Z=1)   ne->JNE   ult->JC(CY=1)   uge->JNC
//   ugt->JG(Z=0^CY=0)  ule->JLE(Z|CY)  slt->JSL(N!=OV)  sge->JSGE(N=OV)
//   sgt->JSG(Z=0^N=OV)  sle->JSLE(Z|N!=OV)
static unsigned getSkipBranchOpcode(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETEQ:
    return MCS251::JNE; // !eq = ne
  case ISD::SETNE:
    return MCS251::JE; // !ne = eq
  case ISD::SETULT:
    return MCS251::JNC; // !ult = uge
  case ISD::SETUGE:
    return MCS251::JC; // !uge = ult
  case ISD::SETUGT:
    return MCS251::JLE; // !ugt = ule
  case ISD::SETULE:
    return MCS251::JG; // !ule = ugt
  case ISD::SETLT:
    return MCS251::JSGE; // !slt = sge
  case ISD::SETGE:
    return MCS251::JSL; // !sge = slt
  case ISD::SETGT:
    return MCS251::JSLE; // !sgt = sle
  case ISD::SETLE:
    return MCS251::JSG; // !sle = sgt
  default:
    llvm_unreachable("unsupported integer condition code");
  }
}

// The skip opcode for the BRCC8S (signed i8, widened to 16 bits) case: after
// the offset-binary flip the signed order is the unsigned order of the
// flipped values, so the complements land on the unsigned jumps.
static unsigned getSkipBranchOpcode8S(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETLT:
    return MCS251::JNC; // !slt = uge(flip)
  case ISD::SETGE:
    return MCS251::JC; // !sge = ult(flip)
  case ISD::SETGT:
    return MCS251::JLE; // !sgt = ule(flip)
  case ISD::SETLE:
    return MCS251::JG; // !sle = ugt(flip)
  default:
    llvm_unreachable("BRCC8S is only built for signed i8 conditions");
  }
}

// Materialise an i8/i16 constant into a vreg (only used on defensive paths
// where the DAG could not fold the constant away).
static SDValue materializeImm(SDValue N, const SDLoc &DL, SelectionDAG &DAG) {
  auto *C = cast<ConstantSDNode>(N);
  if (N.getValueType() == MVT::i8)
    return SDValue(
        DAG.getMachineNode(
            MCS251::MOV8ri, DL, MVT::i8,
            DAG.getTargetConstant(C->getAPIntValue().trunc(8).getZExtValue(),
                                  DL, MVT::i8)),
        0);
  return SDValue(
      DAG.getMachineNode(
          MCS251::MOV16ri, DL, MVT::i16,
          DAG.getTargetConstant(C->getAPIntValue().trunc(16).getZExtValue(),
                                DL, MVT::i16)),
      0);
}

// Known limitation: -O0 (fast regalloc) crashes on any function with
// cross-block live vregs: FastRA spills via loadRegFromStackSlot, and
// stack/frame support only arrives in Phase 9. Use -O2/-O1 (greedy) until
// then.
SDValue MCS251TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  // Canonicalise a constant to the RHS: the cmp immediate forms take the
  // constant second, the LHS must be a register.
  if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }

  SDValue CCVal = DAG.getTargetConstant(CC, DL, MVT::i8);

  if (LHS.getValueType() == MVT::i8 && isSignedIntegerCond(CC)) {
    // Signed i8 compare: the N-flag behaviour at 8-bit width has not been
    // measured, so the comparison is conservatively widened to 16 bits
    // (release this once a QEMU run confirms 8-bit N). The BRCC8S pseudo
    // builds the offset-binary sign-extension per operand at MIR level
    // (byte-into-word lane moves are not expressible in the DAG here); both
    // of its value operands must therefore be vregs.
    if (isa<ConstantSDNode>(RHS))
      RHS = materializeImm(RHS, DL, DAG);
    if (isa<ConstantSDNode>(LHS)) // both constant; defensive
      LHS = materializeImm(LHS, DL, DAG);
    // Machine-node operand order is [explicit..., chain]: InstrEmitter
    // identifies the chain/glue by their trailing position (countOperands),
    // so the chain must be the last value operand.
    return SDValue(DAG.getMachineNode(MCS251::BRCC8S, DL, MVT::Other,
                                      {LHS, RHS, CCVal, Dest, Chain}),
                   0);
  }

  // Emit the compare as a machine node (cmp has no Pattern: there is no
  // value-producing compare in this backend). The glue pins it to the BRCC
  // pseudo so that nothing is scheduled between the flag producer and
  // consumer.
  EVT VT = LHS.getValueType();
  unsigned CmpOpc;
  SmallVector<SDValue, 2> CmpOps;
  if (auto *C = dyn_cast<ConstantSDNode>(RHS)) {
    // cmp r,#imm8 (BE m,0000 imm) / cmp wr,#imm16 (BE j/2,0100 hi lo)
    if (VT == MVT::i8) {
      CmpOpc = MCS251::CMP8ri;
      CmpOps.push_back(DAG.getTargetConstant(
          C->getAPIntValue().trunc(8).getZExtValue(), DL, MVT::i8));
    } else {
      CmpOpc = MCS251::CMP16ri;
      CmpOps.push_back(DAG.getTargetConstant(
          C->getAPIntValue().trunc(16).getZExtValue(), DL, MVT::i16));
    }
  } else {
    // cmp r,r (BC md ms) / cmp wr,wr (BD jd/2 js/2)
    CmpOpc = (VT == MVT::i8) ? MCS251::CMP8rr : MCS251::CMP16rr;
    CmpOps.push_back(RHS);
  }
  if (isa<ConstantSDNode>(LHS)) {
    // Both sides constant (the DAG folds icmp of two constants long before
    // this point, so this is defensive).
    LHS = materializeImm(LHS, DL, DAG);
  }
  CmpOps.insert(CmpOps.begin(), LHS);

  SDValue Glue(DAG.getMachineNode(CmpOpc, DL, MVT::Glue, CmpOps), 0);
  // [explicit..., chain, glue] -- see the comment in the BRCC8S branch above.
  return SDValue(DAG.getMachineNode(MCS251::BRCC, DL, MVT::Other,
                                    {CCVal, Dest, Chain, Glue}),
                 0);
}

MachineBasicBlock *
MCS251TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unknown custom inserter opcode");
  case MCS251::BRCC:
    return expandLongConditionalBranch(
        MI, BB, MI.getOperand(1).getMBB(),
        getSkipBranchOpcode((ISD::CondCode)MI.getOperand(0).getImm()));
  case MCS251::BRCC8S: {
    // Signed i8: build 0x00:(x^0x80) per operand into a fresh wr, compare
    // at 16 bits, then the same long branch. x ^ 0x80 is the offset-binary
    // bias ((x + 0x80) mod 256); it maps the signed order of the bytes onto
    // the unsigned order of the 16-bit results. All encodings used here
    // (mov r,#imm / mov r,r / xrl r,#imm / cmp wr,wr) are
    // measurement-verified.
    //
    // The byte lanes are assembled with one REG_SEQUENCE instead of
    // subregister defs: every temporary then has exactly one defining
    // instruction, which keeps MachineCSE (its replacement path calls
    // getUniqueVRegDef on the dominating def) from dereferencing null for
    // the multi-def vregs that partial defs would create. Everything is
    // inserted directly before the pseudo so that it stays ahead of the
    // terminator cluster.
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();

    auto BuildFlipped = [&](Register Src) {
      Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      Register LoIn = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      Register Tmp = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
      // hi = 0 ; lo = x ^ 0x80. Single-def GPR8 temporaries throughout
      // (not tied-def instructions: TwoAddress runs after MachineCSE and
      // could not protect anything here anyway) -- this is exactly what
      // satisfies the one-def-per-vreg property that MachineCSE's
      // getUniqueVRegDef assumption above relies on, and it also keeps the
      // two flips of one compare from CSEing together, since their XORs
      // read different vregs.
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8ri), Hi).addImm(0);
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8rr), LoIn).addReg(Src);
      BuildMI(*BB, MI, DL, TII->get(MCS251::XOR8ri), Lo)
          .addReg(LoIn)
          .addImm(0x80);
      // wr = { hi:lo }
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), Tmp)
          .addReg(Hi)
          .addImm(MCS251::sub_hi8)
          .addReg(Lo)
          .addImm(MCS251::sub_lo8);
      return Tmp;
    };

    Register Lhs = BuildFlipped(MI.getOperand(0).getReg());
    Register Rhs = BuildFlipped(MI.getOperand(1).getReg());
    BuildMI(*BB, MI, DL, TII->get(MCS251::CMP16rr))
        .addReg(Lhs)
        .addReg(Rhs);
    return expandLongConditionalBranch(
        MI, BB, MI.getOperand(3).getMBB(),
        getSkipBranchOpcode8S((ISD::CondCode)MI.getOperand(2).getImm()));
  }
  }
}

MachineBasicBlock *MCS251TargetLowering::expandLongConditionalBranch(
    MachineInstr &MI, MachineBasicBlock *BB, MachineBasicBlock *TrueMBB,
    unsigned SkipBranchOpcode) const {
  // Layout invariant: the jCCinv->skip displacement is a fixed 4 bytes only
  // while SkipMBB stays physically adjacent to BB (it is inserted directly
  // behind it below), and nothing may ever be scheduled between them. This
  // is currently guaranteed through MachineBlockPlacement because
  //   * the CFG surgery below uses bare addSuccessor, so BB's successor
  //     probabilities are uniform -- the IR edge probabilities are dropped;
  //   * selectBestSuccessor only displaces the fall-through candidate when
  //     a successor is *strictly* more probable, and with uniform
  //     probabilities none is, so MBP keeps BB and SkipMBB adjacent;
  //   * SkipMBB has the single predecessor BB and is on the chain before
  //     MBP runs, so it is already BB's layout successor when BB is placed.
  // If IR !prof probabilities are ever preserved across this surgery, or
  // analyzeBranch is implemented (letting MBP or the branch folder reason
  // about and invert the jCCinv/ejmp pair), this guarantee breaks and the
  // expansion must move to a post-layout pass or a real branch relaxer.
  // The failure mode is loud, not silent: sdas251 rejects the out-of-range
  // rel8 at assembly time.
  MachineFunction *MF = BB->getParent();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const DebugLoc &DL = MI.getDebugLoc();

  // The skip block is placed directly after BB.
  MachineBasicBlock *SkipMBB = MF->CreateMachineBasicBlock();
  MF->insert(std::next(BB->getIterator()), SkipMBB);

  // Everything after the pseudo moves into SkipMBB: the EJMP of the
  // explicit false edge that ISD::BR lowered to. That EJMP is always
  // emitted -- SelectionDAG materialises the unconditional false edge, and
  // with no analyzeBranch there is no branch folding that could turn it
  // into a fall-through -- so SkipMBB is never empty.
  SkipMBB->splice(SkipMBB->end(), BB, std::next(MI.getIterator()), BB->end());

  // CFG surgery: every successor of BB except the true edge transfers to
  // SkipMBB. (SelectionDAGBuilder records both IR successors of the br i1,
  // so the false edge is always present here.) PHI nodes in the transferred
  // successors carry the old incoming block BB; rewrite them to SkipMBB,
  // otherwise PHIElimination would place the incoming copies into a block
  // that no longer reaches the successor. (A PHI's operand 0 is its defined
  // register; the (value, incoming-block) pairs start at operand 1, so the
  // incoming blocks sit at the even operand indices.)
  SmallVector<MachineBasicBlock *, 4> OldSuccs(BB->succ_begin(),
                                               BB->succ_end());
  while (!BB->succ_empty())
    BB->removeSuccessor(BB->succ_begin());
  for (MachineBasicBlock *S : OldSuccs) {
    if (S == TrueMBB)
      continue;
    SkipMBB->addSuccessor(S);
    for (MachineInstr &P : S->phis())
      for (unsigned Op = 2, E = P.getNumOperands(); Op < E; Op += 2)
        if (P.getOperand(Op).isMBB() && P.getOperand(Op).getMBB() == BB)
          P.getOperand(Op).setMBB(SkipMBB);
  }

  // jCCinv skip ; ejmp target. The jcc is 2 bytes, the ejmp 4, so the skip
  // target is always within rel8 range.
  BuildMI(BB, DL, TII->get(SkipBranchOpcode)).addMBB(SkipMBB);
  BuildMI(BB, DL, TII->get(MCS251::EJMP)).addMBB(TrueMBB);
  BB->addSuccessor(SkipMBB);
  BB->addSuccessor(TrueMBB);

  MI.eraseFromParent();
  return SkipMBB;
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
