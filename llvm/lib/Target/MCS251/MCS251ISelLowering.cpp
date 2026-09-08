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
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

// Generated from MCS251CallingConv.td; provides RetCC_MCS251().
#define GET_CALLING_CONV_IMPL
#include "MCS251GenCallingConv.inc"

MCS251TargetLowering::MCS251TargetLowering(const TargetMachine &TM,
                                           const MCS251Subtarget &STI)
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i8, &MCS251::GPR8RegClass);
  addRegisterClass(MVT::i16, &MCS251::GPR16RegClass);
  // i32 is legal for native DR arithmetic and canonical pointer values.
  addRegisterClass(MVT::i32, &MCS251::GPR32RegClass);
  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(MCS251::DR60);
  setBooleanContents(ZeroOrOneBooleanContent);

  // Comparisons feed long conditional branches or register-valued selects.
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
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
  }

  setOperationAction(ISD::MUL, MVT::i32, Custom);

  // Division/remainder (division design v4 §5.4 / SPEC
  // 2026-09-07 §1.1): i8 has no runtime symbols at all,
  // so all four ops promote to i16. The operation
  // legalizer's PromoteNode (LegalizeDAG.cpp :5613-5661)
  // sign-extends SDIV/SREM and zero-extends UDIV/UREM
  // operands, performs the i16 op, truncates the result --
  // promoted i8 arithmetic is exact by mechanism.
  setOperationAction(ISD::UDIV, MVT::i8, Promote);
  setOperationPromotedToType(ISD::UDIV, MVT::i8, MVT::i16);
  setOperationAction(ISD::SDIV, MVT::i8, Promote);
  setOperationPromotedToType(ISD::SDIV, MVT::i8, MVT::i16);
  setOperationAction(ISD::UREM, MVT::i8, Promote);
  setOperationPromotedToType(ISD::UREM, MVT::i8, MVT::i16);
  setOperationAction(ISD::SREM, MVT::i8, Promote);
  setOperationPromotedToType(ISD::SREM, MVT::i8, MVT::i16);
  setOperationAction(ISD::UDIV, MVT::i16, LibCall);
  setOperationAction(ISD::UDIV, MVT::i32, LibCall);
  setOperationAction(ISD::SDIV, MVT::i16, LibCall);
  setOperationAction(ISD::SDIV, MVT::i32, LibCall);
  setOperationAction(ISD::SREM, MVT::i16, LibCall);
  setOperationAction(ISD::SREM, MVT::i32, LibCall);
  setOperationAction(ISD::UREM, MVT::i16, LibCall);
  setOperationAction(ISD::UREM, MVT::i32, LibCall);
  setLibcallImpl(RTLIB::UDIV_I16, RTLIB::impl_mcs251_divuint);
  setLibcallImpl(RTLIB::UDIV_I32, RTLIB::impl_mcs251_divulong);
  setLibcallImpl(RTLIB::SDIV_I16, RTLIB::impl_mcs251_divsint);
  setLibcallImpl(RTLIB::SDIV_I32, RTLIB::impl_mcs251_divslong);
  setLibcallImpl(RTLIB::SREM_I16, RTLIB::impl_mcs251_modsint);
  setLibcallImpl(RTLIB::SREM_I32, RTLIB::impl_mcs251_modslong);
  setLibcallImpl(RTLIB::UREM_I16, RTLIB::impl_mcs251_moduint);
  setLibcallImpl(RTLIB::UREM_I32, RTLIB::impl_mcs251_modulong);
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::MULHU, VT, Expand);
    setOperationAction(ISD::MULHS, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
  }

  // InstCombine recognizes byte permutations as BSWAP. There is no native
  // instruction; use generic shifts/masks, all of which are already lowered.
  setOperationAction(ISD::BSWAP, MVT::i16, Expand);
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);

  // Address classification distinguishes SFR-direct access from canonical DR
  // pointers. Multi-byte objects retain the measured big-endian lane layout.
  setOperationAction(ISD::LOAD, MVT::i8, Custom);
  setOperationAction(ISD::LOAD, MVT::i16, Custom);
  setOperationAction(ISD::STORE, MVT::i8, Custom);
  setOperationAction(ISD::STORE, MVT::i16, Custom);
  // i32 memory objects (including pointer slots) are four ordered byte ops.
  setOperationAction(ISD::LOAD, MVT::i32, Custom);
  setOperationAction(ISD::STORE, MVT::i32, Custom);
  // Extending loads and truncating stores consult their OWN action tables
  // (default Legal, they do not inherit the LOAD/STORE action above), so
  // route them to the same custom lowering explicitly.
  for (unsigned Ext :
       {ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}) {
    setLoadExtAction(Ext, MVT::i16, MVT::i8, Custom);
    setLoadExtAction(Ext, MVT::i32, MVT::i8, Custom);
    setLoadExtAction(Ext, MVT::i32, MVT::i16, Custom);
  }
  setTruncStoreAction(MVT::i16, MVT::i8, Custom);
  setTruncStoreAction(MVT::i32, MVT::i8, Custom);
  setTruncStoreAction(MVT::i32, MVT::i16, Custom);
  // Truncation extracts low lanes; extensions assemble WR/DR lanes.
  setOperationAction(ISD::TRUNCATE, MVT::i8, Custom);
  setOperationAction(ISD::TRUNCATE, MVT::i16, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i16, Custom);
  setOperationAction(ISD::ANY_EXTEND, MVT::i16, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i16, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::ANY_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i32, Custom);
  // SIGN_EXTEND_INREG (DAGCombiner's sext(trunc x) form) consults the
  // action table by INNER type, not by result type (LegalizeDAG), so the
  // two modeled inner widths need explicit entries. All other inner types
  // (i1, wider scalars, vectors) are deliberately NOT registered this
  // round: they keep whatever action the generic defaults give them (Legal
  // for most scalars, Expand for vector INREG and the odd narrow types,
  // TargetLoweringBase::initActions) -- no support is claimed for them,
  // and a Legal-but-unselectable node fails loudly at selection.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Custom);
  // Stack allocations (Phase 9): static allocas surface as FrameIndexSDNode
  // pointers handled by parseAddress (direct @dr60 access) and the FIADDR
  // Select hook (escaping pointer values); variable-length allocas lower
  // through DYNAMIC_STACKALLOC into the DYNALLOCA pseudo.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i8, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i16, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i32, Custom);
  // Scaled GEPs use constant shifts, lowered with native DR additions.
  // Phase 14: full constant-count shift support.  i8/i16 unroll the native
  // 1-bit sll/srl/sra; i32 SHL keeps the ADD32rr doubling while i32 SRL/SRA
  // go through the SRL32ri/SRA32ri custom-inserter pseudos (no native dword
  // shift exists). Variable counts use guarded unit-shift loops.
  setOperationAction(ISD::SHL, MVT::i32, Custom);
  setOperationAction(ISD::SHL, MVT::i8, Custom);
  setOperationAction(ISD::SHL, MVT::i16, Custom);
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::SRL, VT, Custom);
    setOperationAction(ISD::SRA, VT, Custom);
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
    setOperationAction(ISD::FSHL, VT, Expand);
    setOperationAction(ISD::FSHR, VT, Expand);
  }
  // Preserve the 16-bit SPX via SFR reads/writes while its saved value is an
  // i32 pointer. The operation legalizer consults MVT::Other for both nodes.
  setOperationAction(ISD::STACKSAVE, MVT::Other, Custom);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Custom);
  // Atomics are separate opcodes (not flags on ISD::LOAD/STORE); route them
  // to a loud rejection -- "Cannot select" gives no hint what is missing.
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::ATOMIC_LOAD, VT, Custom);
    setOperationAction(ISD::ATOMIC_STORE, VT, Custom);
  }

  // Float (f32/f64) and wide-integer (i64) arithmetic is not implemented.
  // Division/remainder already routes to a libcall that fails loudly; the
  // remaining add/sub/mul/cmp/conversion ops would otherwise be silently
  // mis-lowered (i64 add collapses to a 32-bit add; fadd hits a generic
  // "no libcall" error only at selection time, not a clear target diagnostic).
  // Route them to a custom lowering that report_fatal_errors with a clear
  // message.  When mcs251-runtime soft-float is wired up, replace these with
  // LibCall actions (cf. the SDIV/SREM/UDIV/UREM block above).
  // TODO(mcs251-runtime): switch f32/f64 ops to LibCall once the soft-float
  // runtime is available; switch i64 ops to LibCall or a native widening path.
  for (MVT VT : {MVT::f32, MVT::f64, MVT::i64}) {
    setOperationAction(ISD::ADD, VT, Custom);
    setOperationAction(ISD::SUB, VT, Custom);
    setOperationAction(ISD::MUL, VT, Custom);
    setOperationAction(ISD::SDIV, VT, Custom);
    setOperationAction(ISD::UDIV, VT, Custom);
    setOperationAction(ISD::SREM, VT, Custom);
    setOperationAction(ISD::UREM, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
  }
  // Float-only conversions and operations.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::FADD, VT, Custom);
    setOperationAction(ISD::FSUB, VT, Custom);
    setOperationAction(ISD::FMUL, VT, Custom);
    setOperationAction(ISD::FDIV, VT, Custom);
    setOperationAction(ISD::FREM, VT, Custom);
    setOperationAction(ISD::FCOPYSIGN, VT, Custom);
    setOperationAction(ISD::FNEG, VT, Custom);
    setOperationAction(ISD::FABS, VT, Custom);
    setOperationAction(ISD::FCEIL, VT, Custom);
    setOperationAction(ISD::FFLOOR, VT, Custom);
    setOperationAction(ISD::FTRUNC, VT, Custom);
    setOperationAction(ISD::FRINT, VT, Custom);
    setOperationAction(ISD::FNEARBYINT, VT, Custom);
    setOperationAction(ISD::FSQRT, VT, Custom);
    setOperationAction(ISD::FSIN, VT, Custom);
    setOperationAction(ISD::FCOS, VT, Custom);
    setOperationAction(ISD::FEXP, VT, Custom);
    setOperationAction(ISD::FEXP2, VT, Custom);
    setOperationAction(ISD::FLOG, VT, Custom);
    setOperationAction(ISD::FLOG2, VT, Custom);
    setOperationAction(ISD::FLOG10, VT, Custom);
    setOperationAction(ISD::FPOW, VT, Custom);
    setOperationAction(ISD::FMA, VT, Custom);
    setOperationAction(ISD::FP_EXTEND, VT, Custom);
    setOperationAction(ISD::FP_ROUND, VT, Custom);
  }
  // Integer-float conversions: cover all direction/type combinations that
  // could surface. These are keyed by result type.
  setOperationAction(ISD::SINT_TO_FP, MVT::f32, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::f64, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::f32, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::f64, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i8, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i16, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i8, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i16, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
  // i64 shifts and wide ops that currently mis-compile silently.
  setOperationAction(ISD::SHL, MVT::i64, Custom);
  setOperationAction(ISD::SRL, MVT::i64, Custom);
  setOperationAction(ISD::SRA, MVT::i64, Custom);
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

// Constant shifts retain the Phase 14 unrolled lowering. Variable shifts
// become pre-RA guarded loops with ordinary PHIs and a GPR8Low counter.
//
// i8/i16 unroll the native 1-bit sll/srl/sra: one MachineNode per shifted
// bit.  Machine nodes are opaque to the DAG combiner, so no combinatorial
// re-derivation (or/select chains -> shift) can resurrect an unselectable
// form: the shift nodes here ARE the legal form.
//
// i32: SHL keeps the ADD32rr doubling (no native dword shift exists at all);
// SRL/SRA go through the custom-inserter pseudos (CY-chained RRC A byte
// lanes, see EmitInstrWithCustomInserter).
//
// LLVM IR shifts by count >= width produce poison. SelectionDAG normally
// folds them to UNDEF before this hook; LowerReturn chooses zero for an
// undefined scalar result. The bounds handling below is only defensive,
// not a saturation/sign-fill contract for out-of-range IR shifts.
SDValue MCS251TargetLowering::LowerShift(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  const unsigned Width = VT.getSizeInBits();
  const bool Left = Op.getOpcode() == ISD::SHL;
  const bool Arith = Op.getOpcode() == ISD::SRA;
  auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C) {
    // Defined shift counts fit in five bits. Truncating to a byte does not
    // constrain the unspecified result for out-of-range (poison) counts.
    SDValue Count = DAG.getZExtOrTrunc(Op.getOperand(1), DL, MVT::i8);
    unsigned Opc = Width == 8 ? MCS251::VSHIFT8
                   : Width == 16 ? MCS251::VSHIFT16 : MCS251::VSHIFT32;
    return SDValue(DAG.getMachineNode(
        Opc, DL, VT, {Op.getOperand(0), Count,
                      DAG.getTargetConstant(Left ? 0 : Arith ? 2 : 1,
                                            DL, MVT::i8)}), 0);
  }
  uint64_t Cnt = C->getZExtValue();
  if (Cnt == 0)
    return Op.getOperand(0);
  if (Cnt >= Width) {
    if (!Arith)
      return DAG.getConstant(0, DL, VT);
    Cnt = Width - 1;  // ashr: full sign fill
  }
  SDValue V = Op.getOperand(0);
  if (VT == MVT::i32) {
    if (Left) {
      for (unsigned I = 0; I < Cnt; ++I)
        V = SDValue(DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32, {V, V}),
                    0);
      return V;
    }
    unsigned Opc = Arith ? MCS251::SRA32ri : MCS251::SRL32ri;
    return SDValue(
        DAG.getMachineNode(Opc, DL, MVT::i32,
                           {V, DAG.getTargetConstant(Cnt, DL, MVT::i32)}),
        0);
  }
  unsigned Opc = Left  ? (Width == 8 ? MCS251::SLL8 : MCS251::SLL16)
                 : Arith ? (Width == 8 ? MCS251::SRA8 : MCS251::SRA16)
                         : (Width == 8 ? MCS251::SRL8 : MCS251::SRL16);
  for (unsigned I = 0; I < Cnt; ++I)
    V = SDValue(DAG.getMachineNode(Opc, DL, VT, {V}), 0);
  return V;
}

