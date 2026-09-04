//===-- MCS251ISelLowering.cpp - MCS-251 DAG lowering --------------------===//

#include "MCS251ISelLowering.h"
#include "MCS251.h"
#include "MCS251Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/GlobalValue.h"
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
  // GPR32 is registered so MVT::i32 is a legal type: an illegal i32 would be
  // taken apart by the type legalizer (an i32 load silently split into two
  // i16 loads) before the operation legalizer could reject it. As a legal
  // type every i32 memory access reaches LowerLoad/LowerStore's fatal error,
  // and every other i32 operation fails loudly at selection instead of
  // silently expanding into half-supported fragments. No instruction ever
  // moves a whole dr register in this phase.
  addRegisterClass(MVT::i32, &MCS251::GPR32RegClass);
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

  // DR has native arithmetic, while logical operations are custom-lowered to
  // the two WR lanes (the ISA has no DR-DR anl/orl/xrl form).
  setOperationAction(ISD::ADD, MVT::i32, Custom);
  setOperationAction(ISD::SUB, MVT::i32, Custom);
  setOperationAction(ISD::AND, MVT::i32, Custom);
  setOperationAction(ISD::OR, MVT::i32, Custom);
  setOperationAction(ISD::XOR, MVT::i32, Custom);
  setOperationAction(ISD::SETCC, MVT::i8, Custom);
  setOperationAction(ISD::SETCC, MVT::i16, Custom);

  // Phase 8: loads and stores. Custom (not MSP430-style pattern matching,
  // which works there because MSP430 has native 16-bit load/store
  // instructions and a single data address space): the MCS-251 has no 16-bit
  // memory access, so an i16 object must be decomposed into two byte
  // accesses with big-endian lane mapping, and selecting direct
  // (dir8, 0x80-0xff reaches the SFR space) versus indirect (@wr, the same
  // numeric address is region-00 edata) is a semantic decision per address
  // kind. Both live in LowerLoad/LowerStore.
  setOperationAction(ISD::LOAD, MVT::i8, Custom);
  setOperationAction(ISD::LOAD, MVT::i16, Custom);
  setOperationAction(ISD::STORE, MVT::i8, Custom);
  setOperationAction(ISD::STORE, MVT::i16, Custom);
  // i32 is a legal type in this backend (GPR32 exists) but no memory
  // instruction reaches it: mark Custom so the rejection in LowerLoad/
  // LowerStore is a clear fatal error instead of "Cannot select".
  setOperationAction(ISD::LOAD, MVT::i32, Custom);
  setOperationAction(ISD::STORE, MVT::i32, Custom);
  // Extending loads and truncating stores consult their OWN action tables
  // (default Legal, they do not inherit the LOAD/STORE action above), so
  // route them to the same custom lowering explicitly. i32 results are
  // rejected there.
  for (unsigned Ext :
       {ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}) {
    setLoadExtAction(Ext, MVT::i16, MVT::i8, Custom);
    setLoadExtAction(Ext, MVT::i32, MVT::i8, Custom);
    setLoadExtAction(Ext, MVT::i32, MVT::i16, Custom);
  }
  setTruncStoreAction(MVT::i16, MVT::i8, Custom);
  setTruncStoreAction(MVT::i32, MVT::i8, Custom);
  setTruncStoreAction(MVT::i32, MVT::i16, Custom);
  // Byte-to-word widening (zextload i8 and plain zext i8->i16) expands to
  // the ZEXT8 pseudo (zero hi lane + REG_SEQUENCE); anyext has the same
  // legal expansion. Sign extension has no instruction sequence yet and is
  // rejected in LowerOperation.
  setOperationAction(ISD::ZERO_EXTEND, MVT::i16, Custom);
  setOperationAction(ISD::ANY_EXTEND, MVT::i16, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i16, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::ANY_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i32, Custom);
  // Stack allocations (Phase 9): static allocas surface as FrameIndexSDNode
  // pointers handled by parseAddress (direct @dr60 access) and the FIADDR
  // Select hook (escaping pointer values); variable-length allocas lower
  // through DYNAMIC_STACKALLOC into the DYNALLOCA pseudo.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i8, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Custom);
  // Loop VLAs use stacksave/stackrestore to recover the pre-iteration SPX.
  // They cannot use the generic Copy{From,To}Reg expansion because DR60 is a
  // 32-bit register while pointers are i16.
  setOperationAction(ISD::STACKSAVE, MVT::i16, Custom);
  setOperationAction(ISD::STACKRESTORE, MVT::i16, Custom);
  // STACKSAVE/RESTORE are legalized through MVT::Other despite their i16
  // pointer payload, so register that lookup form as well.
  setOperationAction(ISD::STACKSAVE, MVT::Other, Custom);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Custom);
  // Atomics are separate opcodes (not flags on ISD::LOAD/STORE); route them
  // to a loud rejection -- "Cannot select" gives no hint what is missing.
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::ATOMIC_LOAD, VT, Custom);
    setOperationAction(ISD::ATOMIC_STORE, VT, Custom);
  }
}

const char *MCS251TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case MCS251ISD::ERET:
    return "MCS251ISD::ERET";
  case MCS251ISD::CALL:
    return "MCS251ISD::CALL";
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
  case ISD::ADD:
  case ISD::SUB:
    if (Op.getValueType() == MVT::i32)
      return LowerArithmetic32(Op, DAG);
    llvm_unreachable("unexpected arithmetic type");
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    if (Op.getValueType() == MVT::i32)
      return LowerLogical32(Op, DAG);
    llvm_unreachable("unexpected logical type");
  case ISD::LOAD:
    return LowerLoad(Op, DAG);
  case ISD::STORE:
    return LowerStore(Op, DAG);
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    return LowerExtend(Op, DAG);
  case ISD::SIGN_EXTEND:
    report_fatal_error(
        "MCS251: sign extension is not supported (no 8-to-16 bit sign "
        "extension sequence yet)");
  case ISD::BRCOND:
  case ISD::SETCC:
    report_fatal_error(
        "MCS251: Phase 6 supports only BR_CC (branch on icmp); "
        "SETCC/BRCOND arrive in a later phase");
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDynamicStackAlloc(Op, DAG);
  case ISD::STACKSAVE:
    return LowerSTACKSAVE(Op, DAG);
  case ISD::STACKRESTORE:
    return LowerSTACKRESTORE(Op, DAG);
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE:
    report_fatal_error("MCS251: atomic memory operations are not supported");
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
  if (N.getValueType() == MVT::i16)
    return SDValue(
        DAG.getMachineNode(
            MCS251::MOV16ri, DL, MVT::i16,
            DAG.getTargetConstant(C->getAPIntValue().trunc(16).getZExtValue(),
                                  DL, MVT::i16)),
        0);
  assert(N.getValueType() == MVT::i32 && "unexpected immediate type");
  return SDValue(DAG.getMachineNode(
                     MCS251::MOV32ri, DL, MVT::i32,
                     DAG.getTargetConstant(C->getAPIntValue().trunc(32), DL,
                                           MVT::i32)),
                 0);
}

static SDValue extractLane(SDValue V, unsigned SR, const SDLoc &DL,
                           SelectionDAG &DAG) {
  return DAG.getTargetExtractSubreg(SR, DL, MVT::i16, V);
}