SDValue MCS251TargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  // DF0 task 2: f32/f64/i64 arithmetic, comparison and conversion ops are
  // not yet implemented. Route them to a loud report_fatal_error instead of
  // letting the legalizer silently produce wrong code (i64 add collapses to
  // 32-bit) or hit a generic late "no libcall" error.
  // TODO(mcs251-runtime): once the soft-float runtime is wired up, replace
  // these with LibCall actions (cf. SDIV/SREM/UDIV/UREM above).
  EVT VT = Op.getValueType();
  if (VT == MVT::f32 || VT == MVT::f64 || VT == MVT::i64) {
    // RC-7 (P2): name the runtime that is actually missing -- soft-float for
    // f32/f64, the wide-integer runtime for i64 -- instead of lumping both.
    const char *VTName = VT == MVT::f32 ? "f32" : VT == MVT::f64 ? "f64" : "i64";
    const char *Runtime =
        VT == MVT::i64 ? "wide-integer runtime" : "soft-float runtime";
    report_fatal_error(Twine("MCS251: ") + VTName +
                       " operations are not yet implemented; " + Runtime +
                       " is not connected");
  }
  // FP-to-int and int-to-FP conversions have integer or float result types
  // that may not be caught by the VT check above.
  switch (Op.getOpcode()) {
  default:
    break;
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    report_fatal_error("MCS251: integer-to-float conversion is not yet "
                       "implemented; soft-float runtime is not connected");
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    report_fatal_error("MCS251: float-to-integer conversion is not yet "
                       "implemented; soft-float runtime is not connected");
  case ISD::FP_EXTEND:
    report_fatal_error("MCS251: float widening (fp_extend) is not yet "
                       "implemented; soft-float runtime is not connected");
  case ISD::FP_ROUND:
    report_fatal_error("MCS251: float narrowing (fp_round) is not yet "
                       "implemented; soft-float runtime is not connected");
  }
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("custom operation has no registered lowering");
  case ISD::MUL:
    return LowerMul32(Op, DAG);
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
  case ISD::TRUNCATE: {
    SDLoc DL(Op);
    SDValue V = Op.getOperand(0);
    if (V.getValueType() == MVT::i32)
      V = DAG.getTargetExtractSubreg(MCS251::sub_lo16, DL, MVT::i16, V);
    if (Op.getValueType() == MVT::i8)
      V = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, V);
    return V;
  }
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    return LowerExtend(Op, DAG);
  case ISD::SIGN_EXTEND:
    return LowerExtend(Op, DAG);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSignExtendInReg(Op, DAG);
  case ISD::GlobalAddress: {
    SDLoc DL(Op);
    auto *GA = cast<GlobalAddressSDNode>(Op);
    EVT PtrVT = Op.getValueType();
    unsigned Opc = PtrVT == MVT::i16 ? MCS251::MOV16ri : MCS251::MOVADDR32;
    return SDValue(DAG.getMachineNode(
                       Opc, DL, PtrVT,
                       DAG.getTargetGlobalAddress(GA->getGlobal(), DL, PtrVT,
                                                  GA->getOffset())),
                   0);
  }
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return LowerShift(Op, DAG);
  case ISD::BRCOND: {
    SDLoc DL(Op);
    SDValue Cond = Op.getOperand(1);
    return LowerBR_CC(DAG.getNode(
        ISD::BR_CC, DL, MVT::Other, Op.getOperand(0),
        DAG.getCondCode(ISD::SETNE), Cond,
        DAG.getConstant(0, DL, Cond.getValueType()), Op.getOperand(2)), DAG);
  }
  case ISD::SETCC: {
    SDLoc DL(Op);
    EVT VT = Op.getValueType();
    return LowerSELECT_CC(DAG.getNode(
        ISD::SELECT_CC, DL, VT, Op.getOperand(0), Op.getOperand(1),
        DAG.getConstant(1, DL, VT), DAG.getConstant(0, DL, VT),
        Op.getOperand(2)), DAG);
  }
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDynamicStackAlloc(Op, DAG);
  case ISD::STACKSAVE:
    return LowerSTACKSAVE(Op, DAG);
  case ISD::STACKRESTORE:
    return LowerSTACKRESTORE(Op, DAG);
  case ISD::ADDRSPACECAST:
    return LowerAddrSpaceCast(Op, DAG);
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE:
    report_fatal_error("MCS251: atomic memory operations are not supported");
  }
}

// RC-4: When the type legalizer encounters an i64/f32/f64 result type (which
// is illegal on MCS251 -- no register class) and the operation was registered
// as Custom, it calls ReplaceNodeResults instead of LowerOperation. The
// default implementation hits llvm_unreachable (SIGABRT), and vector i64 ops
// that bypass type legalization entirely hit a null TLI pointer (SIGSEGV).
// Route both cases to the same loud report_fatal_error as LowerOperation.
//
// RC-4 fix: Leaving Results empty only falls back to the generic LegalizeTypes
// path, which does NOT request DAGCombiner folding. Instead, actively attempt
// to constant-fold the node here. If all operands are constants and folding
// succeeds, replace the result with the folded constant. If folding fails or
// operands are not all constants, reject loudly with report_fatal_error.
void MCS251TargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  EVT VT = N->getValueType(0);
  if (VT == MVT::f32 || VT == MVT::f64 || VT == MVT::i64) {
    // Attempt to constant-fold this node. FoldConstantArithmetic handles
    // binary integer ops (add, sub, mul, udiv, etc.), unary and binary FP
    // ops (fabs, fsqrt, fadd, etc.), and conversions (fp_round, fp_extend,
    // fp_to_int, etc.). It returns SDValue() if the operands are not all
    // constants or the opcode is not foldable.
    SmallVector<SDValue, 4> Ops;
    for (unsigned I = 0, E = N->getNumOperands(); I < E; ++I)
      Ops.push_back(N->getOperand(I));
    SDLoc DL(N);
    SDValue Folded =
        DAG.FoldConstantArithmetic(N->getOpcode(), DL, VT, Ops, N->getFlags());
    if (Folded) {
      Results.push_back(Folded);
      return;
    }
    // Folding failed -- either operands are not all constants or the opcode
    // is not foldable. Reject loudly. RC-7 (P2): name the runtime that is
    // actually missing -- soft-float for f32/f64, wide-integer for i64.
    const char *VTName = VT == MVT::f32 ? "f32" : VT == MVT::f64 ? "f64" : "i64";
    const char *Runtime =
        VT == MVT::i64 ? "wide-integer runtime" : "soft-float runtime";
    report_fatal_error(Twine("MCS251: ") + VTName +
                       " operations are not yet implemented; " + Runtime +
                       " is not connected");
  }
  // Any other node reaching here is a bug in the operation table.
  report_fatal_error("MCS251: unhandled custom type legalization for opcode " +
                     Twine(N->getOpcode()));
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

SDValue MCS251TargetLowering::LowerAddrSpaceCast(SDValue Op,
                                                 SelectionDAG &DAG) const {
  auto *Cast = cast<AddrSpaceCastSDNode>(Op.getNode());
  unsigned SrcAS = Cast->getSrcAddressSpace();
  unsigned DstAS = Cast->getDestAddressSpace();
  auto IsNearRAM = [](unsigned AS) {
    return AS == 0 || AS == 1 || AS == 2 || AS == 8;
  };
  auto IsFarRAM = [](unsigned AS) { return AS == 0 || AS == 3 || AS == 9; };

  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();
  EVT DstVT = Op.getValueType();
  SDLoc DL(Op);
  // RC-2: AS1 is strict direct RAM with range [0,0x80). A cast from a wider
  // address space (e.g. AS0, AS8) to AS1 cannot be proven safe at compile time
  // for a dynamic (non-constant) source -- the value may fall outside the
  // destination range. Reject loudly instead of silently passing through.
  auto DestinationHasRangeLimit = [](unsigned AS) { return AS == 1; };
  auto CheckKnownDestinationRange = [&](uint64_t Address) {
    // Representation width and address-space membership are different checks.
    // AS1 is strict direct RAM, not an arbitrary region-00 i16 address.
    if (DstAS == 1 && Address >= 0x80)
      report_fatal_error(
          "MCS251: constant address is outside destination address space 1 "
          "range [0,0x80)");
  };
  if (SrcVT == DstVT &&
      ((SrcVT == MVT::i16 && IsNearRAM(SrcAS) && IsNearRAM(DstAS)) ||
       (SrcVT == MVT::i32 && IsFarRAM(SrcAS) && IsFarRAM(DstAS)))) {
    // Same-address-space cast is a no-op passthrough.
    if (SrcAS == DstAS)
      return Src;
    // Cross-address-space cast: if the destination has a compile-time range
    // limit, a dynamic source cannot be proven safe.
    if (DestinationHasRangeLimit(DstAS)) {
      if (auto *C = dyn_cast<ConstantSDNode>(Src))
        CheckKnownDestinationRange(C->getZExtValue());
      else
        report_fatal_error(
            "MCS251: dynamic address-space cast to a range-limited "
            "destination requires a compile-time-provable source");
    }
    return Src;
  }
  if (SrcVT == MVT::i16 && DstVT == MVT::i32 && IsNearRAM(SrcAS) &&
      IsFarRAM(DstAS))
    return makeDR(materializeImm(DAG.getConstant(0, DL, MVT::i16), DL, DAG),
                  Src, DL, DAG);
  if (SrcVT == MVT::i32 && DstVT == MVT::i16 && IsFarRAM(SrcAS) &&
      IsNearRAM(DstAS)) {
    if (auto *C = dyn_cast<ConstantSDNode>(Src)) {
      uint64_t Address = C->getZExtValue();
      if (Address > 0xffff)
        report_fatal_error("MCS251: far-to-near constant address does not fit "
                           "16 bits");
      CheckKnownDestinationRange(Address);
      return materializeImm(DAG.getConstant(Address, DL, MVT::i16), DL, DAG);
    }
    report_fatal_error("MCS251: dynamic far-to-near address-space cast requires "
                       "an explicit checked conversion");
  }
  report_fatal_error("MCS251: unsupported address-space cast");
}

// Low 32 bits of (AH:AL)*(BH:BL): AL*BL + ((AH*BL + AL*BH) << 16).
// Signed and unsigned MUL have identical low-bit semantics. Keep constants
// in separate WR values, as in LowerLogical32, for FastRA lane correctness.
SDValue MCS251TargetLowering::LowerMul32(SDValue Op,
                                        SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::i32 && "only i32 MUL needs custom lowering");
  SDLoc DL(Op);
  auto Split = [&](SDValue V, unsigned Sub) {
    if (auto *C = dyn_cast<ConstantSDNode>(V)) {
      uint32_t Value = C->getZExtValue();
      if (Sub == MCS251::sub_hi16)
        Value >>= 16;
      return materializeImm(DAG.getConstant(Value & 0xffff, DL, MVT::i16),
                            DL, DAG);
    }
    return extractLane(V, Sub, DL, DAG);
  };
  SDValue AL = Split(Op.getOperand(0), MCS251::sub_lo16);
  SDValue AH = Split(Op.getOperand(0), MCS251::sub_hi16);
  SDValue BL = Split(Op.getOperand(1), MCS251::sub_lo16);
  SDValue BH = Split(Op.getOperand(1), MCS251::sub_hi16);
  SDValue Product(DAG.getMachineNode(MCS251::UMUL16WIDE, DL, MVT::i32,
                                     {AL, BL}), 0);
  SDValue Cross0 = DAG.getNode(ISD::MUL, DL, MVT::i16, AH, BL);
  SDValue Cross1 = DAG.getNode(ISD::MUL, DL, MVT::i16, AL, BH);
  SDValue Hi = DAG.getNode(ISD::ADD, DL, MVT::i16,
                          extractLane(Product, MCS251::sub_hi16, DL, DAG),
                          DAG.getNode(ISD::ADD, DL, MVT::i16, Cross0, Cross1));
  return makeDR(Hi, extractLane(Product, MCS251::sub_lo16, DL, DAG), DL, DAG);
}

// i32 ABI byte-register order is least-significant byte first: DPL, DPH, B,
// A. CC_MCS251/RetCC_MCS251 use DPL only as a single-value gatekeeper; all four
// lanes are explicitly transferred by splitI32ToBytes/combineI32FromBytes and
// the i32 formal/call/return lowering below.
static const MCPhysReg I32ABIRegs[] = {MCS251::DPL, MCS251::DPH, MCS251::B,
                                       MCS251::A};