static SDValue makeWord(SDValue Hi, SDValue Lo, const SDLoc &DL,
                        SelectionDAG &DAG) {
  SmallVector<SDValue, 5> Ops = {
      DAG.getTargetConstant(MCS251::GPR16RegClassID, DL, MVT::i32), Hi,
      DAG.getTargetConstant(MCS251::sub_hi8, DL, MVT::i32), Lo,
      DAG.getTargetConstant(MCS251::sub_lo8, DL, MVT::i32)};
  return SDValue(DAG.getMachineNode(TargetOpcode::REG_SEQUENCE, DL, MVT::i16,
                                    Ops),
                 0);
}

static SDValue makeDR(SDValue Hi, SDValue Lo, const SDLoc &DL,
                      SelectionDAG &DAG) {
  SmallVector<SDValue, 5> Ops = {
      DAG.getTargetConstant(MCS251::GPR32RegClassID, DL, MVT::i32), Lo,
      DAG.getTargetConstant(MCS251::sub_lo16, DL, MVT::i32), Hi,
      DAG.getTargetConstant(MCS251::sub_hi16, DL, MVT::i32)};
  return SDValue(DAG.getMachineNode(TargetOpcode::REG_SEQUENCE, DL, MVT::i32,
                                    Ops),
                 0);
}

// i32 ABI byte-register order is least-significant byte first: DPL, DPH, B,
// A. CC_MCS251/RetCC_MCS251 use DPL only as a single-value gatekeeper; all four
// lanes are explicitly transferred by splitI32ToBytes/combineI32FromBytes and
// the i32 formal/call/return lowering below.
static const MCPhysReg I32ABIRegs[] = {MCS251::DPL, MCS251::DPH, MCS251::B,
                                       MCS251::A};

static void splitI32ToBytes(SDValue Value, const SDLoc &DL, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &Parts) {
  if (isa<ConstantSDNode>(Value))
    Value = materializeImm(Value, DL, DAG);
  SDValue Lo = extractLane(Value, MCS251::sub_lo16, DL, DAG);
  SDValue Hi = extractLane(Value, MCS251::sub_hi16, DL, DAG);
  // ABI order is low byte first: DPL, DPH, B, A.
  Parts.push_back(DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8,
                                             Lo));
  Parts.push_back(DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8,
                                             Lo));
  Parts.push_back(DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8,
                                             Hi));
  Parts.push_back(DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8,
                                             Hi));
}

static SDValue combineI32FromBytes(ArrayRef<SDValue> Parts, const SDLoc &DL,
                                   SelectionDAG &DAG) {
  assert(Parts.size() == 4 && "i32 ABI values have four byte parts");
  SDValue Lo = makeWord(Parts[1], Parts[0], DL, DAG);
  SDValue Hi = makeWord(Parts[3], Parts[2], DL, DAG);
  return makeDR(Hi, Lo, DL, DAG);
}