static void splitI32ToBytes(SDValue Value, const SDLoc &DL, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &Parts) {
  SDValue Lo, Hi;
  if (auto *C = dyn_cast<ConstantSDNode>(Value)) {
    // Do not extract both WR lanes from one materialized DR constant: FastRA
    // can coalesce the two subregister COPYs into the same lane at -O0.
    uint32_t Imm = C->getZExtValue();
    Lo = materializeImm(DAG.getConstant(Imm & 0xffff, DL, MVT::i16), DL, DAG);
    Hi = materializeImm(DAG.getConstant(Imm >> 16, DL, MVT::i16), DL, DAG);
  } else {
    Lo = extractLane(Value, MCS251::sub_lo16, DL, DAG);
    Hi = extractLane(Value, MCS251::sub_hi16, DL, DAG);
  }
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

static SDValue canonicalizePointer32(SDValue Value, const SDLoc &DL,
                                     SelectionDAG &DAG) {
  assert(Value.getValueType() == MVT::i32 &&
         "only four-byte pointers need canonicalization");
  SmallVector<SDValue, 4> Parts;
  splitI32ToBytes(Value, DL, DAG, Parts);
  Parts[3] = materializeImm(DAG.getConstant(0, DL, MVT::i8), DL, DAG);
  return combineI32FromBytes(Parts, DL, DAG);
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

  if (LHS.getValueType() == MVT::i32) {
    if (isa<ConstantSDNode>(LHS))
      LHS = materializeImm(LHS, DL, DAG);
    if (isa<ConstantSDNode>(RHS))
      RHS = materializeImm(RHS, DL, DAG);
    SDValue Glue(DAG.getMachineNode(MCS251::CMP32rr, DL, MVT::Glue,
                                    {LHS, RHS}), 0);
    return SDValue(DAG.getMachineNode(MCS251::BRCC, DL, MVT::Other,
                                      {CCVal, Dest, Chain, Glue}), 0);
  }

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

// SELECT_CC and materialised SETCC share a flags producer and a small CFG
// diamond. The result PHI is a real register-class value (including pointers).
SDValue MCS251TargetLowering::LowerSELECT_CC(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
  SDValue True = Op.getOperand(2), False = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  if (isa<ConstantSDNode>(LHS))
    LHS = materializeImm(LHS, DL, DAG);
  if (isa<ConstantSDNode>(RHS))
    RHS = materializeImm(RHS, DL, DAG);
  if (isa<ConstantSDNode>(True))
    True = materializeImm(True, DL, DAG);
  if (isa<ConstantSDNode>(False))
    False = materializeImm(False, DL, DAG);
  EVT CmpVT = LHS.getValueType();
  if (CmpVT == MVT::i8 && isSignedIntegerCond(CC)) {
    auto Flip = [&](SDValue V) {
      SDValue X(DAG.getMachineNode(MCS251::XOR8ri, DL, MVT::i8,
          {V, DAG.getTargetConstant(0x80, DL, MVT::i8)}), 0);
      return SDValue(DAG.getMachineNode(MCS251::ZEXT8, DL, MVT::i16, X), 0);
    };
    LHS = Flip(LHS);
    RHS = Flip(RHS);
    switch (CC) {
    case ISD::SETLT: CC = ISD::SETULT; break;
    case ISD::SETLE: CC = ISD::SETULE; break;
    case ISD::SETGT: CC = ISD::SETUGT; break;
    case ISD::SETGE: CC = ISD::SETUGE; break;
    default: llvm_unreachable("expected signed condition");
    }
    CmpVT = MVT::i16;
  }
  unsigned CmpOpc = CmpVT == MVT::i32 ? MCS251::CMP32rr
                      : CmpVT == MVT::i16 ? MCS251::CMP16rr : MCS251::CMP8rr;
  SDValue Glue(DAG.getMachineNode(CmpOpc, DL, MVT::Glue, {LHS, RHS}), 0);
  EVT VT = Op.getValueType();
  unsigned Opc = VT == MVT::i32 ? MCS251::SELECT32
                  : VT == MVT::i16 ? MCS251::SELECT16 : MCS251::SELECT8;
  return SDValue(DAG.getMachineNode(Opc, DL, VT,
      {True, False, DAG.getTargetConstant(CC, DL, MVT::i8), Glue}), 0);
}

MachineBasicBlock *
MCS251TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unknown custom inserter opcode");
  case MCS251::VSHIFT8:
  case MCS251::VSHIFT16:
  case MCS251::VSHIFT32:
    return emitVariableShift(MI, BB);
  case MCS251::MUL8:
  case MCS251::MUL16:
  case MCS251::UMUL16WIDE: {
    const TargetInstrInfo *TII = BB->getParent()->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    bool Byte = MI.getOpcode() == MCS251::MUL8;
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY),
            Byte ? MCS251::A : MCS251::WR12)
        .addReg(MI.getOperand(1).getReg());
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY),
            Byte ? MCS251::B : MCS251::WR8)
        .addReg(MI.getOperand(2).getReg());
    BuildMI(*BB, MI, DL, TII->get(Byte ? MCS251::MULAB : MCS251::MULW));
    Register Result = Byte ? MCS251::A
                      : MI.getOpcode() == MCS251::MUL16 ? MCS251::WR14
                                                       : MCS251::DR12;
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY),
            MI.getOperand(0).getReg()).addReg(Result);
    MI.eraseFromParent();
    return BB;
  }
  case MCS251::SELECT8:
  case MCS251::SELECT16:
  case MCS251::SELECT32: {
    MachineFunction *MF = BB->getParent();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    MachineBasicBlock *FalseBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *MergeBB = MF->CreateMachineBasicBlock();
    auto Next = std::next(BB->getIterator());
    MF->insert(Next, FalseBB);
    MF->insert(Next, TrueBB);
    MF->insert(Next, MergeBB);
    MergeBB->splice(MergeBB->end(), BB, std::next(MI.getIterator()), BB->end());
    MergeBB->transferSuccessorsAndUpdatePHIs(BB);
    unsigned Skip = getSkipBranchOpcode(
        (ISD::CondCode)MI.getOperand(3).getImm());
    BuildMI(BB, DL, TII->get(Skip)).addMBB(FalseBB);
    BuildMI(BB, DL, TII->get(MCS251::EJMP)).addMBB(TrueBB);
    BB->addSuccessor(FalseBB);
    BB->addSuccessor(TrueBB);
    BuildMI(FalseBB, DL, TII->get(MCS251::EJMP)).addMBB(MergeBB);
    FalseBB->addSuccessor(MergeBB);
    BuildMI(TrueBB, DL, TII->get(MCS251::EJMP)).addMBB(MergeBB);
    TrueBB->addSuccessor(MergeBB);
    BuildMI(*MergeBB, MergeBB->begin(), DL, TII->get(TargetOpcode::PHI),
            MI.getOperand(0).getReg())
        .addReg(MI.getOperand(1).getReg()).addMBB(TrueBB)
        .addReg(MI.getOperand(2).getReg()).addMBB(FalseBB);
    MI.eraseFromParent();
    return MergeBB;
  }
  case MCS251::MOV32ri: {
    MachineFunction *MF = BB->getParent();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Dst = MI.getOperand(0).getReg();
    uint32_t Value = (uint32_t)MI.getOperand(1).getImm();
    // MOV must precede MOVH: MOV clears the high word, whereas MOVH writes
    // only the high word, so the two instructions are not interchangeable.
    Register Low = Value >> 16
                       ? MF->getRegInfo().createVirtualRegister(&MCS251::GPR32RegClass)
                       : Dst;
    BuildMI(*BB, MI, DL, TII->get(MCS251::MOVDRri), Low)
        .addImm(Value & 0xffff);
    if (Value >> 16)
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOVHDRi), Dst)
          .addReg(Low)
          .addImm(Value >> 16);
    MI.eraseFromParent();
    return BB;
  }
  case MCS251::SRL32ri:
  case MCS251::SRA32ri: {
    // 32-bit right shift by a constant, as a CY-chained byte rotate.  There
    // is no native dword shift, so each bit step moves the lanes through the
    // carry flag, MSB first:
    //
    //   SRL:  clr c                         ; CY = 0
    //   SRA:  mov a, msb / rlc a            ; CY = sign bit
    //   then for each lane, MSB to LSB:
    //         mov a, lane / rrc a / mov lane, a
    //
    // (mov a,rX / rrc a / mov rX,a is the measurement-verified classic
    // encoding family; rrc rotates the 9-bit {CY,A} right, so each lane's
    // old bit 0 enters the next lane's bit 7 via CY -- exactly a 1-bit
    // logical right shift of the 32-bit lane tuple.  For SRA the preloaded
    // sign bit makes the same chain arithmetic.)
    //
    // Register-file byte order is big-endian (lower position = more
    // significant): MSB..LSB = sub_hi16.sub_hi8, sub_hi16.sub_lo8,
    // sub_lo16.sub_hi8, sub_lo16.sub_lo8.  Every lane value lives in a fresh
    // single-def vreg per step (MachineCSE's getUniqueVRegDef assumption;
    // see the BRCC8S comment).
    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    unsigned Cnt = (unsigned)MI.getOperand(2).getImm();
    const bool Arith = MI.getOpcode() == MCS251::SRA32ri;
    if (Cnt > 31)
      Cnt = 31;  // LowerShift already clamps; belt and braces
    if (Cnt == 0) {
      BuildMI(*BB, MI, DL, TII->get(MCS251::MOV32rr), Dst).addReg(Src);
      MI.eraseFromParent();
      return BB;
    }
    Register WrHi = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    Register WrLo = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), WrHi)
        .addReg(Src, RegState(), MCS251::sub_hi16);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), WrLo)
        .addReg(Src, RegState(), MCS251::sub_lo16);
    Register Lanes[4];
    auto ExtractLane = [&](Register Wr, unsigned Sub, Register &Out) {
      Out = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY), Out)
          .addReg(Wr, RegState(), Sub);
    };
    ExtractLane(WrHi, MCS251::sub_hi8, Lanes[0]);  // MSB
    ExtractLane(WrHi, MCS251::sub_lo8, Lanes[1]);
    ExtractLane(WrLo, MCS251::sub_hi8, Lanes[2]);
    ExtractLane(WrLo, MCS251::sub_lo8, Lanes[3]);  // LSB
    for (unsigned I = 0; I < Cnt; ++I) {
      if (Arith) {
        BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8a)).addReg(Lanes[0]);
        BuildMI(*BB, MI, DL, TII->get(MCS251::RLCA));
      } else {
        BuildMI(*BB, MI, DL, TII->get(MCS251::CLRC));
      }
      for (int J = 0; J < 4; ++J) {
        Register Next = MRI.createVirtualRegister(&MCS251::GPR8RegClass);
        BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8a)).addReg(Lanes[J]);
        BuildMI(*BB, MI, DL, TII->get(MCS251::RRCA));
        BuildMI(*BB, MI, DL, TII->get(MCS251::MOV8ra), Next);
        Lanes[J] = Next;
      }
    }
    Register NewHi = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    Register NewLo = MRI.createVirtualRegister(&MCS251::GPR16RegClass);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), NewHi)
        .addReg(Lanes[0]).addImm(MCS251::sub_hi8)
        .addReg(Lanes[1]).addImm(MCS251::sub_lo8);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), NewLo)
        .addReg(Lanes[2]).addImm(MCS251::sub_hi8)
        .addReg(Lanes[3]).addImm(MCS251::sub_lo8);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), Dst)
        .addReg(NewHi).addImm(MCS251::sub_hi16)
        .addReg(NewLo).addImm(MCS251::sub_lo16);
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

MachineBasicBlock *MCS251TargetLowering::emitVariableShift(
    MachineInstr &MI, MachineBasicBlock *BB) const {
  MachineFunction &MF = *BB->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  Register Count = MRI.createVirtualRegister(&MCS251::GPR8LowRegClass);
  BuildMI(*BB, MI, DL, TII.get(TargetOpcode::COPY), Count)
      .addReg(MI.getOperand(2).getReg());
  unsigned Kind = MI.getOperand(3).getImm();
  const TargetRegisterClass *RC = MRI.getRegClass(Dst);

  // Layout: BB -> Zero -> Loop -> Back -> Done. Conditional targets are
  // adjacent skip blocks, never a potentially distant loop body/exit. Keep
  // the same uniform-successor invariant as expandLongConditionalBranch.
  auto NewBlock = [&]() {
    auto *MBB = MF.CreateMachineBasicBlock();
    MF.insert(std::next(BB->getIterator()), MBB);
    return MBB;
  };
  MachineBasicBlock *Done = NewBlock();
  MachineBasicBlock *Back = NewBlock();
  MachineBasicBlock *Loop = NewBlock();
  MachineBasicBlock *Zero = NewBlock();
  Done->splice(Done->end(), BB, std::next(MI.getIterator()), BB->end());
  Done->transferSuccessorsAndUpdatePHIs(BB);

  BuildMI(BB, DL, TII.get(MCS251::CMP8ri)).addReg(Count).addImm(0);
  BuildMI(BB, DL, TII.get(MCS251::JE)).addMBB(Zero);
  BuildMI(BB, DL, TII.get(MCS251::EJMP)).addMBB(Loop);
  BB->addSuccessor(Zero);
  BB->addSuccessor(Loop);
  BuildMI(Zero, DL, TII.get(MCS251::EJMP)).addMBB(Done);
  Zero->addSuccessor(Done);

  Register Value = MRI.createVirtualRegister(RC);
  Register Shifted = MRI.createVirtualRegister(RC);
  Register Remaining = MRI.createVirtualRegister(&MCS251::GPR8LowRegClass);
  Register NextCount = MRI.createVirtualRegister(&MCS251::GPR8LowRegClass);
  BuildMI(Loop, DL, TII.get(TargetOpcode::PHI), Value)
      .addReg(Src).addMBB(BB).addReg(Shifted).addMBB(Back);
  BuildMI(Loop, DL, TII.get(TargetOpcode::PHI), Remaining)
      .addReg(Count).addMBB(BB).addReg(NextCount).addMBB(Back);
  unsigned Opc;
  if (MI.getOpcode() == MCS251::VSHIFT32)
    Opc = Kind == 0 ? MCS251::ADD32rr
          : Kind == 1 ? MCS251::SRL32one : MCS251::SRA32one;
  else if (MI.getOpcode() == MCS251::VSHIFT16)
    Opc = Kind == 0 ? MCS251::SLL16
          : Kind == 1 ? MCS251::SRL16 : MCS251::SRA16;
  else
    Opc = Kind == 0 ? MCS251::SLL8
          : Kind == 1 ? MCS251::SRL8 : MCS251::SRA8;
  auto Shift = BuildMI(Loop, DL, TII.get(Opc), Shifted).addReg(Value);
  if (Opc == MCS251::ADD32rr)
    Shift.addReg(Value);
  // Keep the decrement separate from the terminator. A DJNZ defining a
  // cross-block vreg makes FastRA insert its spill AFTER the branch, which
  // the taken edge would skip. SUB/JNE lets every spill precede the branch.
  // Remaining and Shifted have overlapping live ranges, so their register
  // units must be disjoint (including WR/DR lanes).
  BuildMI(Loop, DL, TII.get(MCS251::SUB8ri), NextCount)
      .addReg(Remaining).addImm(1);
  BuildMI(Loop, DL, TII.get(MCS251::JNE)).addMBB(Back);
  BuildMI(Loop, DL, TII.get(MCS251::EJMP)).addMBB(Done);
  Loop->addSuccessor(Back);
  Loop->addSuccessor(Done);
  BuildMI(Back, DL, TII.get(MCS251::EJMP)).addMBB(Loop);
  Back->addSuccessor(Loop);
  BuildMI(*Done, Done->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(Src).addMBB(Zero).addReg(Shifted).addMBB(Loop);
  MI.eraseFromParent();
  return Done;
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
  // MCS251PassConfig disables tail merging to protect this invariant too.
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
// Canonical pointers use GPR32 and physical low-24-bit addresses. DR indexed
// accesses add a signed dis16; larger offsets are folded with ADD32. Direct
// constants <= 0xff retain the established SFR convention (0x80..0xff), which
// differs from indirect edata at the same numeric address. This convention
// is target-specific: taking an SFR address through a runtime pointer is not
// a portable way to access SFRs. i16/i32 objects use ordered big-endian bytes,
// while spills use native WR memory instructions.

namespace {
// One classified load/store address.
struct MCS251Address {
  // A GPR16 or GPR32 pointer selected from the address space's numeric layout.
  // Larger offsets are folded into Base before selecting an indirect access.
  SDValue Base;
  int64_t Disp = 0;
  // Direct addressing (i8 accesses only): constant address <= 0xff via the
  // dir8 form, covering page-zero edata (0x00-0x7f) and the SFR space
  // (0x80-0xff) -- see the trap above.
  bool IsDirect = false;
  uint64_t DirectAddr = 0;
  // Region-00 RAM reached by the hardware-verified @WR instruction family.
  bool IsNear = false;
  // Frame-relative addressing (Phase 9): the base is a stack object. The
  // displacement is carried as an unresolved (FrameIndex, offset) pair into
  // the mcs251_stack operand; PEI folds it against the frame top
  // (@dr60-0x.... normally, @dr16 when the function has dynamic allocas).
  bool IsStack = false;
  int StackFI = 0;
};
} // namespace

// Materialises a frame-index pointer as a zero-extended GPR32 value (the FIADDR
// pseudo; the custom inserter builds the SFR-direct SP reads). Only used
// when a frame pointer participates in REGISTER arithmetic (alloca[i]):
// a direct load/store base stays frame-relative and never materialises.
static SDValue materializeFrameIndex(int FrameIdx, int64_t Off, EVT PtrVT,
                                     const SDLoc &DL, SelectionDAG &DAG) {
  if (DAG.getMachineFunction().getFrameInfo().hasVarSizedObjects())
    report_fatal_error("MCS251: taking the address of a frame object in a "
                       "function with dynamic allocas is not supported "
                       "(needs an anchor read, not yet implemented)");
  SDValue TFI = DAG.getTargetFrameIndex(FrameIdx, MVT::i16);
  SDValue P(DAG.getMachineNode(MCS251::FIADDR, DL, MVT::i16, TFI), 0);
  if (PtrVT == MVT::i32) {
    SDValue Zero = materializeImm(DAG.getConstant(0, DL, MVT::i16), DL, DAG);
    P = makeDR(Zero, P, DL, DAG);
  } else {
    assert(PtrVT == MVT::i16 && "unexpected frame pointer type");
  }
  if (Off == 0)
    return P;
  if (PtrVT == MVT::i16)
    return SDValue(DAG.getMachineNode(
                       MCS251::ADD16ri, DL, MVT::i16,
                       {P, DAG.getTargetConstant(Off, DL, MVT::i16)}),
                   0);
  SDValue Disp = materializeImm(DAG.getConstant(Off, DL, MVT::i32), DL, DAG);
  return SDValue(DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32,
                                    {P, Disp}), 0);
}

// Materialise a 16-bit constant into a GPR16 vreg.
static SDValue buildMOV16ri(uint64_t Imm, const SDLoc &DL, SelectionDAG &DAG) {
  return SDValue(
      DAG.getMachineNode(MCS251::MOV16ri, DL, MVT::i16,
                         DAG.getTargetConstant(Imm & 0xffff, DL, MVT::i16)),
      0);
}

// Fold an out-of-range displacement into the pointer value. Near arithmetic
// stays i16; it is never widened to DR and then accidentally left untruncated.
static SDValue foldDispIntoBase(SDValue Base, int64_t Disp, const SDLoc &DL,
                                SelectionDAG &DAG) {
  if (Disp == 0)
    return Base;
  EVT PtrVT = Base.getValueType();
  if (PtrVT == MVT::i16)
    return SDValue(DAG.getMachineNode(
                       MCS251::ADD16ri, DL, MVT::i16,
                       {Base, DAG.getTargetConstant(Disp, DL, MVT::i16)}),
                   0);
  assert(PtrVT == MVT::i32 && "unexpected pointer type");
  SDValue D = materializeImm(DAG.getConstant(Disp, DL, MVT::i32), DL, DAG);
  return SDValue(DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32,
                                    {Base, D}), 0);
}

// DF0 P0-A/P0-B: Reject non-zero AS data uses fail-closed, by *use* not by
// number. AS4 (CODE) data store is always rejected; AS4 data load, AS5 (bit
// space), AS7 (reserved) and any unassigned AS are not yet implemented for
// data access. AS4 *function* addresses (calls/returns) are a separate
// capability and must not be caught here.
//
// Allowed data access AS set: {0,1,2,3,6,8,9} (AS0 default RAM, AS1/2/8
// near RAM, AS3/9 far RAM, AS6 SFR direct-byte). This mirrors the implemented
// Shizuku Tiny/XTiny lowering and is NOT a blanket "number allocated" pass:
// AS5/AS7 are allocated in the layout but their data access is still rejected.
static void checkDataAddressSpace(unsigned AS, bool IsStore) {
  if (AS == 4) {
    // CODE: store is always forbidden; load is not yet implemented.
    report_fatal_error(IsStore
                          ? "MCS251: store to CODE (address space 4) is not "
                            "permitted; CODE is read-only"
                          : "MCS251: CODE (address space 4) data load is not "
                            "yet implemented");
  }
  if (AS == 5)
    report_fatal_error("MCS251: address space 5 (bit space) data access is not "
                       "supported; use the controlled-bit lvalue mechanism "
                       "instead");
  if (AS == 7)
    report_fatal_error("MCS251: address space 7 is reserved and its data "
                       "access is not yet implemented");
  // AS0/1/2/3/6/8/9 are the implemented data-access set; anything else is
  // unassigned and must not fall back to a DataLayout p0 default.
  static const unsigned Implemented[] = {0, 1, 2, 3, 6, 8, 9};
  for (unsigned A : Implemented)
    if (AS == A)
      return;
  report_fatal_error("MCS251: address space " + Twine(AS) +
                     " is not allocated for data access; refusing to fall "
                     "back to the default address space");
}