// Known limitation resolved (Phase 9): -O0's fast register allocator spills
// cross-block live vregs through loadRegFromStackSlot/storeRegFromStackSlot,
// which the frame now provides; the entry-block COPY shape below no longer
// needs any special care.
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
  case MCS251::MOV32ri: {
    MachineFunction *MF = BB->getParent();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Dst = MI.getOperand(0).getReg();
    uint32_t Value = (uint32_t)MI.getOperand(1).getImm();
    // MOV must precede MOVH: MOV clears the high word, whereas MOVH writes
    // only the high word, so the two instructions are not interchangeable.
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOVDRri), Dst)
        .addImm(Value & 0xffff);
    if (Value >> 16)
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOVHDRi), Dst)
          .addReg(Dst)
          .addImm(Value >> 16);
    MI.eraseFromParent();
    return BB;
  }
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
  case MCS251::LD16S:
  case MCS251::ST16S:
  case MCS251::ST16TS: {
    // Phase 9 frame-relative memory pseudos, mirror images of
    // LD16/ST16/ST16T with the address carried as an unresolved
    // (placeholder base, FrameIndex, offset) triple. Explicit operand
    // order: LD16S [dst, base, FI, off], ST16S/ST16TS [base, FI, off, val].
    // The displacement handed to the expanded MOV8rmS/MOV8mrS stays an
    // (FI, off) pair -- PEI resolves each against the frame top.
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();

    bool IsLoad = MI.getOpcode() == MCS251::LD16S;
    bool IsWideStore = MI.getOpcode() == MCS251::ST16S;
    unsigned BaseOp = IsLoad ? 1 : 0;
    int FI = MI.getOperand(BaseOp + 1).getIndex();
    uint64_t Disp = MI.getOperand(BaseOp + 2).getImm() & 0xffff;
    MachineMemOperand *MMO =
        MI.memoperands_empty() ? nullptr : *MI.memoperands_begin();
    assert(MMO && "load/store custom inserter requires a MachineMemOperand");

    auto SplitMMO = [&](uint64_t Off) {
      return MF->getMachineMemOperand(MMO, Off, /*Size=*/1);
    };
    // MOV8rmS/MOV8mrS with an unresolved frame address (the placeholder
    // base is re-attached per instruction; eliminateFrameIndex replaces it
    // with the real frame base register).
    auto BuildFrameMI = [&](unsigned Opc, uint64_t Off) {
      return BuildMI(*BB, MI, DL, TII->get(Opc))
          .addReg(MCS251::DR60)
          .addFrameIndex(FI)
          .addImm(Off);
    };

    if (IsLoad) {
      Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8rmS), Hi)
          .addReg(MCS251::DR60)
          .addFrameIndex(FI)
          .addImm(Disp)
          .setMemRefs(SplitMMO(0));
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8rmS), Lo)
          .addReg(MCS251::DR60)
          .addFrameIndex(FI)
          .addImm(Disp + 1)
          .setMemRefs(SplitMMO(1));
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE),
              MI.getOperand(0).getReg())
          .addReg(Hi)
          .addImm(MCS251::sub_hi8)
          .addReg(Lo)
          .addImm(MCS251::sub_lo8);
    } else {
      // Stores extract the lanes from the GPR16 value with sub-register
      // COPYs (def before use, keeping every temporary single-def).
      Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), Lo)
          .addReg(MI.getOperand(3).getReg(), RegState::NoFlags,
                  MCS251::sub_lo8);
      if (IsWideStore) {
        Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
        BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), Hi)
            .addReg(MI.getOperand(3).getReg(), RegState::NoFlags,
                    MCS251::sub_hi8);
        BuildFrameMI(MCS251::MOV8mrS, Disp).addReg(Hi).setMemRefs(SplitMMO(0));
      }
      BuildFrameMI(MCS251::MOV8mrS, IsWideStore ? Disp + 1 : Disp)
          .addReg(Lo)
          .setMemRefs(IsWideStore ? SplitMMO(1) : MMO);
    }

    MI.eraseFromParent();
    return BB;
  }
  case MCS251::FIADDR: {
    // Frame-index pointer materialisation: read SPX through the two SFR
    // direct addresses (0x81 = SP, 0x85 = SPH on this platform), assemble
    // the WR, and let ADD16fi carry the frame index to PEI, where the final
    // displacement (depends on StackSize) is folded.
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    int FI = MI.getOperand(1).getIndex();

    Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    Register Sp = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8di), Lo)
        .addImm(0x81); // SP (SFR direct)
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8di), Hi)
        .addImm(0x85); // SPH (SFR direct, QEMU-arbitrated address)
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), Sp)
        .addReg(Hi)
        .addImm(MCS251::sub_hi8)
        .addReg(Lo)
        .addImm(MCS251::sub_lo8);
    BuildMI(*BB, MI, DL, TII->get(MCS251::ADD16fi),
            MI.getOperand(0).getReg())
        .addReg(Sp)
        .addFrameIndex(FI)
        .addImm(0);
    MI.eraseFromParent();
    return BB;
  }
  case MCS251::DYNALLOCA: {
    // Dynamic alloca (SFR-direct SPX read/modify/write -- `add dr60,wr` is
    // an illegal width mix; see the DYNALLOCA comment in
    // MCS251InstrInfo.td). The returned pointer is the object base =
    // oldSPX+1 (up-growing stack, SPX at the top-most used byte).
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Size = MI.getOperand(1).getReg();

    Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    Register Sp = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    Register New = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    Register NewLo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    Register NewHi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8di), Lo).addImm(0x81);
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8di), Hi).addImm(0x85);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), Sp)
        .addReg(Hi)
        .addImm(MCS251::sub_hi8)
        .addReg(Lo)
        .addImm(MCS251::sub_lo8);
    BuildMI(*BB, MI, DL, TII->get(MCS251::ADD16rr), New)
        .addReg(Sp)
        .addReg(Size);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), NewLo)
        .addReg(New, RegState::NoFlags, MCS251::sub_lo8);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), NewHi)
        .addReg(New, RegState::NoFlags, MCS251::sub_hi8);
    // Write SPX back high byte first: if a mid-update interrupt fires, the
    // transient SPX (newHi:oldLo) is >= the new stack top, so the interrupt
    // push lands above the new object instead of overwriting live frames.
    // The reverse order would transiently drop SPX below the old top whenever
    // the new value carries into the high byte (crossing a 256-byte page).
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8id)).addImm(0x85).addReg(NewHi);
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8id)).addImm(0x81).addReg(NewLo);
    BuildMI(*BB, MI, DL, TII->get(MCS251::ADD16ri),
            MI.getOperand(0).getReg())
        .addReg(Sp)
        .addImm(1);
    MI.eraseFromParent();
    return BB;
  }
  case MCS251::LD16:
  case MCS251::ST16:
  case MCS251::ST16T: {
    // Phase 8 memory pseudos. Explicit operand order: LD16 [dst, base,
    // disp], ST16/ST16T [base, disp, val]. The pseudo's MachineMemOperand
    // (size 2 for LD16/ST16) is split into per-byte MMOs so the expansion
    // instructions carry exact size/offset info (the volatile flag survives
    // the split, keeping volatile accesses un-mergeable and ordered).
    //
    // Endianness (measurement-verified): 16-bit objects in edata memory are
    // big-endian -- mem[base+Disp] is the high byte, mem[base+Disp+1] the
    // low byte -- matching the WR register file where sub_hi8 is the
    // lower-numbered byte slot.
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();

    bool IsLoad = MI.getOpcode() == MCS251::LD16;
    bool IsWideStore = MI.getOpcode() == MCS251::ST16;
    unsigned BaseOp = IsLoad ? 1 : 0;
    Register Base = MI.getOperand(BaseOp).getReg();
    uint64_t Disp = MI.getOperand(BaseOp + 1).getImm() & 0xffff;
    // Lowering guarantees Disp <= 0xfffe, so Disp + 1 cannot wrap.
    Register Val = IsLoad ? Register(0) : MI.getOperand(2).getReg();
    MachineMemOperand *MMO =
        MI.memoperands_empty() ? nullptr : *MI.memoperands_begin();
    assert(MMO && "load/store custom inserter requires a MachineMemOperand");

    auto SplitMMO = [&](uint64_t Off) {
      return MF->getMachineMemOperand(MMO, Off, /*Size=*/1);
    };

    if (IsLoad) {
      Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8rmD), Hi)
          .addReg(Base)
          .addImm(Disp)
          .setMemRefs(SplitMMO(0));
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8rmD), Lo)
          .addReg(Base)
          .addImm(Disp + 1)
          .setMemRefs(SplitMMO(1));
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE),
              MI.getOperand(0).getReg())
          .addReg(Hi)
          .addImm(MCS251::sub_hi8)
          .addReg(Lo)
          .addImm(MCS251::sub_lo8);
    } else {
      // Stores extract the lanes from the GPR16 value with sub-register
      // COPYs (def before use, keeping every temporary single-def).
      Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), Lo)
          .addReg(Val, RegState::NoFlags, MCS251::sub_lo8);
      if (IsWideStore) {
        Register Hi = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
        BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), Hi)
            .addReg(Val, RegState::NoFlags, MCS251::sub_hi8);
        BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8mrD))
            .addReg(Base)
            .addImm(Disp)
            .addReg(Hi)
            .setMemRefs(SplitMMO(0));
      }
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8mrD))
          .addReg(Base)
          .addImm(IsWideStore ? Disp + 1 : Disp)
          .addReg(Lo)
          .setMemRefs(IsWideStore ? SplitMMO(1) : MMO);
    }

    MI.eraseFromParent();
    return BB;
  }
  case MCS251::ST16TD: {
    // Direct-address truncating store (constant address <= 0xff, including
    // the SFR space): store the value's low lane through the dir8 form.
    // Explicit operands [addr, val].
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Lo = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), Lo)
        .addReg(MI.getOperand(1).getReg(), RegState::NoFlags,
                MCS251::sub_lo8);
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8id))
        .addImm(MI.getOperand(0).getImm() & 0xff)
        .addReg(Lo)
        .setMemRefs(MI.memoperands());
    MI.eraseFromParent();
    return BB;
  }
  case MCS251::ZEXT8: {
    // %dst = { 0, %src }: zero hi lane + REG_SEQUENCE (single-def vregs
    // throughout, same MachineCSE-safety shape as BuildFlipped above).
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Zero = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8ri), Zero).addImm(0);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE),
            MI.getOperand(0).getReg())
        .addReg(Zero)
        .addImm(MCS251::sub_hi8)
        .addReg(MI.getOperand(1).getReg())
        .addImm(MCS251::sub_lo8);
    MI.eraseFromParent();
    return BB;
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

//===----------------------------------------------------------------------===//
//  Load/store lowering (Phase 8)
//===----------------------------------------------------------------------===//
//
// The MCS-251 has no 16-bit memory access instruction, and its byte-access
// forms span two different address spaces:
//
//   * direct (dir8): 0x00-0x7f page-zero edata, 0x80-0xff SFR
//   * @wr indirect / @wr+dis16: region-00 edata, 16-bit
//
// ADDRESS-SPACE TRAP (measurement-verified, do not change): the same numeric
// address 0x80-0xff is the SFR space through direct addressing but plain
// region-00 edata through @wr. SFRs are therefore ONLY reachable via
// MOV8di/MOV8id, which parseAddress guarantees by mapping every constant
// address K <= 0xff to the direct form (this also covers page-zero edata,
// which is address-identical in both spaces). Everything else -- runtime
// pointers, globals, displaced addresses -- goes through a 16-bit base in
// @wr form.
//
// i16 objects are accessed as two bytes with BIG-ENDIAN lane mapping
// (measurement-verified: 16-bit objects in edata memory are big-endian,
// mem[base] = high byte, matching the WR register file where the MSB lives
// in the lower byte slot = sub_hi8). The decomposition needs REG_SEQUENCE /
// sub-register copies, which are MIR-only, so an i16 access is emitted as
// the LD16/ST16/ST16T pseudo and split in EmitInstrWithCustomInserter.
//
// volatile: the MachineMemOperand of the original load/store (which carries
// the volatile flag) is attached to every emitted machine node, so ordering
// and non-merging are enforced by the scheduler through the MMO; no extra
// handling is required here.