// Classify a load/store pointer. AllowDirect permits the dir8 form (i8
// accesses only; an i16 access must never use it: bytes 0x7f|0x80 would
// silently straddle the page-zero/SFR boundary, and the SFR space is not
// contiguous i16 storage anyway).
static MCS251Address parseAddress(SDValue Ptr, const SDLoc &DL,
                                  SelectionDAG &DAG, bool AllowDirect,
                                  unsigned AddressSpace, unsigned AccessSize) {
  MCS251Address A;
  int64_t Off = 0;
  EVT PtrVT = Ptr.getValueType();
  A.IsNear = PtrVT == MVT::i16;
  if (A.IsNear) {
    if (AddressSpace == 6) {
      if (AccessSize != 1)
        report_fatal_error("MCS251: SFR address space supports only byte access");
    } else if (AddressSpace != 0 && AddressSpace != 1 && AddressSpace != 2 &&
               AddressSpace != 8) {
      report_fatal_error("MCS251: 16-bit generic RAM access requires address "
                         "space 0, 1, 2 or 8");
    }
  } else if (PtrVT != MVT::i32 ||
             (AddressSpace != 0 && AddressSpace != 3 && AddressSpace != 9)) {
    report_fatal_error("MCS251: unsupported address space for generic RAM access");
  }

  // Peel signed GEP offsets in the index width selected by the DataLayout.
  while (Ptr.getOpcode() == ISD::ADD) {
    SDValue LHS = Ptr.getOperand(0), RHS = Ptr.getOperand(1);
    if (auto *C = dyn_cast<ConstantSDNode>(RHS)) {
      Off += C->getSExtValue();
      Ptr = LHS;
      continue;
    }
    if (auto *C = dyn_cast<ConstantSDNode>(LHS)) {
      Off += C->getSExtValue();
      Ptr = RHS;
      continue;
    }
    // Register + register: materialise one 32-bit add. Recursing keeps
    // nested folded offsets (add (add p, 1), q) out of the operands. A
    // frame-index side leaves the frame-relative form here: it becomes a
    // plain pointer (materializeFrameIndex) so the register add covers it
    // (this is the alloca[i] shape).
    MCS251Address L = parseAddress(LHS, DL, DAG, /*AllowDirect=*/false,
                                   AddressSpace, AccessSize);
    MCS251Address R = parseAddress(RHS, DL, DAG, /*AllowDirect=*/false,
                                   AddressSpace, AccessSize);
    assert(!L.IsDirect && !R.IsDirect && "direct requires AllowDirect");
    SDValue LBase =
        L.IsStack ? materializeFrameIndex(L.StackFI, L.Disp, PtrVT, DL, DAG)
                  : foldDispIntoBase(L.Base, L.Disp, DL, DAG);
    SDValue RBase =
        R.IsStack ? materializeFrameIndex(R.StackFI, R.Disp, PtrVT, DL, DAG)
                  : foldDispIntoBase(R.Base, R.Disp, DL, DAG);
    unsigned AddOpc = PtrVT == MVT::i16 ? MCS251::ADD16rr : MCS251::ADD32rr;
    Ptr = SDValue(DAG.getMachineNode(AddOpc, DL, PtrVT, {LBase, RBase}), 0);
    break;
  }

  if (AddressSpace == 6 && !isa<ConstantSDNode>(Ptr))
    report_fatal_error("MCS251: SFR access requires a constant direct-byte "
                       "address");

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

  // Keep the full symbol+addend for byte-of-24 link-time relocations. For a
  // near symbol, however, the complete offset must be applied by i16 address
  // formation rather than delegated to an unproven relocation/displacement
  // combination at the 16-bit wrap boundary.
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Ptr)) {
    int64_t SymOff = GA->getOffset() + Off;
    if (A.IsNear) {
      A.Base = SDValue(
          DAG.getMachineNode(MCS251::MOV16ri, DL, PtrVT,
                             DAG.getTargetGlobalAddress(
                                 GA->getGlobal(), DL, PtrVT, /*Offset=*/0)),
          0);
      A.Base = foldDispIntoBase(A.Base, SymOff, DL, DAG);
    } else {
      A.Base = SDValue(
          DAG.getMachineNode(MCS251::MOVADDR32, DL, PtrVT,
                             DAG.getTargetGlobalAddress(GA->getGlobal(), DL,
                                                        PtrVT, SymOff)),
          0);
    }
    return A;
  }
  if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Ptr)) {
    unsigned Opc = PtrVT == MVT::i16 ? MCS251::MOV16ri : MCS251::MOVADDR32;
    A.Base = SDValue(DAG.getMachineNode(
                         Opc, DL, PtrVT,
                         DAG.getTargetExternalSymbol(ES->getSymbol(), PtrVT)),
                     0);
    // External symbols carry no offset field; fold Off below.
  } else if (isa<ConstantPoolSDNode>(Ptr)) {
    // The MC layer has no MO_ConstantPoolIndex lowering; rejecting here is
    // honest instead of failing later with a misleading operand error.
    report_fatal_error(
        "MCS251: constant-pool addresses are not supported (constant "
        "islands arrive with a later phase)");
  } else if (auto *C = dyn_cast<ConstantSDNode>(Ptr)) {
    // Preserve the direct/SFR convention only for absolute byte addresses.
    uint64_t K = (C->getZExtValue() + Off) &
                 (PtrVT == MVT::i16 ? 0xffffULL : 0xffffffffULL);
    if (AddressSpace == 6) {
      if (K == 0xff)
        report_fatal_error("MCS251: SFR address 0xff is permanently forbidden");
      if (K < 0x80 || K > 0xfe)
        report_fatal_error("MCS251: SFR byte address must be in 0x80..0xfe");
      A.IsDirect = true;
      A.DirectAddr = K;
      A.IsNear = false;
      return A;
    }
    bool LegacyDirect = AllowDirect && AddressSpace == 0 && PtrVT == MVT::i32 &&
                        DAG.getDataLayout().getProgramAddressSpace() == 0;
    if (LegacyDirect && K <= 0xff) {
      A.IsDirect = true;
      A.DirectAddr = K;
      return A;
    }
    A.Base = materializeImm(DAG.getConstant(K, DL, PtrVT), DL, DAG);
    return A;
  } else if (isa<JumpTableSDNode>(Ptr) || isa<BlockAddressSDNode>(Ptr)) {
    report_fatal_error(
        "MCS251: jump-table/block-address data addresses are not supported");
  } else {
    A.Base = Ptr;
    int64_t Last = Off + AccessSize - 1;
    // A near displacement is added by the addressing mode after the i16 base
    // value has been formed. Unless the base range is proven, even a small
    // displacement can cross 0xffff/0x0000 and no longer implement LLVM's i16
    // GEP wrap. Materialize the complete i16 address first and use dis16=0.
    bool CanFold = A.IsNear ? Off == 0
                            : Off >= -32768 && Last <= 32767;
    if (CanFold)
      A.Disp = Off;
    else
      A.Base = foldDispIntoBase(Ptr, Off, DL, DAG);
    return A;
  }

  // External-symbol base with a folded offset: same displacement treatment
  // as the register-base case (the symbol itself accepts no offset).
  int64_t Last = Off + AccessSize - 1;
  bool CanFold = A.IsNear ? Off == 0
                          : Off >= -32768 && Last <= 32767;
  if (CanFold)
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
        MCS251::MOV8rmF, DL, ResTys,
        {DAG.getRegister(MCS251::DR60, MVT::i16),
         DAG.getTargetFrameIndex(A.StackFI, MVT::i16),
         DAG.getTargetConstant(A.Disp, DL, MVT::i16), Chain});
  } else if (A.IsDirect) {
    N = DAG.getMachineNode(MCS251::MOV8di, DL, ResTys,
                           {DAG.getTargetConstant(A.DirectAddr, DL, MVT::i8),
                            Chain});
  } else if (A.IsNear) {
    if (A.Disp == 0)
      N = DAG.getMachineNode(MCS251::MOV8rm, DL, ResTys, {A.Base, Chain});
    else
      N = DAG.getMachineNode(
          MCS251::MOV8rmD, DL, ResTys,
          {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Chain});
  } else {
    N = DAG.getMachineNode(
        MCS251::MOV8rmP, DL, ResTys,
        {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Chain});
  }
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
  if (MemVT != MVT::i8 && MemVT != MVT::i16 && MemVT != MVT::i32)
    report_fatal_error("MCS251: only i8/i16/i32 memory objects are supported (load)");

  // DF0 P0-B: reject unimplemented non-zero AS data loads before parseAddress
  // classifies the pointer value. parseAddress keeps its own width/AS guard as
  // a second line of defence; the check here gives a use-specific diagnostic.
  checkDataAddressSpace(LD->getAddressSpace(), /*IsStore=*/false);

  unsigned Size = MemVT.getSizeInBits() / 8;
  MCS251Address A = parseAddress(LD->getBasePtr(), DL, DAG,
                                 /*AllowDirect=*/MemVT == MVT::i8,
                                 LD->getAddressSpace(), Size);

  // Preserve the established big-endian object layout, including the new
  // four-byte pointer slots. Each byte retains exact MMO offset and ordering.
  SmallVector<SDValue, 4> Bytes;
  SDValue Chain = LD->getChain();
  for (unsigned I = 0; I < Size; ++I) {
    MCS251Address ByteAddr = A;
    ByteAddr.Disp += I;
    if (ByteAddr.IsNear && !ByteAddr.IsStack && ByteAddr.Disp) {
      ByteAddr.Base =
          foldDispIntoBase(ByteAddr.Base, ByteAddr.Disp, DL, DAG);
      ByteAddr.Disp = 0;
    }
    auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
        LD->getMemOperand(), I, /*Size=*/1);
    SDValue Byte = buildByteLoad(ByteAddr, DL, DAG, Chain, MMO);
    Bytes.push_back(Byte);
    Chain = Byte.getValue(1);
  }
  SDValue Res = Bytes[0];
  if (Size >= 2)
    Res = makeWord(Bytes[0], Bytes[1], DL, DAG);
  if (Size == 4)
    Res = makeDR(Res, makeWord(Bytes[2], Bytes[3], DL, DAG), DL, DAG);
  if (ValVT != MemVT)
    // Forward the extension kind: SEXTLOAD routes through SIGN_EXTEND into
    // LowerExtend's bias-identity sequence (the new node re-enters the
    // legalizer and hits the Custom SIGN_EXTEND action). EXTLOAD is
    // refined to zero on purpose (legal anyext refinement, existing
    // behaviour, unchanged).
    Res = DAG.getNode(LD->getExtensionType() == ISD::SEXTLOAD
                          ? ISD::SIGN_EXTEND
                          : ISD::ZERO_EXTEND,
                      DL, ValVT, Res);
  return DAG.getMergeValues({Res, Chain}, DL);
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
        MCS251::MOV8mrF, DL, MVT::Other,
        {DAG.getRegister(MCS251::DR60, MVT::i16),
         DAG.getTargetFrameIndex(A.StackFI, MVT::i16),
         DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val, Chain});
  } else if (A.IsDirect) {
    N = DAG.getMachineNode(
        MCS251::MOV8id, DL, MVT::Other,
        {DAG.getTargetConstant(A.DirectAddr, DL, MVT::i8), Val, Chain});
  } else if (A.IsNear) {
    if (A.Disp == 0)
      N = DAG.getMachineNode(MCS251::MOV8mr, DL, MVT::Other,
                             {A.Base, Val, Chain});
    else
      N = DAG.getMachineNode(
          MCS251::MOV8mrD, DL, MVT::Other,
          {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val, Chain});
  } else {
    N = DAG.getMachineNode(
        MCS251::MOV8mrP, DL, MVT::Other,
        {A.Base, DAG.getTargetConstant(A.Disp, DL, MVT::i16), Val, Chain});
  }
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
  if (MemVT != MVT::i8 && MemVT != MVT::i16 && MemVT != MVT::i32)
    report_fatal_error("MCS251: only i8/i16/i32 memory objects are supported (store)");

  // DF0 P0-A: reject CODE (AS4) stores and all unimplemented non-zero AS data
  // stores before parseAddress touches the pointer value.
  checkDataAddressSpace(ST->getAddressSpace(), /*IsStore=*/true);

  unsigned Size = MemVT.getSizeInBits() / 8;
  MCS251Address A = parseAddress(ST->getBasePtr(), DL, DAG,
                                 /*AllowDirect=*/MemVT == MVT::i8,
                                 ST->getAddressSpace(), Size);

  SmallVector<SDValue, 4> Bytes;
  if (auto *C = dyn_cast<ConstantSDNode>(Val)) {
    for (unsigned I = 0; I < Size; ++I)
      Bytes.push_back(buildMOV8ri(C->getZExtValue() >> ((Size - I - 1) * 8), DL, DAG));
  } else {
    if (ST->isTruncatingStore())
      Val = DAG.getNode(ISD::TRUNCATE, DL, MemVT, Val);
    if (Size == 4) {
      splitI32ToBytes(Val, DL, DAG, Bytes);
      std::reverse(Bytes.begin(), Bytes.end());
    } else if (Size == 2) {
      Bytes.push_back(DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8, Val));
      Bytes.push_back(DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Val));
    } else {
      Bytes.push_back(Val);
    }
  }
  SDValue Chain = ST->getChain();
  for (unsigned I = 0; I < Size; ++I) {
    MCS251Address ByteAddr = A;
    ByteAddr.Disp += I;
    if (ByteAddr.IsNear && !ByteAddr.IsStack && ByteAddr.Disp) {
      ByteAddr.Base =
          foldDispIntoBase(ByteAddr.Base, ByteAddr.Disp, DL, DAG);
      ByteAddr.Disp = 0;
    }
    auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
        ST->getMemOperand(), I, /*Size=*/1);
    Chain = buildByteStore(ByteAddr, Bytes[I], DL, DAG, Chain, MMO);
  }
  return Chain;
}