namespace {
// One classified load/store address.
struct MCS251Address {
  // Register base: a GPR16 vreg pointer, or a value materialised here into
  // one (constants, globals via `mov wr,#_sym`).
  SDValue Base;
  // Constant displacement carried by the dis16 operand (0..0xFFFE); a plain
  // undisplaced access has Disp == 0. Values outside the range are folded
  // into Base by a real 16-bit add instead (parseAddress never leaves them
  // here; 0xffff is excluded because @wr+0xffff wraparound was not
  // measured, while `add wr,#imm16` wraparound is the plain 16-bit add).
  int64_t Disp = 0;
  // Direct addressing (i8 accesses only): constant address <= 0xff via the
  // dir8 form, covering page-zero edata (0x00-0x7f) and the SFR space
  // (0x80-0xff) -- see the trap above.
  bool IsDirect = false;
  uint64_t DirectAddr = 0;
  // Frame-relative addressing (Phase 9): the base is a stack object. The
  // displacement is carried as an unresolved (FrameIndex, offset) pair into
  // the mcs251_stack operand; PEI folds it against the frame top
  // (@dr60-0x.... normally, @dr56 when the function has dynamic allocas).
  bool IsStack = false;
  int StackFI = 0;
};
} // namespace

// Materialises a frame-index pointer as a plain GPR16 value (the FIADDR
// pseudo; the custom inserter builds the SFR-direct SP reads). Only used
// when a frame pointer participates in REGISTER arithmetic (alloca[i]):
// a direct load/store base stays frame-relative and never materialises.
static SDValue materializeFrameIndex(int FrameIdx, int64_t Off,
                                     const SDLoc &DL, SelectionDAG &DAG) {
  if (DAG.getMachineFunction().getFrameInfo().hasVarSizedObjects())
    report_fatal_error("MCS251: taking the address of a frame object in a "
                       "function with dynamic allocas is not supported "
                       "(needs an anchor read, not yet implemented)");
  SDValue TFI = DAG.getTargetFrameIndex(FrameIdx, MVT::i16);
  SDValue P(DAG.getMachineNode(MCS251::FIADDR, DL, MVT::i16, TFI), 0);
  if (Off == 0)
    return P;
  return SDValue(DAG.getMachineNode(
                     MCS251::ADD16ri, DL, MVT::i16,
                     {P, DAG.getTargetConstant(Off & 0xffff, DL, MVT::i16)}),
                 0);
}

// Materialise a 16-bit constant into a GPR16 vreg.
static SDValue buildMOV16ri(uint64_t Imm, const SDLoc &DL, SelectionDAG &DAG) {
  return SDValue(
      DAG.getMachineNode(MCS251::MOV16ri, DL, MVT::i16,
                         DAG.getTargetConstant(Imm & 0xffff, DL, MVT::i16)),
      0);
}

// Fold a displacement into a register base with a real 16-bit add
// (add wr,#imm16 wraps mod 2^16, which is the correct region-00 semantics).
static SDValue foldDispIntoBase(SDValue Base, int64_t Disp, const SDLoc &DL,
                                SelectionDAG &DAG) {
  if (Disp == 0)
    return Base;
  return SDValue(DAG.getMachineNode(
                     MCS251::ADD16ri, DL, MVT::i16,
                     {Base, DAG.getTargetConstant(Disp & 0xffff, DL,
                                                  MVT::i16)}),
                 0);
}

// Classify a load/store pointer. AllowDirect permits the dir8 form (i8
// accesses only; an i16 access must never use it: bytes 0x7f|0x80 would
// silently straddle the page-zero/SFR boundary, and the SFR space is not
// contiguous i16 storage anyway).
static MCS251Address parseAddress(SDValue Ptr, const SDLoc &DL,
                                  SelectionDAG &DAG, bool AllowDirect) {
  MCS251Address A;
  int64_t Off = 0;

  // Peel constant offsets: (add p, K). The DAG combiner folds constant
  // chains, so more than one level here is defensive. NOTE: offsets are
  // read zero-extended: region-00 pointer arithmetic is mod 2^16, so the
  // i16 constant 0xfffe is +65534 (dis16-able), not -2.
  while (Ptr.getOpcode() == ISD::ADD) {
    SDValue LHS = Ptr.getOperand(0), RHS = Ptr.getOperand(1);
    if (auto *C = dyn_cast<ConstantSDNode>(RHS)) {
      Off += (int64_t)C->getZExtValue();
      Ptr = LHS;
      continue;
    }
    if (auto *C = dyn_cast<ConstantSDNode>(LHS)) {
      Off += (int64_t)C->getZExtValue();
      Ptr = RHS;
      continue;
    }
    // Register + register: materialise one 16-bit add. Recursing keeps
    // nested folded offsets (add (add p, 1), q) out of the operands. A
    // frame-index side leaves the frame-relative form here: it becomes a
    // plain pointer (materializeFrameIndex) so the register add covers it
    // (this is the alloca[i] shape).
    MCS251Address L = parseAddress(LHS, DL, DAG, /*AllowDirect=*/false);
    MCS251Address R = parseAddress(RHS, DL, DAG, /*AllowDirect=*/false);
    assert(!L.IsDirect && !R.IsDirect && "direct requires AllowDirect");
    SDValue LBase =
        L.IsStack ? materializeFrameIndex(L.StackFI, L.Disp, DL, DAG)
                  : foldDispIntoBase(L.Base, L.Disp, DL, DAG);
    SDValue RBase =
        R.IsStack ? materializeFrameIndex(R.StackFI, R.Disp, DL, DAG)
                  : foldDispIntoBase(R.Base, R.Disp, DL, DAG);
    Ptr = SDValue(DAG.getMachineNode(MCS251::ADD16rr, DL, MVT::i16,
                                     {LBase, RBase}),
                  0);
    break;
  }

  // A stack object used directly as the access base: stay frame-relative
  // (one @dr60 access, no pointer materialisation). The peeled offset rides
  // the frame displacement; the same 0..0xfffe range as dis16 applies.
  if (auto *FIN = dyn_cast<FrameIndexSDNode>(Ptr)) {
    A.IsStack = true;
    A.StackFI = FIN->getIndex();
    if (Off < 0 || Off > 0xfffe)
      report_fatal_error("MCS251: frame object access offset out of the "
                         "16-bit displacement range");
    A.Disp = Off;
    return A;
  }

  // Symbolic bases: `mov wr,#_sym` (symbolic immediate, Phase 8
  // measurement: sdas251 accepts it and sdld resolves the relocation; an
  // offset stays symbolic as #(_sym+off)). The displacement stays 0 so the
  // offset rides the immediate instead of a dis16 byte pair.
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Ptr)) {
    int64_t SymOff = GA->getOffset() + Off;
    if (SymOff < 0 || SymOff > 0xffff)
      report_fatal_error("MCS251: global address offset out of 16-bit "
                         "range");
    A.Base = SDValue(
        DAG.getMachineNode(MCS251::MOV16ri, DL, MVT::i16,
                           DAG.getTargetGlobalAddress(GA->getGlobal(), DL,
                                                       MVT::i16, SymOff)),
        0);
    return A;
  }
  if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Ptr)) {
    A.Base = SDValue(DAG.getMachineNode(
                         MCS251::MOV16ri, DL, MVT::i16,
                         DAG.getTargetExternalSymbol(ES->getSymbol(),
                                                     MVT::i16)),
                     0);
    // External symbols carry no offset field; fold Off below.
  } else if (isa<ConstantPoolSDNode>(Ptr)) {
    // The MC layer has no MO_ConstantPoolIndex lowering; rejecting here is
    // honest instead of failing later with a misleading operand error.
    report_fatal_error(
        "MCS251: constant-pool addresses are not supported (constant "
        "islands arrive with a later phase)");
  } else if (auto *C = dyn_cast<ConstantSDNode>(Ptr)) {
    // Absolute constant address. Direct addressing for K <= 0xff (page-zero
    // edata or SFR -- the SFR is ONLY reachable this way, see the trap
    // above); otherwise materialise and access region-00 edata via @wr.
    // Pointer arithmetic wraps mod 2^16 (region-00 is a 16-bit space).
    uint64_t K = (C->getZExtValue() + (uint64_t)(int64_t)Off) & 0xffff;
    if (AllowDirect && K <= 0xff) {
      A.IsDirect = true;
      A.DirectAddr = K;
      return A;
    }
    A.Base = buildMOV16ri(K, DL, DAG);
    return A;
  } else if (isa<JumpTableSDNode>(Ptr) || isa<BlockAddressSDNode>(Ptr)) {
    report_fatal_error(
        "MCS251: jump-table/block-address data addresses are not supported");
  } else {
    // Plain register base (a GPR16 vreg pointer). Off within the dis16
    // range rides the displacement (one instruction, no base clobber);
    // outside it is folded into the base with a real add (wrap semantics).
    A.Base = Ptr;
    if (Off >= 0 && Off <= 0xfffe)
      A.Disp = Off;
    else
      A.Base = foldDispIntoBase(Ptr, Off, DL, DAG);
    return A;
  }

  // External-symbol base with a folded offset: same displacement treatment
  // as the register-base case (the symbol itself accepts no offset).
  if (Off >= 0 && Off <= 0xfffe)
    A.Disp = Off;
  else
    A.Base = foldDispIntoBase(A.Base, Off, DL, DAG);
  return A;
}

// Build the byte-load machine node for an i8 memory object, classified per
// parseAddress, and attach the original MachineMemOperand (volatile flag
// and aliasing info) to it.
static SDValue buildByteLoad(const MCS251Address &A, const SDLoc &DL,
                             SelectionDAG &DAG, SDValue Chain,
                             MachineMemOperand *MMO) {
  SDVTList ResTys = DAG.getVTList(MVT::i8, MVT::Other);
  SDNode *N;
  if (A.IsStack) {
    // mov rX, @dr60+<FI+off>: unresolved (placeholder base, FI, offset)
    // operands; PEI folds the displacement (see eliminateFrameIndex).
    N = DAG.getMachineNode(
        MCS251::MOV8rmS, DL, ResTys,
        {DAG.getRegister(MCS251::DR60, MVT::i16),
         DAG.getTargetFrameIndex(A.StackFI, MVT::i16),
         DAG.getTargetConstant(A.Disp, DL, MVT::i16), Chain});
  } else if (A.IsDirect)
    N = DAG.getMachineNode(MCS251::MOV8di, DL, ResTys,
                           {DAG.getTargetConstant(A.DirectAddr, DL, MVT::i8),
                            Chain});
  else if (A.Disp == 0)
    N = DAG.getMachineNode(MCS251::MOV8rm, DL, ResTys, {A.Base, Chain});
  else
    N = DAG.getMachineNode(
        MCS251::MOV8rmD, DL, ResTys,
        {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Chain});
  DAG.setNodeMemRefs(cast<MachineSDNode>(N), {MMO});
  return SDValue(N, 0);
}

SDValue MCS251TargetLowering::LowerLoad(SDValue Op, SelectionDAG &DAG) const {
  auto *LD = cast<LoadSDNode>(Op);
  SDLoc DL(Op);
  EVT MemVT = LD->getMemoryVT();
  EVT ValVT = LD->getValueType(0);

  if (LD->isAtomic())
    report_fatal_error("MCS251: atomic memory operations are not supported");
  if (MemVT != MVT::i8 && MemVT != MVT::i16)
    report_fatal_error("MCS251: only i8/i16 memory objects are supported "
                       "(load)");
  // Extending load results must be widenable: i32 (a GPR32 value) has no
  // instructions, reject before an i8 node could be returned to an i32 user.
  if (ValVT != MVT::i8 && ValVT != MVT::i16)
    report_fatal_error("MCS251: only i8/i16 values are supported (load)");
  if (LD->getExtensionType() == ISD::SEXTLOAD)
    report_fatal_error("MCS251: sign-extending loads are not supported (no "
                       "8-to-16 bit sign extension yet)");

  MCS251Address A = parseAddress(LD->getBasePtr(), DL, DAG,
                                 /*AllowDirect=*/MemVT == MVT::i8);

  if (MemVT == MVT::i16) {
    // Two byte loads + REG_SEQUENCE via the LD16 pseudo (big-endian: the
    // pseudo's disp 0 lane is the hi lane). The pseudo carries the full MMO
    // (size 2); the split MMOs are attached by the custom inserter.
    SDNode *N;
    if (A.IsStack) {
      N = DAG.getMachineNode(
          MCS251::LD16S, DL, DAG.getVTList(MVT::i16, MVT::Other),
          {DAG.getRegister(MCS251::DR60, MVT::i16),
           DAG.getTargetFrameIndex(A.StackFI, MVT::i16),
           DAG.getTargetConstant(A.Disp, DL, MVT::i16), LD->getChain()});
    } else {
      N = DAG.getMachineNode(
          MCS251::LD16, DL, DAG.getVTList(MVT::i16, MVT::Other),
          {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16),
           LD->getChain()});
    }
    DAG.setNodeMemRefs(cast<MachineSDNode>(N), {LD->getMemOperand()});
    return SDValue(N, 0);
  }

  // i8 memory object: value is i8 (plain) or i16 (zero/any-extending load).
  SDValue Res =
      buildByteLoad(A, DL, DAG, LD->getChain(), LD->getMemOperand());
  if (LD->getValueType(0) == MVT::i16) {
    // Widen with a zero hi lane (anyext: high bits undefined, zeroing is a
    // legal choice). Chain passthrough keeps the load ordered.
    SDNode *Z = DAG.getMachineNode(MCS251::ZEXT8, DL,
                                   DAG.getVTList(MVT::i16, MVT::Other),
                                   {Res, Res.getValue(1)});
    return SDValue(Z, 0);
  }
  return Res;
}

// Materialise an i8 constant value into a GPR8 vreg for the store forms.
static SDValue buildMOV8ri(uint64_t Imm, const SDLoc &DL, SelectionDAG &DAG) {
  return SDValue(
      DAG.getMachineNode(MCS251::MOV8ri, DL, MVT::i8,
                         DAG.getTargetConstant(Imm & 0xff, DL, MVT::i8)),
      0);
}