// Sign-extension core, shared by LowerExtend (SIGN_EXTEND, including the
// SIGN_EXTEND that LowerLoad forwards for SEXTLOAD) and LowerSignExtendInReg.
// Offset-binary identity, proven for ALL inputs (not just boundary samples):
//
//     sext_N(x) == zext_N(x ^ 2^(m-1)) + (2^N - 2^(m-1))   (mod 2^N)
//
// The bias addition MUST wrap unsigned: for non-negative inputs the sum
// is 2^N + x (exactly 2^N only at x = 0) and only the wrap lands it back
// on x (QEMU-observed carry-out, SEXTLOAD P-A2). Machine ADD nodes carry
// no nuw/nsw semantics -- keep it that way in any future rewrite.
//
// Per-width released main forms (all operand encodings QEMU-run-verified,
// SEXTLOAD-DESIGN 2.5 / P-A verdicts):
//   i8->i16  (a): zero hi lane + xrl #0x80 + add wr,#0xff80
//   i8->i32  (c): lanes {00,00,00,x^80} + full 32-bit bias 0xffffff80
//                 (MOV32ri -> mov dr,#0xff80 / movh dr,#0xffff) + add dr,dr;
//                 assembled directly at 32 bits, no intermediate i16 sext
//   i16->i32 (d): lanes {00,00,hi^80,lo} + bias 0xffff8000 + add dr,dr;
//                 the 0x8000 flip lands on the source's big-endian
//                 Bytes[0] (sub_hi8) -- the sign-byte position shared by
//                 all three widths
//
// Lane discipline: makeWord/makeDR (REG_SEQUENCE) only assemble lanes and
// never fabricate zeros; every zero lane is an explicit MOV8ri. Identical
// MOV8ri(0) and EXTRACT_SUBREG machine nodes CSE together
// (SelectionDAG::getMachineNode), so zero lanes and lane extractions may
// be shared by several consumers -- CSE is not a privacy barrier and the
// source is never written through in the first place: the lowering only
// reads it (SSA dataflow), the flip is a tied XOR8ri whose operand
// TwoAddressInstructionPass turns into a COPY whenever the (possibly
// shared) value has other users, and register-allocation interference
// keeps a clobbering def from coalescing over a still-live value
// (sext-pressure.ll binds exactly this as MIR dataflow).
static SDValue buildSignExtend(SDValue Src, EVT DstVT, const SDLoc &DL,
                               SelectionDAG &DAG) {
  EVT SrcVT = Src.getValueType();
  assert(((SrcVT == MVT::i8 && (DstVT == MVT::i16 || DstVT == MVT::i32)) ||
          (SrcVT == MVT::i16 && DstVT == MVT::i32)) &&
         "only i8->i16, i8->i32 and i16->i32 sign extensions are custom");

  // x ^ 0x80 on the sign byte (the source's most significant byte at every
  // width).
  auto FlipSignByte = [&](SDValue Byte) {
    return SDValue(
        DAG.getMachineNode(MCS251::XOR8ri, DL, MVT::i8,
                           {Byte, DAG.getTargetConstant(0x80, DL, MVT::i8)}),
        0);
  };

  if (SrcVT == MVT::i8) {
    SDValue Flipped = FlipSignByte(Src);
    if (DstVT == MVT::i16) {
      // (a): wr = {00, x^80}; wr += 0xff80 (mod 2^16).
      SDValue Z16 = makeWord(buildMOV8ri(0, DL, DAG), Flipped, DL, DAG);
      return SDValue(DAG.getMachineNode(
                         MCS251::ADD16ri, DL, MVT::i16,
                         {Z16, DAG.getTargetConstant(0xff80, DL, MVT::i16)}),
                     0);
    }
    // (c): dr = {00, 00, 00, x^80}; dr += 0xffffff80 (mod 2^32).
    SDValue Z32 = makeDR(makeWord(buildMOV8ri(0, DL, DAG),
                                  buildMOV8ri(0, DL, DAG), DL, DAG),
                         makeWord(buildMOV8ri(0, DL, DAG), Flipped, DL, DAG),
                         DL, DAG);
    SDValue Bias(DAG.getMachineNode(
                     MCS251::MOV32ri, DL, MVT::i32,
                     DAG.getTargetConstant(0xffffff80u, DL, MVT::i32)),
                 0);
    return SDValue(
        DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32, {Z32, Bias}), 0);
  }

  // (d): i16 -> i32. The sub_hi8/sub_lo8 extractions may be CSE-shared
  // with other consumers of the same word; source preservation rests on
  // the SSA read-only lowering plus the tied XOR/liveness/RA-interference
  // mechanism described above, not on any privacy of the extraction.
  SDValue Hi = DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8, Src);
  SDValue Lo = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Src);
  SDValue Z32 = makeDR(makeWord(buildMOV8ri(0, DL, DAG),
                                buildMOV8ri(0, DL, DAG), DL, DAG),
                       makeWord(FlipSignByte(Hi), Lo, DL, DAG), DL, DAG);
  SDValue Bias(DAG.getMachineNode(
                   MCS251::MOV32ri, DL, MVT::i32,
                   DAG.getTargetConstant(0xffff8000u, DL, MVT::i32)),
               0);
  return SDValue(
      DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32, {Z32, Bias}), 0);
}

SDValue MCS251TargetLowering::LowerExtend(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();
  bool Signed = Op.getOpcode() == ISD::SIGN_EXTEND;
  if (Signed) {
    // Offset-binary bias identity -- see buildSignExtend above. One
    // implementation point covers explicit sext and the SIGN_EXTEND that
    // LowerLoad forwards for SEXTLOAD.
    return buildSignExtend(Src, Op.getValueType(), DL, DAG);
  }
  if (SrcVT == MVT::i8)
    Src = SDValue(DAG.getMachineNode(MCS251::ZEXT8, DL, MVT::i16, Src), 0);
  if (Op.getValueType() == MVT::i16)
    return Src;
  return makeDR(buildMOV16ri(0, DL, DAG), Src, DL, DAG);
}

// SIGN_EXTEND_INREG is DAGCombiner's sext(trunc x) form (result type equals
// the widened operand's type; operand 1 is a VTSDNode carrying the inner
// width). The action table is keyed by that INNER type, so i8/i16 are the
// registered Custom entries; anything else keeps the default action and
// never reaches this hook. The operand's upper bits are unspecified;
// only the effective inner-width lanes contribute to the result, so drop
// the rest and reuse the plain widening core.
SDValue MCS251TargetLowering::LowerSignExtendInReg(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT InnerVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  if (InnerVT == MVT::i8) {
    // Effective byte = big-endian Bytes[3]: low byte of the low word.
    if (DstVT == MVT::i32)
      Src = DAG.getTargetExtractSubreg(MCS251::sub_lo16, DL, MVT::i16, Src);
    Src = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Src);
  } else {
    assert(InnerVT == MVT::i16 && DstVT == MVT::i32 &&
           "SIGN_EXTEND_INREG shape without a registered action");
    Src = DAG.getTargetExtractSubreg(MCS251::sub_lo16, DL, MVT::i16, Src);
  }
  return buildSignExtend(Src, DstVT, DL, DAG);
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
  EVT PtrVT = Op.getValueType();
  if (PtrVT == MVT::i32)
    SPX = makeDR(buildMOV16ri(0, DL, DAG), SPX, DL, DAG);
  else
    assert(PtrVT == MVT::i16 && "unexpected stacksave pointer type");
  return DAG.getMergeValues({SPX, Hi.getValue(1)}, DL);
}

SDValue MCS251TargetLowering::LowerSTACKRESTORE(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue SavedSPX = Op.getOperand(1);
  if (SavedSPX.getValueType() == MVT::i32)
    SavedSPX = extractLane(SavedSPX, MCS251::sub_lo16, DL, DAG);
  else
    assert(SavedSPX.getValueType() == MVT::i16 &&
           "unexpected stackrestore pointer type");
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

  // SPX arithmetic remains 16-bit. Objects must fit the physical stack;
  // allocation amounts above 65535 are outside this target's stack model.
  if (Size.getValueType() == MVT::i32)
    Size = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Size);
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

  SDValue Alloc(DAG.getMachineNode(MCS251::DYNALLOCA, DL,
      DAG.getVTList(MVT::i16, MVT::Other), {Size, Chain}), 0);
  EVT PtrVT = Op.getValueType();
  SDValue Ptr = Alloc;
  if (PtrVT == MVT::i32)
    Ptr = makeDR(buildMOV16ri(0, DL, DAG), Alloc, DL, DAG);
  else
    assert(PtrVT == MVT::i16 && "unexpected dynamic alloca pointer type");
  return DAG.getMergeValues({Ptr, Alloc.getValue(1)}, DL);
}

// Static argument slots use the *mangled function symbol* plus _PARM_n.
// The leading \1 prevents the external-symbol path from mangling it twice.
static SDValue parameterSlot(StringRef Callee, unsigned Index,
                             SelectionDAG &DAG) {
  std::string Name = (Twine("\1") + Callee + "_PARM_" + Twine(Index + 1)).str();
  MVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  return DAG.getExternalSymbol(
      DAG.getMachineFunction().createExternalSymbolName(Name), PtrVT);
}

// Check the IR type as well as the legalized piece. A one-field aggregate (or
// an empty aggregate preceding a scalar) need not carry the ISD split flag.
static bool hasOrdinaryPointerABI(const Type *Ty) {
  auto *PT = dyn_cast<PointerType>(Ty);
  if (!PT)
    return true;
  switch (PT->getAddressSpace()) {
  case 0:
  case 1:
  case 2:
  case 3:
  case 4:
  case 8:
  case 9:
    return true;
  default:
    return false;
  }
}

static void checkParameterType(Type *Ty, unsigned Index,
                               bool AllowStaticPointers) {
  if (Ty->isPointerTy()) {
    if (!hasOrdinaryPointerABI(Ty))
      report_fatal_error("MCS251: pointer parameter address space has no "
                         "ordinary register/static-slot ABI");
    if (Index && !AllowStaticPointers)
      report_fatal_error("MCS251: static pointer parameters are not supported "
                         "by the compatibility ABI");
    return;
  }
  if (!Ty->isIntegerTy(8) && !Ty->isIntegerTy(16) && !Ty->isIntegerTy(32))
    report_fatal_error("MCS251: arguments must be unsplit i8/i16/i32 scalars");
}

template <typename ArgT>
static void checkParameter(const ArgT &Arg, unsigned Index,
                           bool AllowStaticPointers) {
  if ((Arg.VT != MVT::i8 && Arg.VT != MVT::i16 && Arg.VT != MVT::i32) ||
      Arg.ArgVT != Arg.VT || Arg.PartOffset || Arg.Flags.isSplit() ||
      Arg.Flags.isByVal() || Arg.Flags.isByRef() || Arg.Flags.isSRet() ||
      Arg.Flags.isInAlloca() || Arg.Flags.isNest())
    report_fatal_error("MCS251: arguments must be unsplit i8/i16/i32 scalars");
  if (Index && Arg.Flags.isPointer() && !AllowStaticPointers)
    report_fatal_error("MCS251: static pointer parameters are not supported "
                       "by the compatibility ABI");
}