// Build the byte-store machine node for an i8 memory object (value must
// already be a GPR8 vreg), classified per parseAddress, with the original
// MachineMemOperand attached.
static SDValue buildByteStore(const MCS251Address &A, SDValue Val,
                              const SDLoc &DL, SelectionDAG &DAG,
                              SDValue Chain, MachineMemOperand *MMO) {
  SDNode *N;
  if (A.IsStack) {
    // mov @dr60+<FI+off>, rX -- mirror of the load path.
    N = DAG.getMachineNode(
        MCS251::MOV8mrS, DL, MVT::Other,
        {DAG.getRegister(MCS251::DR60, MVT::i16),
         DAG.getTargetFrameIndex(A.StackFI, MVT::i16),
         DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val, Chain});
  } else if (A.IsDirect)
    N = DAG.getMachineNode(
        MCS251::MOV8id, DL, MVT::Other,
        {DAG.getTargetConstant(A.DirectAddr, DL, MVT::i8), Val, Chain});
  else if (A.Disp == 0)
    N = DAG.getMachineNode(MCS251::MOV8mr, DL, MVT::Other,
                           {A.Base, Val, Chain});
  else
    N = DAG.getMachineNode(
        MCS251::MOV8mrD, DL, MVT::Other,
        {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val, Chain});
  DAG.setNodeMemRefs(cast<MachineSDNode>(N), {MMO});
  return SDValue(N, 0);
}

SDValue MCS251TargetLowering::LowerStore(SDValue Op, SelectionDAG &DAG) const {
  auto *ST = cast<StoreSDNode>(Op);
  SDLoc DL(Op);
  EVT MemVT = ST->getMemoryVT();
  SDValue Val = ST->getValue();

  if (ST->isAtomic())
    report_fatal_error("MCS251: atomic memory operations are not supported");
  if (MemVT != MVT::i8 && MemVT != MVT::i16)
    report_fatal_error("MCS251: only i8/i16 memory objects are supported "
                       "(store)");

  MCS251Address A = parseAddress(ST->getBasePtr(), DL, DAG,
                                 /*AllowDirect=*/MemVT == MVT::i8);

  if (MemVT == MVT::i16) {
    // i16 memory object: two byte stores via the ST16 pseudo (value lane
    // extraction is MIR-only). $val must be a GPR16 vreg; constants are
    // materialised here.
    if (isa<ConstantSDNode>(Val))
      Val = buildMOV16ri(cast<ConstantSDNode>(Val)->getZExtValue(), DL, DAG);
    SDNode *N;
    if (A.IsStack) {
      N = DAG.getMachineNode(
          MCS251::ST16S, DL, MVT::Other,
          {DAG.getRegister(MCS251::DR60, MVT::i16),
           DAG.getTargetFrameIndex(A.StackFI, MVT::i16),
           DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val,
           ST->getChain()});
    } else {
      N = DAG.getMachineNode(
          MCS251::ST16, DL, MVT::Other,
          {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val,
           ST->getChain()});
    }
    DAG.setNodeMemRefs(cast<MachineSDNode>(N), {ST->getMemOperand()});
    return SDValue(N, 0);
  }

  // i8 memory object: value is i8 (plain) or i16 (truncating store, low
  // lane). A constant truncation is shortened to a plain byte store.
  if (ST->isTruncatingStore()) {
    if (auto *C = dyn_cast<ConstantSDNode>(Val))
      return buildByteStore(A, buildMOV8ri(C->getZExtValue(), DL, DAG), DL,
                            DAG, ST->getChain(), ST->getMemOperand());
    // Non-constant i16 value: the low-lane extraction is MIR-only. A
    // direct-classified address keeps the dir8 form via ST16TD -- falling
    // back to @wr would silently retarget 0x80-0xff from the SFR space to
    // region-00 edata (the address-space trap). A frame-relative address
    // likewise stays frame-relative (ST16TS).
    SmallVector<SDValue, 4> Ops;
    unsigned Opc;
    if (A.IsDirect) {
      Opc = MCS251::ST16TD;
      Ops.push_back(DAG.getTargetConstant(A.DirectAddr, DL, MVT::i8));
    } else if (A.IsStack) {
      Opc = MCS251::ST16TS;
      Ops.push_back(DAG.getRegister(MCS251::DR60, MVT::i16));
      Ops.push_back(DAG.getTargetFrameIndex(A.StackFI, MVT::i16));
      Ops.push_back(DAG.getTargetConstant(A.Disp, DL, MVT::i16));
    } else {
      Opc = MCS251::ST16T;
      Ops.push_back(A.Base);
      Ops.push_back(DAG.getTargetConstant(A.Disp, DL, MVT::i16));
    }
    Ops.push_back(Val);
    Ops.push_back(ST->getChain());
    SDNode *N = DAG.getMachineNode(Opc, DL, MVT::Other, Ops);
    DAG.setNodeMemRefs(cast<MachineSDNode>(N), {ST->getMemOperand()});
    return SDValue(N, 0);
  }

  if (isa<ConstantSDNode>(Val))
    Val = buildMOV8ri(cast<ConstantSDNode>(Val)->getZExtValue(), DL, DAG);
  return buildByteStore(A, Val, DL, DAG, ST->getChain(), ST->getMemOperand());
}

SDValue MCS251TargetLowering::LowerExtend(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  if (Op.getValueType() != MVT::i16)
    report_fatal_error("MCS251: i32 values are not supported");
  SDValue Src = Op.getOperand(0);
  assert(Src.getValueType() == MVT::i8 && "only i8->i16 extension is routed "
                                          "here");
  return SDValue(DAG.getMachineNode(MCS251::ZEXT8, DL, MVT::i16, Src), 0);
}

SDValue MCS251TargetLowering::LowerArithmetic32(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  bool IsSub = Op.getOpcode() == ISD::SUB;

  // DAGCombiner canonicalises `sub x, C` as `add x, -C` before this custom
  // lowering runs. Recover native DR subtraction for a negative addend;
  // both forms have identical modulo-2^32 value semantics.
  if (!IsSub)
    if (auto *C = dyn_cast<ConstantSDNode>(RHS))
      if (C->getAPIntValue().isNegative()) {
        RHS = DAG.getConstant(-C->getAPIntValue(), DL, MVT::i32);
        IsSub = true;
      }

  if (isa<ConstantSDNode>(LHS))
    LHS = materializeImm(LHS, DL, DAG);
  if (isa<ConstantSDNode>(RHS))
    RHS = materializeImm(RHS, DL, DAG);
  unsigned Opc = IsSub ? MCS251::SUB32rr : MCS251::ADD32rr;
  return SDValue(DAG.getMachineNode(Opc, DL, MVT::i32, {LHS, RHS}), 0);
}

SDValue MCS251TargetLowering::LowerLogical32(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  // Keep the two words of an i32 constant in independent GPR16 vregs. At -O0,
  // materializing a constant as MOV32ri and then extracting both DR lanes
  // produces two subregister COPYs from the same DR vreg. FastRA incorrectly
  // coalesces those copies as the high lane, losing the low word. Constants
  // therefore bypass the DR representation here; non-constants retain the
  // normal subregister extraction path.
  auto SplitOperand = [&](SDValue V, SDValue &Hi, SDValue &Lo) {
    if (auto *C = dyn_cast<ConstantSDNode>(V)) {
      uint32_t Value = C->getZExtValue();
      Hi = buildMOV16ri(Value >> 16, DL, DAG);
      Lo = buildMOV16ri(Value, DL, DAG);
      return;
    }
    Hi = extractLane(V, MCS251::sub_hi16, DL, DAG);
    Lo = extractLane(V, MCS251::sub_lo16, DL, DAG);
  };

  unsigned Opc;
  switch (Op.getOpcode()) {
  case ISD::AND:
    Opc = MCS251::AND16rr;
    break;
  case ISD::OR:
    Opc = MCS251::OR16rr;
    break;
  case ISD::XOR:
    Opc = MCS251::XOR16rr;
    break;
  default:
    llvm_unreachable("not an i32 logical operation");
  }
  SDValue LHi, LLo, RHi, RLo;
  SplitOperand(LHS, LHi, LLo);
  SplitOperand(RHS, RHi, RLo);
  SDValue Hi(DAG.getMachineNode(Opc, DL, MVT::i16, {LHi, RHi}), 0);
  SDValue Lo(DAG.getMachineNode(Opc, DL, MVT::i16, {LLo, RLo}), 0);
  return makeDR(Hi, Lo, DL, DAG);
}

//===----------------------------------------------------------------------===//
// Dynamic alloca lowering (Phase 9)
//===----------------------------------------------------------------------===//
//
// DYNAMIC_STACKALLOC becomes the DYNALLOCA machine pseudo (value + chain);
// the custom inserter builds the full SFR-direct read/add/write sequence
// (see the DYNALLOCA comment in MCS251InstrInfo.td). The stack grows up
// and the returned pointer is the object base (old SPX + 1), so nothing
// else is needed here beyond alignment policing and size normalisation.

// STACKSAVE/STACKRESTORE use the same SFR-direct SPX representation as
// DYNALLOCA. DR60 cannot be copied to/from a pointer-sized WR directly: its
// low 16 bits are exposed only as SFR bytes 0x81 (SP) and 0x85 (SPH).
SDValue MCS251TargetLowering::LowerSTACKSAVE(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDVTList ByteWithChain = DAG.getVTList(MVT::i8, MVT::Other);

  SDValue Lo(DAG.getMachineNode(
                 MCS251::MOV8di, DL, ByteWithChain,
                 {DAG.getTargetConstant(0x81, DL, MVT::i8), Chain}),
             0);
  SDValue Hi(DAG.getMachineNode(
                 MCS251::MOV8di, DL, ByteWithChain,
                 {DAG.getTargetConstant(0x85, DL, MVT::i8), Lo.getValue(1)}),
             0);
  SmallVector<SDValue, 5> Ops = {
      DAG.getTargetConstant(MCS251::GPR16RegClassID, DL, MVT::i32), Hi,
      DAG.getTargetConstant(MCS251::sub_hi8, DL, MVT::i32), Lo,
      DAG.getTargetConstant(MCS251::sub_lo8, DL, MVT::i32)};
  SDValue SPX(DAG.getMachineNode(TargetOpcode::REG_SEQUENCE, DL, MVT::i16,
                                 Ops),
              0);
  return DAG.getMergeValues({SPX, Hi.getValue(1)}, DL);
}

SDValue MCS251TargetLowering::LowerSTACKRESTORE(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue SavedSPX = Op.getOperand(1);
  SDValue Hi = DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8,
                                           SavedSPX);
  SDValue Lo = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8,
                                           SavedSPX);
  // Keep the writes chained and write high first. The transient (newHi:oldLo)
  // lies at or above the final stack top, so an interrupt cannot overwrite
  // the object being restored away.
  SDValue HiWrite(DAG.getMachineNode(
                      MCS251::MOV8id, DL, MVT::Other,
                      {DAG.getTargetConstant(0x85, DL, MVT::i8), Hi, Chain}),
                  0);
  return SDValue(DAG.getMachineNode(
                     MCS251::MOV8id, DL, MVT::Other,
                     {DAG.getTargetConstant(0x81, DL, MVT::i8), Lo, HiWrite}),
                 0);
}

SDValue MCS251TargetLowering::LowerDynamicStackAlloc(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  SDLoc DL(Op);

  // The result is a pointer; only the i16 pointer type exists.
  if (Op.getValueType() != MVT::i16)
    report_fatal_error("MCS251: only 16-bit pointers are supported");
  if (Size.getValueType() == MVT::i8) {
    // Widen a byte-sized allocation amount to the 16-bit add.
    Size = SDValue(DAG.getMachineNode(MCS251::ZEXT8, DL, MVT::i16, Size), 0);
  }
  if (auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(2))) {
    if (C->getZExtValue() > 1)
      report_fatal_error("MCS251: dynamic alloca alignment > 1 is not "
                         "supported (the stack is byte-aligned, no padding "
                         "is ever emitted)");
  } else {
    llvm_unreachable("DYNAMIC_STACKALLOC alignment must be a constant");
  }
  // Defensive: a constant size should never reach here (the IR builder
  // turns constant-sized allocas into frame indices), but if it does,
  // materialise it so the register operand of DYNALLOCA is a real vreg.
  if (isa<ConstantSDNode>(Size))
    Size = buildMOV16ri(cast<ConstantSDNode>(Size)->getZExtValue(), DL, DAG);

  return SDValue(DAG.getMachineNode(
                     MCS251::DYNALLOCA, DL,
                     DAG.getVTList(MVT::i16, MVT::Other), {Size, Chain}),
                 0);
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
  if (Ins.size() > 1 ||
      (Ins[0].VT != MVT::i8 && Ins[0].VT != MVT::i16 &&
       Ins[0].VT != MVT::i32))
    report_fatal_error("minimal MCS251 backend only supports zero or one "
                       "i8/i16/i32 argument; SDCC multi-arg ABI uses static "
                       "OSEG overlay slots (not yet supported)");

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_MCS251);

  // The argument arrives in a reserved SFR (DPL/DPH/DPTR, and A/B for i32).
  // addLiveIn hands its value to the DAG as a virtual register of the matching
  // allocatable class; it emits no copy itself. At the end of instruction
  // selection, SelectionDAGISel calls MachineRegisterInfo::EmitLiveInCopies
  // to materialise the phys-to-virt COPY in the entry block. The coalescer
  // cannot merge that COPY because the SFR is reserved and outside GPR8/GPR16,
  // so it survives register allocation and ExpandPostRAPseudos lowers it via
  // copyPhysReg (TargetInstrInfo::lowerCopy). A/B follow exactly the same path
  // as DPL/DPH.
  if (Ins[0].VT == MVT::i32) {
    SmallVector<SDValue, 4> Parts;
    for (MCPhysReg Reg : I32ABIRegs) {
      Register VReg = MF.addLiveIn(Reg, &MCS251::GPR8RegClass);
      SDValue Part = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i8);
      Parts.push_back(Part);
      Chain = Part.getValue(1);
    }
    InVals.push_back(combineI32FromBytes(Parts, DL, DAG));
    return Chain;
  }

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    if (!VA.isRegLoc())
      report_fatal_error(
          "minimal MCS251 backend only supports zero or one i8/i16/i32 "
          "argument; SDCC multi-arg ABI uses static OSEG overlay slots (not "
          "yet supported)");
    const TargetRegisterClass *RC = VA.getLocVT() == MVT::i8
                                        ? &MCS251::GPR8RegClass
                                        : &MCS251::GPR16RegClass;
    Register VReg = MF.addLiveIn(VA.getLocReg(), RC);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
    InVals.push_back(ArgValue);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//  Call lowering (Phase 7)