SDValue MCS251TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    // IPO can select fastcc for local functions. MCS251 deliberately uses
    // the C physical ABI for Fast too: ABI registers plus named scalar
    // parameter slots. Call sites and definitions must still agree on CC.
    break;
  }

  if (IsVarArg)
    report_fatal_error("minimal MCS251 backend does not support variadic "
                       "functions");

  MachineFunction &MF = DAG.getMachineFunction();
  // DF0 P0-B: function placement must match the program address space (AS4 in
  // Tiny/Small v2, AS0 in compatibility mode). AS5/AS7/AS10 etc. are not
  // valid function address spaces and must be rejected even though they have
  // no load/store -- the function itself has no call/return contract there.
  unsigned FnAS = MF.getFunction().getAddressSpace();
  unsigned ProgAS = DAG.getDataLayout().getProgramAddressSpace();
  if (FnAS != ProgAS)
    report_fatal_error("MCS251: functions must reside in the program address "
                       "space (" +
                       Twine(ProgAS) + "); address space " + Twine(FnAS) +
                       " is not a valid function placement");
  bool AllowStaticPointers = DAG.getDataLayout().getProgramAddressSpace() == 4;
  for (const Argument &Arg : MF.getFunction().args())
    checkParameterType(Arg.getType(), Arg.getArgNo(), AllowStaticPointers);

  if (Ins.empty())
    return Chain;

  for (unsigned I = 0; I < Ins.size(); ++I) {
    checkParameter(Ins[I], I, AllowStaticPointers);
    if (Ins[I].OrigArgIndex != I)
      report_fatal_error("MCS251: aggregate parameters are not supported");
  }

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
      // SDCC pointers occupy B:DPH:DPL; A is unspecified, not an address
      // byte. Canonicalise it before comparisons, not just at dereference.
      if (Ins[0].Flags.isPointer() && Reg == MCS251::A) {
        Parts.push_back(buildMOV8ri(0, DL, DAG));
        continue;
      }
      Register VReg = MF.addLiveIn(Reg, &MCS251::GPR8RegClass);
      SDValue Part = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i8);
      Parts.push_back(Part);
      Chain = Part.getValue(1);
    }
    InVals.push_back(combineI32FromBytes(Parts, DL, DAG));
  } else {
    bool Byte = Ins[0].VT == MVT::i8;
    Register VReg = MF.addLiveIn(Byte ? MCS251::DPL : MCS251::DPTR,
        Byte ? &MCS251::GPR8RegClass : &MCS251::GPR16RegClass);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, Ins[0].VT);
    InVals.push_back(ArgValue);
    Chain = ArgValue.getValue(1);
  }

  // Chain entry reads before any call can overwrite an overlay slot. Unknown
  // pointer info deliberately avoids claiming that distinct slot symbols do
  // not alias: leaf functions share the same OSEG storage across modules.
  for (unsigned I = 1; I < Ins.size(); ++I) {
    SDValue Ptr = parameterSlot(
        DAG.getTarget().getSymbol(&MF.getFunction())->getName(), I, DAG);
    SDValue Value = DAG.getLoad(Ins[I].VT, DL, Chain, Ptr,
                                MachinePointerInfo(), Align(1));
    Chain = Value.getValue(1);
    SDValue ArgValue = Value;
    if (Ins[I].Flags.isPointer() && Ins[I].VT == MVT::i32)
      ArgValue = canonicalizePointer32(Value, DL, DAG);
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
// loading mirrors LowerFormalArguments: the first scalar uses the ABI
// registers and subsequent scalars use the named callee's static slots.
// Results come back through the same fixed locations
// (LowerCallResult's CopyFromReg); a returned value feeding straight into
// the caller's own return needs no intermediate copy at all -- the
// phys->virt->phys chain through dpl/dptr coalesces away (dpl/dptr are
// reserved), the SDCC-equivalent `ecall; eret` tail.
//
// CALLSEQ_START/END carry zero stack sizes but are still essential: independent
// libcalls can start on the entry chain, and their static argument stores must
// not interleave. The standard call-sequence scheduler dependency keeps each
// setup/call together; PEI erases the zero-sized call-frame pseudos.
//
// Register pressure across calls: since Phase 9 the spiller has real frame
// slots (storeRegToStackSlot/loadRegFromStackSlot -> @dr60 displacement
// accesses), so values live across a call are spilled to the frame instead
// of being a hard error. A mixed dynamic-alloca function references its
// static slots through the dr16 anchor, so those spills stay correct too.
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
  case CallingConv::Fast:
    // IPO can select fastcc for local functions. MCS251 deliberately uses
    // the C physical ABI for Fast too: ABI registers plus named scalar
    // parameter slots. Call sites and definitions must still agree on CC.
    break;
  }

  if (IsVarArg)
    report_fatal_error("minimal MCS251 backend does not support variadic "
                       "functions");

  if (CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("MCS251: musttail calls are not supported",
                       /*gen_crash_diag=*/false);
  // Ordinary tail hints, including calls without an IR CallBase, are optional.
  IsTailCall = false;

  const unsigned ProgramAS = DAG.getDataLayout().getProgramAddressSpace();
  // Direct symbols, including target-generated libcalls, may initially carry
  // getPointerTy() and therefore be i16 in Tiny. Preserve their symbol identity
  // and rebuild them as CODE pointers below. Only a true indirect address is
  // subject to pointer-container and original-IR address-space checks.
  //
  // RC-1: A GlobalAddress direct call target must be a function in the CODE
  // address space (ProgramAS). A data global masquerading as a call target, or
  // a function declared in a non-CODE address space (e.g. AS3), must be
  // rejected loudly -- both can produce ELF symbols with the wrong flags.
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = GA->getGlobal();
    if (!isa<Function>(GV))
      report_fatal_error("MCS251: direct call target is not a function");
    if (GA->getAddressSpace() != ProgramAS)
      report_fatal_error(
          "MCS251: direct call target is not in the configured CODE "
          "address space");
  }
  const bool IsDirect = isa<GlobalAddressSDNode>(Callee) ||
                        isa<ExternalSymbolSDNode>(Callee);
  if (!IsDirect) {
    if (CLI.CB) {
      auto *CalleeTy = cast<PointerType>(CLI.CB->getCalledOperand()->getType());
      unsigned CalleeAS = CalleeTy->getAddressSpace();
      if (CalleeAS != ProgramAS)
        report_fatal_error(
            "MCS251: indirect call target is not in the configured CODE "
            "address space");
    }
    if (Callee.getValueType() != MVT::i32)
      report_fatal_error(
          "MCS251: indirect call target must be a 32-bit CODE pointer");
    // ECALLr consumes the complete region-qualified address (not a WR offset).
    if (isa<ConstantSDNode>(Callee))
      Callee = materializeImm(Callee, DL, DAG);
  }

  bool AllowStaticPointers = ProgramAS == 4;
  if (CLI.CB) {
    if (!hasOrdinaryPointerABI(CLI.CB->getType()))
      report_fatal_error("MCS251: pointer return address space has no ordinary ABI");
    for (unsigned I = 0; I < CLI.CB->arg_size(); ++I)
      checkParameterType(CLI.CB->getArgOperand(I)->getType(), I,
                         AllowStaticPointers);
  }

  for (unsigned I = 0; I < Outs.size(); ++I) {
    checkParameter(Outs[I], I, AllowStaticPointers);
    if (CLI.CB && Outs[I].OrigArgIndex != I)
      report_fatal_error("MCS251: aggregate parameters are not supported");
  }

  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, DL);

  std::string SlotCallee;
  if (Outs.size() > 1) {
    if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
      if (G->getOffset() || !isa<Function>(G->getGlobal()))
        report_fatal_error("MCS251: static parameters require a named function");
      SlotCallee = DAG.getTarget().getSymbol(G->getGlobal())->getName().str();
    } else if (auto *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
      SmallString<128> Name;
      Mangler::getNameWithPrefix(Name, S->getSymbol(), DAG.getDataLayout());
      SlotCallee = Name.str().str();
    } else {
      report_fatal_error("MCS251: multi-argument indirect calls are not supported "
                         "(static parameter slots require a named callee)");
    }
    // Finish every slot store before setting up the first argument registers.
    // Memory objects use the same measured big-endian layout as SDCC.
    for (unsigned I = 1; I < Outs.size(); ++I) {
      SDValue SlotValue = OutVals[I];
      if (Outs[I].Flags.isPointer() && Outs[I].VT == MVT::i32)
        SlotValue = canonicalizePointer32(SlotValue, DL, DAG);
      Chain = DAG.getStore(Chain, DL, SlotValue,
                           parameterSlot(SlotCallee, I, DAG),
                           MachinePointerInfo(), Align(1));
    }
  }

  // Copy the arguments into their ABI registers, chained and glued so nothing
  // can be scheduled between the copies and the call.
  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SDValue InGlue;
  if (!Outs.empty() && Outs[0].VT == MVT::i32) {
    SmallVector<SDValue, 4> Parts;
    splitI32ToBytes(OutVals[0], DL, DAG, Parts);
    if (Outs[0].Flags.isPointer())
      Parts[3] = buildMOV8ri(0, DL, DAG);
    for (unsigned I = 0; I < 4; ++I)
      RegsToPass.emplace_back(I32ABIRegs[I], Parts[I]);
  } else if (!Outs.empty()) {
    RegsToPass.emplace_back(Outs[0].VT == MVT::i8 ? MCS251::DPL : MCS251::DPTR,
                            OutVals[0]);
  }
  for (const auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Wrap the callee so legalisation cannot hack the address apart: every
  // direct call reaches here as one of these two node kinds (checked above).
  // Calls always use the 32-bit AS4 CODE address, independently of AS0 data
  // pointer width. A Tiny/Huge translation unit must never truncate a function
  // symbol to its 16-bit default data-pointer type.
  MVT CodePtrVT = getPointerTy(DAG.getDataLayout(),
                               DAG.getDataLayout().getProgramAddressSpace());
  if (CodePtrVT != MVT::i32)
    report_fatal_error("MCS251: CODE call target must use a 32-bit pointer");
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, CodePtrVT,
                                        G->getOffset());
  else if (auto *S = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), CodePtrVT);

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

  Chain = DAG.getCALLSEQ_END(Chain, 0, 0, InGlue, DL);
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
      if (Ins[0].Flags.isPointer() && Reg == MCS251::A) {
        Parts.push_back(buildMOV8ri(0, DL, DAG));
        continue;
      }
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
  if (!hasOrdinaryPointerABI(RetTy))
    report_fatal_error("MCS251: pointer return address space has no ordinary ABI");
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

  // Out-of-range constant shifts become UNDEF during DAG construction,
  // before LowerShift can see them. Choose zero for an undefined scalar
  // return instead of leaving stale ABI registers (or a bare ERET at -O2).
  // This is a permitted refinement of undef, not defined shift semantics.
  auto ReturnValue = [&](unsigned I) {
    SDValue V = OutVals[I];
    return V.isUndef() ? DAG.getConstant(0, DL, V.getValueType()) : V;
  };

  SDValue Glue;
  SmallVector<SDValue, 8> RetOps(1, Chain);
  if (!Outs.empty() && Outs[0].VT == MVT::i32) {
    assert(Outs.size() == 1 && "MCS251 supports only one return value");
    SmallVector<SDValue, 4> Parts;
    splitI32ToBytes(ReturnValue(0), DL, DAG, Parts);
    if (Outs[0].Flags.isPointer())
      Parts[3] = buildMOV8ri(0, DL, DAG);
    for (unsigned I = 0; I < 4; ++I) {
      Chain = DAG.getCopyToReg(Chain, DL, I32ABIRegs[I], Parts[I], Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(I32ABIRegs[I], MVT::i8));
    }
  } else {
    for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
      CCValAssign &VA = RVLocs[I];
      assert(VA.isRegLoc() && "MCS251 return values must go to registers");
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), ReturnValue(I), Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
    }
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(MCS251ISD::ERET, DL, MVT::Other, RetOps);
}