//===----------------------------------------------------------------------===//
//
// Direct calls compile to `ecall _sym` (9A + addr24, 4 bytes; the linker
// fills the 24-bit address, so the symbol is never truncated). Argument
// loading reuses the Phase 4 ABI slots (i8 -> dpl, i16 -> dptr) via the same
// CC_MCS251 analysis as LowerFormalArguments, mirrored onto the caller side
// with CopyToReg. Results come back through the same fixed locations
// (LowerCallResult's CopyFromReg); a returned value feeding straight into
// the caller's own return needs no intermediate copy at all -- the
// phys->virt->phys chain through dpl/dptr coalesces away (dpl/dptr are
// reserved), the SDCC-equivalent `ecall; eret` tail.
//
// CALLSEQ decision: no CALLSEQ_START/END nodes are emitted. The SDCC MCS-251
// ABI passes no arguments on the stack (stack-auto=0; the single register
// slot is all this backend supports), so the sequence bounds would always be
// 0,0. Skipping them entirely (deviation from the MSP430/AVR template, which
// wraps the call in CALLSEQ nodes and then implements the ADJCALLSTACKDOWN/UP
// pseudos) avoids two never-anything-but-zero pseudo instructions and the
// corresponding eliminateCallFramePseudoInstr hook; nothing in the DAG
// builder requires a LowerCall to produce a call sequence.
//
// Register pressure across calls: since Phase 9 the spiller has real frame
// slots (storeRegToStackSlot/loadRegFromStackSlot -> @dr60 displacement
// accesses), so values live across a call are spilled to the frame instead
// of being a hard error. A mixed dynamic-alloca function references its
// static slots through the dr56 anchor, so those spills stay correct too.
SDValue MCS251TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
    break;
  }

  if (IsVarArg)
    report_fatal_error("minimal MCS251 backend does not support variadic "
                       "functions");

  if (IsTailCall)
    report_fatal_error("MCS251: tail calls are not supported");

  // Only direct calls: the ecall encoding takes a symbolic target address.
  // A register callee (function pointer) would need an indirect-call
  // encoding that this phase does not model.
  if (!isa<GlobalAddressSDNode>(Callee) && !isa<ExternalSymbolSDNode>(Callee))
    report_fatal_error("MCS251: indirect calls (function pointers) are not "
                       "supported");

  // Same single-slot restriction as LowerFormalArguments, checked up front
  // so the error names the real limitation (OSEG overlay) instead of a
  // generic CC allocation failure.
  if (Outs.size() > 1 ||
      (!Outs.empty() && Outs[0].VT != MVT::i8 && Outs[0].VT != MVT::i16 &&
       Outs[0].VT != MVT::i32))
    report_fatal_error("MCS251 multi-argument calls need SDCC OSEG overlay "
                       "slots (not yet supported)");

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_MCS251);

  // Copy the arguments into their ABI registers, chained and glued so nothing
  // can be scheduled between the copies and the call.
  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SDValue InGlue;
  if (!Outs.empty() && Outs[0].VT == MVT::i32) {
    SmallVector<SDValue, 4> Parts;
    splitI32ToBytes(OutVals[0], DL, DAG, Parts);
    for (unsigned I = 0; I < 4; ++I)
      RegsToPass.emplace_back(I32ABIRegs[I], Parts[I]);
  } else {
    for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
      CCValAssign &VA = ArgLocs[I];
      if (!VA.isRegLoc() || VA.getLocInfo() != CCValAssign::Full)
        report_fatal_error("MCS251 multi-argument calls need SDCC OSEG overlay "
                           "slots (not yet supported)");
      RegsToPass.emplace_back(VA.getLocReg(), OutVals[I]);
    }
  }
  for (const auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Wrap the callee so legalisation cannot hack the address apart: every
  // direct call reaches here as one of these two node kinds (checked above).
  // The pointer type follows the DataLayout (MVT::i16 today) so a future
  // pointer-width change cannot silently truncate the symbol, and a
  // GlobalAddress with an offset keeps it (MSP430 pattern).
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL,
                                        getPointerTy(DAG.getDataLayout()),
                                        G->getOffset());
  else if (auto *S = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(),
                                         getPointerTy(DAG.getDataLayout()));

  // Build the CALL node: [chain, callee, arg regs..., regmask, glue]. The
  // getRegister operands become implicit uses on the ECALL MachineInstr,
  // keeping the ABI registers live into the call; the register mask (from
  // getCallPreservedMask, everything caller-saved except spx) carries the
  // clobber set so RA knows nothing else survives.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  for (const auto &[Reg, Val] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Val.getValueType()));
  const uint32_t *RegMask =
      DAG.getSubtarget().getRegisterInfo()->getCallPreservedMask(
          DAG.getMachineFunction(), CallConv);
  Ops.push_back(DAG.getRegisterMask(RegMask));
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  Chain = DAG.getNode(MCS251ISD::CALL, DL,
                      DAG.getVTList(MVT::Other, MVT::Glue), Ops);
  InGlue = Chain.getValue(1);

  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, CLI.Ins, DL, DAG,
                         InVals);
}

SDValue MCS251TargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // Same single-value restriction as CanLowerReturn, checked for the call
  // site (a call to a declared-but-absurd callee type would otherwise die
  // inside the generic CC machinery).
  if (Ins.size() > 1 ||
      (!Ins.empty() && Ins[0].VT != MVT::i8 && Ins[0].VT != MVT::i16 &&
       Ins[0].VT != MVT::i32))
    report_fatal_error(
        "minimal MCS251 backend only supports i8/i16/i32/void return values");

  if (!Ins.empty() && Ins[0].VT == MVT::i32) {
    SmallVector<SDValue, 4> Parts;
    for (MCPhysReg Reg : I32ABIRegs) {
      SDValue Val = DAG.getCopyFromReg(Chain, DL, Reg, MVT::i8, InGlue);
      Parts.push_back(Val.getValue(0));
      Chain = Val.getValue(1);
      InGlue = Val.getValue(2);
    }
    InVals.push_back(combineI32FromBytes(Parts, DL, DAG));
    return Chain;
  }

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_MCS251);

  // Copy the results out of dpl / dptr into vregs, glued to the call. The
  // glue keeps the reads adjacent to the ECALL.
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    assert(RVLocs[I].isRegLoc() && "MCS251 call results must be in "
                                   "registers");
    SDValue Val = DAG.getCopyFromReg(Chain, DL, RVLocs[I].getLocReg(),
                                     RVLocs[I].getValVT(), InGlue);
    InVals.push_back(Val.getValue(0));
    Chain = Val.getValue(1);
    InGlue = Val.getValue(2);
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
  if (Outs.size() > 1)
    report_fatal_error("minimal MCS251 backend only supports zero or one "
                       "i8/i16/i32 return value; multi-value returns are not "
                       "supported");
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  if (!CCInfo.CheckReturn(Outs, RetCC_MCS251))
    report_fatal_error(
        "minimal MCS251 backend only supports i8/i16/i32/void return values");
  return true;
}

SDValue MCS251TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  // Assign the return values to the ABI locations: dpl for i8, dpl:dph for
  // i16, and dpl/dph/b/a for i32. The i32 value is split explicitly because
  // the CC assignment records are one logical value while the ABI has four
  // byte registers.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_MCS251);

  SDValue Glue;
  SmallVector<SDValue, 8> RetOps(1, Chain);
  if (!Outs.empty() && Outs[0].VT == MVT::i32) {
    assert(Outs.size() == 1 && "MCS251 supports only one return value");
    SmallVector<SDValue, 4> Parts;
    splitI32ToBytes(OutVals[0], DL, DAG, Parts);
    for (unsigned I = 0; I < 4; ++I) {
      Chain = DAG.getCopyToReg(Chain, DL, I32ABIRegs[I], Parts[I], Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(I32ABIRegs[I], MVT::i8));
    }
  } else {
    for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
      CCValAssign &VA = RVLocs[I];
      assert(VA.isRegLoc() && "MCS251 return values must go to registers");
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
    }
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(MCS251ISD::ERET, DL, MVT::Other, RetOps);
}
