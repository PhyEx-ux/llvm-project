//===-- MCS251ISelLowering.cpp - MCS-251 DAG lowering --------------------===//

#include "MCS251ISelLowering.h"
#include "MCS251.h"
#include "MCS251BitObject.h"
#include "MCS251LocalInterp.h"
#include "MCS251Subtarget.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsMCS251.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/SizeOpts.h"

using namespace llvm;

// BRJT (design D4): switch jump tables are opt-in.  The default is the closed
// gate (comparison chain everywhere); `-mllvm -mcs251-jump-tables` enables the
// CODE-space ljmp table lowering in eligible clusters (qualification predicates
// E1-E6, design §3.2.6).  clang passes the flag through verbatim with -mllvm.
static cl::opt<bool> MCS251JumpTables(
    "mcs251-jump-tables", cl::Hidden, cl::init(false),
    cl::desc("MCS-251: emit CODE-space ljmp jump tables for eligible switches "
             "(default: off, every switch lowers through its comparison "
             "chain)"));

// BRJT E3 cap (design §3.2.6). The jump-table index travels through the 8-bit
// accumulator after the x3 sequence `mov rT,rI; add rT,rT; add rT,rI`
// (§3.2.1): 3*idx <= 255, i.e. idx <= 85. The Range argument of
// isSuitableForJumpTable is High-Low+1 -- the number of table entries
// including DefaultMBB-filled holes -- so the cap on entries is 86.
constexpr uint64_t MaxJumpTableEntries = 86;

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

  // BRJT (design §3.1.1). The action table entry is the areJTsAllowed gate
  // (TargetLowering.h: areJTsAllowed -> isOperationLegalOrCustom(BR_JT/BRIND)):
  // the default initActions() state would be Legal, so the upstream jump-table
  // builder would emit ISD::BR_JT for any switch with >= MinJumpTableEntries
  // cases and this target would die in "Cannot select: br_jt". Expand closes
  // the gate: no BR_JT/BRIND node is ever produced, and every switch lowers
  // through the case-cluster comparison chain above. The `-mllvm
  // -mcs251-jump-tables` opt-in (D4) re-opens the gate for BR_JT only, with
  // Custom lowering to the fixed `jmp @a+dptr` dispatch sequence (§3.2.1);
  // BRIND stays Expand in both modes (no indirectbr pattern by design, R3).
  if (!MCS251JumpTables) {
    setOperationAction(ISD::BR_JT, MVT::Other, Expand);
    setOperationAction(ISD::BRIND, MVT::Other, Expand);
  } else {
    setOperationAction(ISD::BR_JT, MVT::Other, Custom);
    setOperationAction(ISD::BRIND, MVT::Other, Expand);
  }

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

  // RuntimeLibcalls.td generates both the membership predicate and the exact
  // implementation mapping. Thus TableGen, ISel and the contract verifier
  // cannot drift into three hand-maintained descriptions of the f32 subset.
  // The helper ABI is ordinary softened i32: DPL:DPH:B:A for operand/result and
  // _PARM_2 for operand two.
  for (RTLIB::Libcall Call : RTLIB::libcalls())
    if (RTLIB::LibcallImpl Impl = getMCS251ConnectedF32LibcallImpl(Call);
        Impl != RTLIB::Unsupported)
      setLibcallImpl(Call, Impl);

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
  // (wider scalars, vectors) are deliberately NOT registered this round:
  // they keep whatever action the generic defaults give them (Legal for
  // most scalars, Expand for vector INREG and the odd narrow types,
  // TargetLoweringBase::initActions) -- no support is claimed for them,
  // and a Legal-but-unselectable node fails loudly at selection.
  //
  // Inner i1 is the exception that must NOT be left to the default: it is
  // Legal-by-default yet unselectable, so `sext i1 %x to iN` (DAGCombiner
  // folds it into SIGN_EXTEND_INREG) died at ISel with "Cannot select:
  // sign_extend_inreg ... i1". That shape is reachable from clang for a
  // signed 1-bit source, and sitofp consumes it (the signed pair must lower
  // `sitofp i1` as sext-to-iN first: LLVM/IR semantics are `sitofp i1 true
  // == -1.0`, matching LegalizeDAG.cpp's boolean expansion, versus `uitofp
  // i1 true == +1.0`). Register Expand so the generic expander turns the
  // boolean negation into AND 1 / SUB 0 that the i8/i16/i32 cores already
  // select, instead of leaking an unselectable node.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
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
  // G2 B-S2 static-slot variadic ABI (G2-VARIADIC-DESIGN-draft.md R3
  // §4.3.4/§4.3.5).  VASTART/VAEND/VACOPY lower onto the va_list
  // {owner-slot-area base, byte offset} pair; VAARG is a fail-closed reject
  // for any residual llvm.va_arg (clang lowers va_arg itself via
  // MCS251ABIInfo::EmitVAArg).  NOTE: the legalizer queries VAARG's action
  // on MVT::Other unless the value-type action is Promote (LegalizeDAG.cpp
  // case ISD::VAARG), so the reject MUST be registered on MVT::Other; the
  // per-value-type table is never consulted here.  An illegal-result VAARG
  // (f32/f64/i64) type-legalizes first and is rejected in
  // ReplaceNodeResults instead.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
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

  // Controlled bit-access intrinsics (BIT BT03). set/clear/toggle are
  // chain-only INTRINSIC_VOID nodes; read is an INTRINSIC_W_CHAIN with an i1
  // result. The type legalizer promotes that i1 through ReplaceNodeResults
  // (registered on MVT::i1), and the operation legalizer handles the
  // chain-only writes and the promoted read's re-legalization through
  // LowerOperation (registered on MVT::Other).
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::i1, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::Other, Custom);

  // i64 and true f64 IR remain unsupported. In particular, double=32 in
  // TargetInfo is a frontend ABI choice, not permission to route f64 DAG nodes
  // through binary32 helpers.
  for (MVT VT : {MVT::f64, MVT::i64}) {
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

  // The connected f32 subset is deliberately all LibCall except SETCC: generic
  // softening selects the predicate-specific FCMP3/UO helper through
  // softenSetCCOperands, preserving its complete ordered/unordered semantics.
  for (unsigned Opc : {ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV, ISD::FNEG})
    setOperationAction(Opc, MVT::f32, LibCall);
  setOperationAction(ISD::SETCC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
  setOperationAction(ISD::SINT_TO_FP, MVT::f32, LibCall);
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, LibCall);

  // All remaining f32 math and conversion families, and every f64 operation,
  // stay Custom so they fail with a target diagnostic rather than silently
  // acquiring an unrelated generic helper.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::FREM, VT, Custom);
    setOperationAction(ISD::FCOPYSIGN, VT, Custom);
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
  setOperationAction(ISD::FADD, MVT::f64, Custom);
  setOperationAction(ISD::FSUB, MVT::f64, Custom);
  setOperationAction(ISD::FMUL, MVT::f64, Custom);
  setOperationAction(ISD::FDIV, MVT::f64, Custom);
  setOperationAction(ISD::FNEG, MVT::f64, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::f64, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::f32, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::f64, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i8, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i16, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i8, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i16, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
  // i64 shifts and wide ops that currently mis-compile silently.
  setOperationAction(ISD::SHL, MVT::i64, Custom);
  setOperationAction(ISD::SRL, MVT::i64, Custom);
  setOperationAction(ISD::SRA, MVT::i64, Custom);
}

// BRJT qualification predicates E2/E3/E5 (design §3.2.6, rev3). This is the
// backend-independent qualification gate: with `-mcs251-jump-tables` on, upstream consults
// this hook once per candidate cluster (SwitchLoweringUtils.cpp:92 whole-range
// and :153 per partition). The base implementation must NOT be relied upon:
// its hard cap is short-circuited by OptForSize
// (`(OptForSize || Range <= MaxJumpTableSize) && ...`,
// TargetLoweringBase.cpp:1810) because shouldOptimizeForSize is true for any
// function with the optsize/minsize attribute, and max-jump-table-size
// defaults to UINT_MAX -- so under a size attribute the base check would let
// tables of ANY entry count through. The 8-bit index invariant below is a
// correctness predicate, not a heuristic, so this override deliberately
// omits the `OptForSize ||` arm (measured P9-C/P9-D probes: optsize/minsize
// bypass the base cap and crash br_jt selection on the unmodified backend).
//
// E3 semantics note (rev3): the Range argument is High-Low+1, i.e. the number
// of table entries including the DefaultMBB-filled holes
// (getJumpTableRange, SwitchLoweringUtils.cpp:24-34; buildJumpTable fills the
// gaps :218-228) -- not the case count.
//
// E4 (>= getMinimumJumpTableEntries cases) stays upstream: it is applied
// outside this hook at SwitchLoweringUtils.cpp:68-70/:181 and is not subject
// to the OptForSize short-circuit.
bool MCS251TargetLowering::isSuitableForJumpTable(
    const SwitchInst *SI, uint64_t NumCases, uint64_t Range,
    ProfileSummaryInfo *PSI, BlockFrequencyInfo *BFI) const {
  // E2: the dispatch sequence consumes an 8-bit index built from the JT
  // header's cond-Low subtraction, so the original condition must be an
  // integer scalar of 8/16/32 bits. Wider (i64) or non-integer conditions
  // would need libcalls or 64-bit sequences in the dispatch path -- refuse
  // them and the cluster falls back to the comparison chain.
  const auto *CondTy =
      dyn_cast<IntegerType>(SI->getCondition()->getType());
  if (!CondTy)
    return false;
  unsigned Width = CondTy->getBitWidth();
  if (Width != 8 && Width != 16 && Width != 32)
    return false;

  // E3: table-entry-count cap. The index travels through A (8-bit) after the
  // ×3 sequence `mov rT,rI; add rT,rT; add rT,rI` (§3.2.1): 3*idx must stay
  // <= 255, i.e. idx <= 85, i.e. Range (the entry count) <= 86. There is no
  // runtime check -- an out-of-range index would silently jump to a wrong
  // table entry, so this predicate is what keeps the sequence exact.
  if (Range > MaxJumpTableEntries)
    return false;

  // E5: density. The tier follows the optsize switch (10, or 40 for size
  // optimization) exactly like the base class -- density is a heuristic and
  // may vary with the size tier; only the Range cap above is a correctness
  // predicate and it must have no size-attribute bypass.
  const bool OptForSize = llvm::shouldOptimizeForSize(SI->getParent(), PSI, BFI);
  const unsigned MinDensity = getMinimumJumpTableDensity(OptForSize);
  return NumCases * 100 >= Range * MinDensity;
}

// BRJT dispatch lowering (design §3.2.1, opt-in via `-mcs251-jump-tables`).
// The upstream JT header has already produced the index in a virtual register
// (cond - Low, zero-extended/truncated to the JT register type) and guarded
// it with the SETUGT range branch; E2/E3 qualified the cluster, so the index
// is known to be <= 85 and 3*idx <= 255 fits the 8-bit accumulator exactly.
//
// The six-slot sequence (all additions go through the 0x73 hardware 16-bit
// sum -- there is no ADDC in this ISA and the software version would need a
// carry chain):
//
//   mov  rT, rI        ; MOV8rr
//   add  rT, rT        ; ADD8rr (two-operand accumulate): 2xidx
//   add  rT, rI        ; ADD8rr: 3xidx
//   mov  a, rT         ; MOV8a (Defs=[A])
//   mov  dptr, #jt     ; MOVDPTRri (0x90 hi lo, J16 field; Defs=[DPL,DPH])
//   jmp  @a+dptr       ; JMPIAD (0x73): PC <- bank:(DPTR+A) & 0xffff
//
// Every step after the x3 arithmetic is welded with Glue into one scheduling
// unit (the same device the MOVX channel uses, design §3.2.2.4): A/DPL/DPH
// are fixed reserved SFRs, the physical Defs/Uses pins already forbid
// reordering against their writers, and the glue additionally keeps the
// whole dispatch atomic against DAG-combiner parallelisation.
SDValue MCS251TargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Table = Op.getOperand(1);
  SDValue Index = Op.getOperand(2);
  int JTI = cast<JumpTableSDNode>(Table.getNode())->getIndex();

  // rI: the low byte of the JT header's index register. The qualifier (E3)
  // guarantees the value fits: idx <= 85, so no information is lost in the
  // subreg extraction.
  SDValue Idx = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Index);

  SDValue RT = SDValue(DAG.getMachineNode(MCS251::MOV8rr, DL, MVT::i8, Idx), 0);
  RT = SDValue(DAG.getMachineNode(MCS251::ADD8rr, DL, MVT::i8, {RT, RT}), 0);
  RT = SDValue(DAG.getMachineNode(MCS251::ADD8rr, DL, MVT::i8, {RT, Idx}), 0);

  SDVTList ChainGlue = DAG.getVTList(MVT::Other, MVT::Glue);
  SDNode *N = DAG.getMachineNode(MCS251::MOV8a, DL, ChainGlue, {RT, Chain});
  Chain = SDValue(N, 0);
  SDValue Glue = SDValue(N, 1);
  N = DAG.getMachineNode(MCS251::MOVDPTRri, DL, ChainGlue,
                         {DAG.getTargetJumpTable(JTI, MVT::i32), Chain, Glue});
  Chain = SDValue(N, 0);
  Glue = SDValue(N, 1);
  N = DAG.getMachineNode(MCS251::JMPIAD, DL, MVT::Other, {Chain, Glue});
  return SDValue(N, 0);
}

const char *MCS251TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case MCS251ISD::ERET:
    return "MCS251ISD::ERET";
  case MCS251ISD::RETI:
    return "MCS251ISD::RETI";
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

// Defined with the controlled bit-access lowering below (P09 section 2.4).
static SDValue tryLowerDirectBitBranch(SDValue Op, SelectionDAG &DAG,
                                       SDNode *StaleChainUser = nullptr);

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
  case ISD::VAARG:
    // G2 B-S2 (G2-VARIADIC-DESIGN-draft.md R3 §4.3.5/§4.3.6, frozen message
    // E): clang lowers va_arg itself -- the guard/load/advance chain of
    // §4.3.3(c) emitted by MCS251ABIInfo::EmitVAArg -- so no llvm.va_arg
    // needs to reach the backend.  A node that does is a hand-written-IR or
    // stale-compiler leak and fails closed here instead of the generic
    // "Cannot select".
    report_fatal_error("MCS251: llvm.va_arg is not supported; va_arg is "
                       "lowered by clang CodeGen (MCS251ABIInfo::EmitVAArg)");
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::VAEND:
    // va_end touches nothing: the {base,off} pair lives in the owner's frame
    // and the slot area is static storage.  Keep the chain.
    return Op.getOperand(0);
  case ISD::VACOPY:
    return LowerVACOPY(Op, DAG);
  case ISD::MUL:
    return LowerMul32(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::BR_JT:
    return LowerBR_JT(Op, DAG);
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
    // P09 section 2.4: `if (B)` / `if (!B)` is one JB/JNB bit test. The DAG
    // combiner reaches this case with EITHER polarity: a xor-inverted
    // condition is canonicalised into a swapped BRCOND(cond, false-block)
    // plus trailing BR(true-block), so the raw cond here can already be the
    // non-inverted sample. Try the direct bit-branch shape on the wrapped
    // BR_CC first -- tolerating this BRCOND itself as one stale chain user
    // of the sample, because the node is still live until the legalizer
    // replaces it with the returned JB/JNB -- and keep LowerBR_CC's own hook
    // for the BR_CC nodes the combiner produces directly. Anything that does
    // not trace back to a single glued bit sample falls through to the
    // generic compare path.
    SDLoc DL(Op);
    SDValue Cond = Op.getOperand(1);
    SDValue BRCC = DAG.getNode(
        ISD::BR_CC, DL, MVT::Other, Op.getOperand(0),
        DAG.getCondCode(ISD::SETNE), Cond,
        DAG.getConstant(0, DL, Cond.getValueType()), Op.getOperand(2));
    if (SDValue JB = tryLowerDirectBitBranch(BRCC, DAG,
                                             /*StaleChainUser=*/Op.getNode()))
      return JB;
    return LowerBR_CC(BRCC, DAG);
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
  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN: {
    // Both node shapes carry the intrinsic ID as operand 1 (operand 0 is the
    // chain). The set/clear/toggle intrinsics are chain-only writes; the read
    // intrinsic is an INTRINSIC_W_CHAIN, but its illegal i1 result is promoted
    // by the type legalizer (ReplaceNodeResults) before operation
    // legalization, so it never reaches here. Any other target intrinsic has
    // no lowering.
    unsigned IID = Op.getConstantOperandVal(1);
    switch (IID) {
    case Intrinsic::mcs251_bit_set:
    case Intrinsic::mcs251_bit_clear:
    case Intrinsic::mcs251_bit_toggle:
    case Intrinsic::mcs251_bit_obj_set:
    case Intrinsic::mcs251_bit_obj_clear:
    case Intrinsic::mcs251_bit_obj_toggle:
      return LowerBitIntrinsic(Op, DAG);
    case Intrinsic::mcs251_vararg_halt:
      // G2 B-S2 (G2-VARIADIC-DESIGN-draft.md §4.3.6): deterministic
      // dead-loop stop, encoded as `sjmp .` (80 FE).  Terminator + barrier,
      // so the halt arm's block can never fall through.
      return SDValue(DAG.getMachineNode(MCS251::VARARG_HALT, SDLoc(Op),
                                        MVT::Other, Op.getOperand(0)),
                     0);
    case Intrinsic::mcs251_tfpu_sin:
    case Intrinsic::mcs251_tfpu_cos:
    case Intrinsic::mcs251_tfpu_tan:
    case Intrinsic::mcs251_tfpu_atan:
    case Intrinsic::mcs251_tfpu_sqrt:
    case Intrinsic::mcs251_tfpu_add:
    case Intrinsic::mcs251_tfpu_sub:
    case Intrinsic::mcs251_tfpu_mul:
    case Intrinsic::mcs251_tfpu_div:
      // G7 S3 (G7-FLOAT-DESIGN-draft.md §2.4): the i32 bit-pattern
      // operands ride CopyToReg into the fixed DR4/DR0 window, the single
      // TFPU pseudo carries trigger+wait, and the result is read back from
      // DR4. All welded with Glue into one scheduling unit.
      return LowerTFPUIntrinsic(Op, DAG);
    default:
      report_fatal_error("MCS251: unsupported target intrinsic");
    }
  }
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
//
// P1-2 optnone consumers. MCS251LoweringPrep folds constants and erases dead
// wide/float computations for every function EXCEPT optnone ones, whose IR
// must reach instruction selection untouched. The read-only MCS251ContractCheck
// still passes those shapes (it judges them with MCS251LocalInterp), so
// instruction selection locally substitutes the two shapes that can appear
// in an optnone function:
//
//   1. A wide result that is provably unobservable: every user is a simple
//      (non-volatile, non-atomic) store into a stack slot that is never
//      read anywhere in the FUNCTION. Substituting any value (zero) is
//      sound.
//   2. A wide operation whose operands are provably constants parked in
//      stack slots by the -O0 frontend: a load whose stack slot has exactly
//      one storing user, that store has provably executed first, and the
//      stored value is a constant of the exact loaded width. Both proofs
//      are conservative; anything they cannot prove keeps the loud
//      rejection below (fail-closed, never a silent miscompile).
//
// P12-1 (Alice review round 3): a SelectionDAG covers exactly one basic
// block, so a slot-reading LOAD, CALL, PHI/select copy or address escape
// living in a successor block is invisible to any DAG-local scan -- the
// zero-substitution below then erased results that were still observable
// (crossblock_live: udiv 100/4 stored in one block, loaded in the next,
// returned 0 instead of 25). Every slot-shaped proof below is therefore
// FUNCTION-level: the frame index is mapped back to its IR alloca
// (FunctionLoweringInfo records it in MachineFrameInfo) and the verdict
// comes from the shared MCS251LocalInterp oracle over the whole function.
namespace {

// The IR alloca a frame index was created for, or null (spill slots and
// other synthesized objects carry none -- nothing can be proven there).
static const AllocaInst *mcs251FIAlloca(const SDNode *N, SelectionDAG &DAG) {
  auto *FI = dyn_cast<FrameIndexSDNode>(N);
  if (!FI)
    return nullptr;
  return DAG.getMachineFunction().getFrameInfo().getObjectAllocation(
      FI->getIndex());
}

// True when the slot is never read over the whole function: every user of
// its IR alloca is a plain store through the slot pointer, so any value
// stored there is unobservable. P12-1: the old DAG-local FI->uses() scan
// could not see readers in other blocks.
static bool mcs251FrameSlotIsNeverRead(const SDNode *Ptr,
                                       SelectionDAG &DAG) {
  const AllocaInst *AI = mcs251FIAlloca(Ptr, DAG);
  if (!AI)
    return false;
  return llvm::MCS251::allocaIsNeverRead(AI);
}

// True when every user of V is a simple store of V into a never-read stack
// slot (possibly after the type legalizer split it into truncating halves,
// which stay equally dead).
bool mcs251IsUnobservableWideResult(const SDValue V, SelectionDAG &DAG) {
  if (V.use_empty())
    return true;
  for (const SDUse &Use : V.getNode()->uses()) {
    if (Use.getResNo() != V.getResNo())
      continue; // consumes another result of a multi-result node
    const SDNode *U = Use.getUser();
    if (const auto *ST = dyn_cast<StoreSDNode>(U)) {
      if (ST->isVolatile() || ST->isAtomic() ||
          ST->getValue() != V)
        return false;
      const SDNode *Ptr = ST->getBasePtr().getNode();
      if (!isa<FrameIndexSDNode>(Ptr) ||
          !mcs251FrameSlotIsNeverRead(Ptr, DAG))
        return false;
      continue;
    }
    // The value-legalizer inserts plain truncates when it splits a wide
    // store; a truncate whose only role is feeding such a store keeps the
    // result dead.
    if (U->getOpcode() == ISD::TRUNCATE) {
      for (const SDUse &TUse : U->uses()) {
        const auto *ST = dyn_cast<StoreSDNode>(TUse.getUser());
        if (!ST || ST->isVolatile() || ST->isAtomic())
          return false;
        const SDNode *Ptr = ST->getBasePtr().getNode();
        if (!isa<FrameIndexSDNode>(Ptr) ||
            !mcs251FrameSlotIsNeverRead(Ptr, DAG))
          return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

// Resolve \p V to a ConstantSDNode/ConstantFPSDNode, or return a null
// SDValue. Recognizes extends, integer arithmetic over resolved values,
// BUILD_PAIR halves and loads of whole-function-proven constant stack
// slots (P12-5: slot loads are resolved exclusively through the shared
// getProvenSlotConstant oracle below -- there is deliberately no
// DAG-local fallback).
// The APInt/APFloat work is exact; divisions by zero and oversized shifts
// do not resolve (they are UB, and folding UB would be a silent verdict).
SDValue mcs251ResolveDAGConstant(SDValue V, SelectionDAG &DAG,
                                 unsigned Depth) {
  if (Depth > 64)
    return SDValue();
  if (auto *CN = dyn_cast<ConstantSDNode>(V.getNode()))
    if (V.getResNo() == 0 && !CN->isUndef())
      return V;
  if (auto *CF = dyn_cast<ConstantFPSDNode>(V.getNode()))
    if (V.getResNo() == 0)
      return V;

  SDLoc DL(V);
  EVT VT = V.getValueType();
  auto IntOf = [&](SDValue R) -> std::optional<APInt> {
    if (!R || !R.getValueType().isInteger() || R.getResNo() != 0)
      return std::nullopt;
    if (auto *CN = dyn_cast<ConstantSDNode>(R.getNode()))
      if (!CN->isUndef())
        return CN->getAPIntValue();
    return std::nullopt;
  };
  auto MakeInt = [&](const APInt &Val) -> SDValue {
    return DAG.getConstant(Val.trunc(VT.getSizeInBits()), DL, VT);
  };

  switch (V.getOpcode()) {
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    if (std::optional<APInt> C = IntOf(mcs251ResolveDAGConstant(
            V.getOperand(0), DAG, Depth + 1)))
      return MakeInt(C->zext(VT.getSizeInBits()));
    return SDValue();
  case ISD::SIGN_EXTEND:
    if (std::optional<APInt> C = IntOf(mcs251ResolveDAGConstant(
            V.getOperand(0), DAG, Depth + 1)))
      return MakeInt(C->sext(VT.getSizeInBits()));
    return SDValue();
  case ISD::TRUNCATE:
    if (std::optional<APInt> C = IntOf(mcs251ResolveDAGConstant(
            V.getOperand(0), DAG, Depth + 1)))
      return MakeInt(*C);
    return SDValue();
  case ISD::BUILD_PAIR: {
    std::optional<APInt> Lo = IntOf(
        mcs251ResolveDAGConstant(V.getOperand(0), DAG, Depth + 1));
    std::optional<APInt> Hi = IntOf(
        mcs251ResolveDAGConstant(V.getOperand(1), DAG, Depth + 1));
    if (!Lo || !Hi)
      return SDValue();
    unsigned Half = VT.getSizeInBits() / 2;
    APInt Combined = Hi->zext(VT.getSizeInBits()) << Half;
    Combined |= Lo->zext(VT.getSizeInBits());
    return MakeInt(Combined);
  }
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::UDIV:
  case ISD::SDIV:
  case ISD::UREM:
  case ISD::SREM:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA: {
    std::optional<APInt> L = IntOf(
        mcs251ResolveDAGConstant(V.getOperand(0), DAG, Depth + 1));
    std::optional<APInt> R = IntOf(
        mcs251ResolveDAGConstant(V.getOperand(1), DAG, Depth + 1));
    if (!L || !R)
      return SDValue();
    unsigned W = VT.getSizeInBits();
    APInt A = L->zext(W), B = R->zext(W);
    APInt Res(W, 0);
    switch (V.getOpcode()) {
    case ISD::ADD: Res = A + B; break;
    case ISD::SUB: Res = A - B; break;
    case ISD::MUL: Res = A * B; break;
    case ISD::UDIV:
      if (B.isZero())
        return SDValue();
      Res = A.udiv(B);
      break;
    case ISD::SDIV:
      if (B.isZero())
        return SDValue();
      Res = A.sdiv(B);
      break;
    case ISD::UREM:
      if (B.isZero())
        return SDValue();
      Res = A.urem(B);
      break;
    case ISD::SREM:
      if (B.isZero())
        return SDValue();
      Res = A.srem(B);
      break;
    case ISD::AND: Res = A & B; break;
    case ISD::OR:  Res = A | B; break;
    case ISD::XOR: Res = A ^ B; break;
    case ISD::SHL:
      if (B.uge(W))
        return SDValue();
      Res = A << B;
      break;
    case ISD::SRL:
      if (B.uge(W))
        return SDValue();
      Res = A.lshr(B);
      break;
    case ISD::SRA:
      if (B.uge(W))
        return SDValue();
      Res = A.ashr(B);
      break;
    default:
      llvm_unreachable("covered above");
    }
    return MakeInt(Res);
  }
  case ISD::LOAD: {
    const auto *LD = cast<LoadSDNode>(V.getNode());
    if (V.getResNo() != 0 || LD->isVolatile() || LD->isAtomic())
      return SDValue();
    // The base must be a stack slot, possibly reached through one constant
    // offset add (the value legalizer splits wide slots into FI+0/FI+4).
    int64_t LoadOff = 0;
    const SDNode *Base = LD->getBasePtr().getNode();
    if (Base->getOpcode() == ISD::ADD && Base->getNumOperands() == 2) {
      auto *C = dyn_cast<ConstantSDNode>(Base->getOperand(1).getNode());
      if (!C || Base->getOperand(0).getValueType() != MVT::i32)
        return SDValue();
      LoadOff = C->getSExtValue();
      Base = Base->getOperand(0).getNode();
    }
    auto *FI = dyn_cast<FrameIndexSDNode>(Base);
    if (!FI)
      return SDValue();
    const unsigned Width = LD->getMemoryVT().getFixedSizeInBits() / 8;
    const bool BigEndian = DAG.getDataLayout().isBigEndian();
    // P12-2: rebuild the raw memory image first, then apply the extending
    // load's own semantics when widening to the result type. SEXTLOAD
    // replicates the sign bit; ZEXTLOAD zero-fills; EXTLOAD leaves the upper
    // bits unspecified, for which zero is the canonical materialization.
    // Ignoring getExtensionType() here rebuilt `sext i8 -100` as +156 and
    // folded the i64 sdiv to 39 instead of -25.
    auto AsExtended = [&](APInt Raw) -> SDValue {
      unsigned ResW = VT.getSizeInBits();
      if (VT.isFloatingPoint()) {
        // Softened float loads surface here with their integer-promoted
        // type; a still-float VT takes the bit pattern via a bitcast.
        // Extending float loads do not exist on this target; fail closed
        // if one ever appears.
        if (Raw.getBitWidth() != ResW)
          return SDValue();
        return DAG.getBitcast(
            VT, DAG.getConstant(Raw, DL, MVT::getIntegerVT(ResW)));
      }
      if (Raw.getBitWidth() < ResW) {
        if (LD->getExtensionType() == ISD::SEXTLOAD)
          Raw = Raw.sext(ResW);
        else
          Raw = Raw.zext(ResW);
      }
      return DAG.getConstant(Raw, DL, VT);
    };
    // P12-1: function-level parked-constant proof. The parking store may
    // live in a block outside this DAG (-O0 parks constants in the entry
    // block and reloads them in successors), which no same-block chain walk
    // can observe; the shared oracle proves the single dominating constant
    // store over the whole function instead.
    if (const AllocaInst *AI = mcs251FIAlloca(FI, DAG)) {
      if (Constant *C = llvm::MCS251::getProvenSlotConstant(AI)) {
        APInt Bits;
        if (auto *CI = dyn_cast<ConstantInt>(C))
          Bits = CI->getValue();
        else if (auto *CF = dyn_cast<ConstantFP>(C))
          Bits = CF->getValueAPF().bitcastToAPInt();
        else
          return SDValue(); // aggregate/vector parked value: not handled
        const unsigned StoredBytes = Bits.getBitWidth() / 8;
        if (LoadOff >= 0 && LoadOff + (int64_t)Width <= (int64_t)StoredBytes) {
          APInt Raw(Width * 8, 0);
          for (unsigned J = 0; J < Width; ++J) {
            // Object byte at offset LoadOff+J, most significant object byte
            // first in the big-endian layout.
            unsigned StoredShift =
                8 * (BigEndian ? (StoredBytes - 1 - (LoadOff + J))
                               : (LoadOff + J));
            APInt Byte = Bits.lshr(StoredShift) & APInt(Bits.getBitWidth(), 0xff);
            Raw |= Byte.zext(Width * 8)
                   << (8 * (BigEndian ? (Width - 1 - J) : J));
          }
          return AsExtended(Raw);
        }
      }
    }
    // P12-5 (Alice review round 4): the former DAG-local fallback is
    // deleted. It scanned this DAG's FI->uses() and walked the chain past
    // calls to reconstruct slot bytes locally, but a SelectionDAG covers a
    // single basic block: an address escape stored in a predecessor
    // (save(&p) feeding a mutate() call) and any second store living in
    // another block were invisible to it, so it kept returning a constant
    // after the whole-function proof above had correctly returned null
    // (the "alias" counterexample: local verdict 100, proof verdict null).
    // The gate above is now the ONLY slot-constant path: if the oracle
    // cannot prove the loaded bytes over the whole function, the load is
    // left unresolved and the caller fails closed (loud rejection) rather
    // than substituting an unproven constant.
    return SDValue();
  }
  default:
    return SDValue();
  }
}

} // namespace

void MCS251TargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  EVT VT = N->getValueType(0);
  // Controlled bit read (BIT BT03): the i1 result is illegal, so the type
  // legalizer routes it here. Handle it before the f32/f64/i64 cases.
  if (N->getOpcode() == ISD::INTRINSIC_W_CHAIN && VT == MVT::i1)
    return ReplaceBitReadResults(N, Results, DAG);
  // G2 B-S2: a residual llvm.va_arg whose result type is illegal on this
  // target (f32/f64/i64) reaches the type legalizer instead of
  // LowerOperation; reject it with the same frozen message E so every
  // llvm.va_arg shape fails closed identically.
  if (N->getOpcode() == ISD::VAARG)
    report_fatal_error("MCS251: llvm.va_arg is not supported; va_arg is "
                       "lowered by clang CodeGen (MCS251ABIInfo::EmitVAArg)");
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
    // P1-2 optnone shape 2: the operands are constants parked in stack slots
    // by the -O0 frontend (MCS251LoweringPrep must not touch optnone IR).
    // Resolve them through the conservative DAG-local proof and retry the
    // shared folder.
    SmallVector<SDValue, 4> Resolved;
    bool AllResolved = true;
    for (const SDValue &Op : Ops) {
      SDValue R = mcs251ResolveDAGConstant(Op, DAG, 0);
      if (!R) {
        AllResolved = false;
        break;
      }
      Resolved.push_back(R);
    }
    if (AllResolved) {
      SDValue LocallyFolded = DAG.FoldConstantArithmetic(
          N->getOpcode(), DL, VT, Resolved, N->getFlags());
      if (LocallyFolded) {
        Results.push_back(LocallyFolded);
        return;
      }
    }
    // P1-2 optnone shape 1: a wide/float result whose only users are stores
    // into never-read stack slots is unobservable; any substitute value
    // (zero) preserves the program. This is the DAG-side twin of the
    // targeted DCE MCS251LoweringPrep performs for non-optnone functions.
    // P12-1: the never-read verdict is proven over the whole function, not
    // just this DAG, so readers in successor blocks keep the result live.
    if (mcs251IsUnobservableWideResult(SDValue(N, 0), DAG)) {
      if (VT == MVT::i64)
        Results.push_back(DAG.getConstant(0, DL, VT));
      else
        Results.push_back(DAG.getConstantFP(0.0, DL, VT));
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
  // A3 (RUNTIME-AS-PTR-DESIGN-A.md §3-A3; DESIGN.md B.1.1/B.2.1.2/D.4): the
  // approved runtime pointer plan creates a *restricted, equal-width* AS4<->32
  // bit AS0 conversion. This is an independent CODE branch placed before the
  // original RAM rules on purpose -- AS4 is deliberately NOT added to
  // IsFarRAM, because that set also participates in i32 equal-width
  // interconversion, i16->i32 extension and i32->i16 constant narrowing, and
  // joining it would silently open AS4<->AS3/AS9, Near->CODE and low-address
  // CODE constant narrowing, none of which are approved.
  //
  // AS4 and a 32-bit AS0 have the same 32/8 pointer representation and the
  // same canonical 24-bit effective address, and AS4 loads already read
  // through the unified DR channel (parseAddress), so the value passes
  // through unchanged: no tag, no lookup table, no truncation. The reverse
  // (AS0->AS4) explicit conversion is the same representation pass-through;
  // its non-null value is only a valid CODE pointer under the validity
  // contract, and this does not make an implicit AS0->AS4 assignment legal
  // (the frontend keeps rejecting that) nor authorize an AS4 store.
  auto IsCode = [](unsigned AS) { return AS == 4; };
  auto IsAS0 = [](unsigned AS) { return AS == 0; };
  if (SrcVT == MVT::i32 && DstVT == MVT::i32 &&
      ((IsAS0(SrcAS) && IsCode(DstAS)) ||
       (IsCode(SrcAS) && IsAS0(DstAS))))
    return Src;
  // Every other conversion involving AS4 -- i32->i16 (a 16-bit AS0 model
  // cannot hold the CODE bank), i16->i32, AS4<->AS1/AS2/AS3/AS8/AS9 -- stays
  // rejected in this slice. The message names CODE so this can never be
  // mistaken for an unassigned-space failure.
  if (IsCode(SrcAS) || IsCode(DstAS))
    report_fatal_error(
        "MCS251: unsupported address-space cast involving CODE (address "
        "space 4); only the equal-width 32-bit AS4<->AS0 conversion is "
        "permitted");
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

//===----------------------------------------------------------------------===//
//  Controlled bit-access intrinsics (BIT task BT03)
//===----------------------------------------------------------------------===//
//
// llvm.mcs251.bit.read/set/clear/toggle are the ONLY routes into the bit
// address space; ordinary addrspace(5) loads/stores stay fail-closed (see
// checkDataAddressSpace). The bit address is an immarg i32 constant in
// [0, 255]; ImmArg<0> makes the IR verifier reject a dynamic address before
// any backend code runs, and the range/constant checks below are the
// backend's own loud guard.
//
// Instruction mapping:
//   set    -> SETBBIT bit      clear -> CLRBIT bit     toggle -> CPLBIT bit
//   read   -> MOVCBIT bit ; materialise the carry into a byte
// Where the bit address names PSW.CY (0xd7) the flag-bearing C form is used
// instead (SETBC / CLRC / CPLC), so the virtual PSW register stays accurate
// (the bit forms do not declare a PSW def/use).
//
// toggle is deliberately a single CPL: it must NOT be split into a read
// followed by a write (that would be a non-atomic read-modify-write).
//
// read builds one atomic sample group that cannot be reordered by the
// scheduler:
//
//   mov c, bit    (MOVCBIT, reads the bit into CY; Defs=[PSW])
//   mov a, #0     (MOVAI)
//   rlc a         (RLCA: A = 0<<1 | CY = CY; Uses/Defs=[PSW,A])
//   mov <dst>, a  (MOV8ra: the i8 result, 0 or 1)
//
// The group is pinned with a Glue chain and the implicit PSW/A register
// dependencies, so no other instruction can be inserted between the sample
// and its materialisation.

// Validate the immarg bit-address constant of a bit intrinsic. A dynamic
// (non-constant) operand is impossible after ImmArg<0>, but this is the
// backend's own check and stays loud in release builds. The value is read
// zero-extended: the operand is emitted as an i16 target constant precisely
// so bit addresses >= 0x80 are not sign-extended back to a negative i8.
static unsigned getBitIntrinsicAddr(const SDNode *N, unsigned OpNo) {
  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(OpNo));
  if (!C)
    report_fatal_error("MCS251: bit intrinsic address must be a constant "
                       "immediate (dynamic bit addresses are not supported)");
  int64_t V = C->getZExtValue();
  if (V < 0 || V > 0xff)
    report_fatal_error("MCS251: bit intrinsic address " + Twine(V) +
                       " is out of range [0, 255]");
  return unsigned(V);
}

// The bit-address machine operand of an access, in either spelling (P09
// section 1.1): the fixed family carries an immarg i32 constant in [0, 255];
// the obj family carries the identity handle -- an AS0 GlobalAddress of an
// i8 GlobalVariable marked "mcs251-bit-object", at zero offset. The symbolic
// handle is routed through a target GlobalAddress so the normal GlobalAddress
// lowering never materialises it as a DR byte pointer; the bit number itself
// is resolved by the linker from R_MCS251_BITADDR8.
struct BitAccessOperand {
  unsigned Addr = 0;    // fixed family only; always [0, 255]
  bool Symbolic = false;
  SDValue MachineOp;    // target constant / target global address
};

static BitAccessOperand getBitIntrinsicOperand(SDNode *N, unsigned OpNo,
                                               SelectionDAG &DAG) {
  SDValue Op = N->getOperand(OpNo);
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Op.getNode())) {
    if (Op.getResNo() != 0)
      report_fatal_error("MCS251: symbolic bit intrinsic operand must be the "
                         "bit-object global itself");
    auto *GV = dyn_cast<GlobalVariable>(GA->getGlobal());
    // The IR contract verifier (MCS251ContractCheck) already guarantees the
    // direct marked-handle form; this is the backend's own loud guard, and it
    // also covers the MIR entry path that skips the IR verifier.
    if (!GV || GV->getAddressSpace() != 0 || !MCS251::isBitObjectGlobal(*GV))
      report_fatal_error("MCS251: symbolic bit intrinsic operand must be a "
                         "direct AS0 bit-object global");
    if (GA->getOffset() != 0)
      report_fatal_error("MCS251: bit object '" + GV->getName() +
                         "' bit-address operand must have no addend");
    SDLoc DL(Op);
    BitAccessOperand Res;
    Res.Symbolic = true;
    Res.MachineOp =
        DAG.getTargetGlobalAddress(GV, DL, MVT::i16, /*Offset=*/0);
    return Res;
  }
  unsigned BitAddr = getBitIntrinsicAddr(N, OpNo);
  SDLoc DL(Op);
  BitAccessOperand Res;
  Res.Addr = BitAddr;
  Res.MachineOp = DAG.getTargetConstant(BitAddr, DL, MVT::i16);
  return Res;
}

// The bit instruction for a set/clear/toggle request. Bit address 0xd7 is
// PSW.CY, so it routes to the flag-bearing C form (the bit-address form would
// not declare the PSW def/use the virtual flags register needs). A symbolic
// handle never names PSW.CY (RAM bits only, P09 section 1.1), so it always
// takes the bit-address form.
static unsigned getBitWriteOpcode(unsigned IID, const BitAccessOperand &Op) {
  const bool Carry = !Op.Symbolic && Op.Addr == 0xd7;
  switch (IID) {
  case Intrinsic::mcs251_bit_set:
  case Intrinsic::mcs251_bit_obj_set:
    return Carry ? MCS251::SETBC : MCS251::SETBBIT;
  case Intrinsic::mcs251_bit_clear:
  case Intrinsic::mcs251_bit_obj_clear:
    return Carry ? MCS251::CLRC : MCS251::CLRBIT;
  case Intrinsic::mcs251_bit_toggle:
  case Intrinsic::mcs251_bit_obj_toggle:
    return Carry ? MCS251::CPLC : MCS251::CPLBIT;
  default:
    llvm_unreachable("unexpected MCS251 bit intrinsic");
  }
}

SDValue MCS251TargetLowering::LowerBitIntrinsic(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDNode *N = Op.getNode();
  SDLoc DL(Op);
  SDValue Chain = N->getOperand(0);
  unsigned IID = N->getConstantOperandVal(1);
  BitAccessOperand Bit = getBitIntrinsicOperand(N, 2, DAG);
  unsigned Opc = getBitWriteOpcode(IID, Bit);
  // The C forms take no bit-address operand (the mnemonic names the carry).
  // The address is emitted as an i16 target constant so values >= 0x80 are
  // not sign-extended to a negative i8 immediate (see getBitIntrinsicAddr);
  // the symbolic handle is a target global address at offset zero.
  SmallVector<SDValue, 2> Ops;
  if (!(!Bit.Symbolic && Bit.Addr == 0xd7))
    Ops.push_back(Bit.MachineOp);
  Ops.push_back(Chain);
  return SDValue(DAG.getMachineNode(Opc, DL, MVT::Other, Ops), 0);
}

// Type-legalizer hook: the read intrinsic is an INTRINSIC_W_CHAIN whose i1
// result is illegal on this target (no i1 register class), so its first
// result is promoted. Build the whole sample group here and hand the generic
// promotion machinery a TRUNCATE(i1) of the i8 the group produces; the
// truncate is then promoted back to the i8 vreg, which already holds 0/1.
// Both families arrive here (P09 section 1.1): the fixed immarg constant and
// the symbolic bit-object handle share the machine path, the latter carried
// by the same target operand (global address, zero offset) that
// LowerBitIntrinsic emits for the writers.
void MCS251TargetLowering::ReplaceBitReadResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  assert(N->getOpcode() == ISD::INTRINSIC_W_CHAIN &&
         N->getValueType(0) == MVT::i1 &&
         "unexpected bit-read legalization request");
  SDLoc DL(N);
  SDValue Chain = N->getOperand(0);
  BitAccessOperand Bit = getBitIntrinsicOperand(N, 2, DAG);

  // mov c, bit -- sample the bit into CY. This is the side-effecting access:
  // it consumes and produces the memory chain.
  SDValue MovC(DAG.getMachineNode(
                   MCS251::MOVCBIT, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                   {Bit.MachineOp, Chain}),
               0);
  SDValue Sample = MovC.getValue(0);
  // mov a, #0 ; rlc a ; mov <dst>, a -- A = CY in {0, 1}.
  SDValue Zero(DAG.getMachineNode(MCS251::MOVAI, DL, MVT::Glue,
                                  {DAG.getTargetConstant(0, DL, MVT::i8),
                                   MovC.getValue(1)}),
               0);
  SDValue Rlc(DAG.getMachineNode(MCS251::RLCA, DL, MVT::Glue, {Zero}), 0);
  SDValue Byte(DAG.getMachineNode(MCS251::MOV8ra, DL, MVT::i8, {Rlc}), 0);

  // Hand back a TRUNCATE to i1 (same shape as PPC's i1 intrinsic promotion):
  // the type legalizer will then promote this truncate, and since i8 truncates
  // to i1 by keeping the low bit -- and Byte is already exactly 0 or 1 -- the
  // promoted value is Byte itself. The chain result is returned unchanged.
  Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Byte));
  Results.push_back(Sample);
}

//===----------------------------------------------------------------------===//
// TFPU coprocessor intrinsics (G7 S3, design §2.4)
//===----------------------------------------------------------------------===//
//
// The whole hardware sequence is ONE MachineInstr (the TFPU_<OP> pseudo)
// between two ordinary fixed-register copies:
//
//   copy  dr4, <AR vreg>          ; AR window load (mov dr4 is lane-exact
//   copy  dr0, <BR vreg>          ;  for the TFPU MSB-first figures, binary
//                                 ;  ops only)
//   TFPU_<OP>                     ; expanded post-RA: mov 0xED,#cmd ; NOPs
//   <dst vreg> = copy dr4         ; result readback
//
// Every step is welded with Glue into a single scheduling unit (the same
// device the MOVX channel uses): the pseudo's implicit Uses/Defs carry the
// physical window to the machine schedulers, and the glue additionally
// keeps the load/trigger/readback inseparable under DAG-combiner
// parallelisation. The implicit Defs cover the FULL R0-R7 bank window
// (the coprocessor owns it for the whole busy period), so RA relocates or
// spills anything live across the sequence out of those bytes; values that
// merely pass through (the two loads before, the readback after) are
// unaffected. The pseudo is expanded only after RA by MCS251TFPUExpand,
// which is why it stays one instruction here.
SDValue MCS251TargetLowering::LowerTFPUIntrinsic(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDNode *N = Op.getNode();
  SDLoc DL(Op);
  unsigned IID = N->getConstantOperandVal(1);
  unsigned Opc;
  bool Binary;
  switch (IID) {
  default:
    llvm_unreachable("not a TFPU intrinsic");
  case Intrinsic::mcs251_tfpu_sin:
    Opc = MCS251::TFPU_SIN; Binary = false; break;
  case Intrinsic::mcs251_tfpu_cos:
    Opc = MCS251::TFPU_COS; Binary = false; break;
  case Intrinsic::mcs251_tfpu_tan:
    Opc = MCS251::TFPU_TAN; Binary = false; break;
  case Intrinsic::mcs251_tfpu_atan:
    Opc = MCS251::TFPU_ATAN; Binary = false; break;
  case Intrinsic::mcs251_tfpu_sqrt:
    Opc = MCS251::TFPU_SQRT; Binary = false; break;
  case Intrinsic::mcs251_tfpu_add:
    Opc = MCS251::TFPU_ADD; Binary = true; break;
  case Intrinsic::mcs251_tfpu_sub:
    Opc = MCS251::TFPU_SUB; Binary = true; break;
  case Intrinsic::mcs251_tfpu_mul:
    Opc = MCS251::TFPU_MUL; Binary = true; break;
  case Intrinsic::mcs251_tfpu_div:
    Opc = MCS251::TFPU_DIV; Binary = true; break;
  }

  SDValue Chain = N->getOperand(0);
  // Park the AR (first operand) in GPR32Win -- dr12, the only allocatable
  // 32-bit register outside the PSW[4:3] selected R0-R7 window -- and load
  // the AR window with the dedicated TFPU_LD_AR pseudo, expanded post-RA
  // into `mov dr4, $ar`.
  //
  // This is deliberately NOT a CopyToReg to DR4. With CopyToReg the
  // allocator coalesced the operand vreg straight into dr4, the copy folded
  // away, and the byte gather that produces the value stayed pinned at its
  // definition site (function entry) -- the AR window could then be written
  // BEFORE the TPIN=0 bit write (G7 S3 blocker 1, measured at -O0 and -O2).
  // With the operand parked in dr12 the allocator cannot place the value in
  // the window at all, and the only write into r4-r7 is the pseudo's own
  // expansion, which is emitted inside the window.
  //
  // Both window loads carry Chain+Glue. The chain serialises them against
  // every other memory operation (the TPIN writes included, both
  // IntrHasSideEffects); the glue is what makes them one scheduling unit
  // with the command pseudo, and it is also what keeps the implicit DR4/DR0
  // defs of the loads alive for the command's implicit uses (InstrEmitter's
  // glue walk collects the implicit uses of glued users).
  SDVTList ChainGlue = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue AR =
      SDValue(DAG.getMachineNode(TargetOpcode::COPY_TO_REGCLASS, DL, MVT::i32,
                                 N->getOperand(2),
                                 DAG.getTargetConstant(
                                     MCS251::GPR32WinRegClassID, DL, MVT::i32)),
              0);
  SDNode *LDAR =
      DAG.getMachineNode(MCS251::TFPU_LD_AR, DL, ChainGlue, {AR, Chain});
  Chain = SDValue(LDAR, 0);
  SDValue Glue = SDValue(LDAR, 1);
  // BR (second operand) into dr0 for the binary four, through the same
  // parking class. DR0 and DR4 are physically disjoint windows, so the two
  // loads never conflict; the AR parking value is dead once its window load
  // has run, so the singleton parking class can serve a binary command
  // (the allocator spills/reloads the BR operand in dr12 if it must, and
  // that reload is outside the window by construction).
  if (Binary) {
    SDValue BR =
        SDValue(DAG.getMachineNode(TargetOpcode::COPY_TO_REGCLASS, DL,
                                   MVT::i32, N->getOperand(3),
                                   DAG.getTargetConstant(
                                       MCS251::GPR32WinRegClassID, DL,
                                       MVT::i32)),
                0);
    SDNode *LDBR =
        DAG.getMachineNode(MCS251::TFPU_LD_BR, DL, ChainGlue, {BR, Chain, Glue});
    Chain = SDValue(LDBR, 0);
    Glue = SDValue(LDBR, 1);
  }
  // The trigger plus the fixed wait, i.e. the middle of the window. The
  // chained loads above keep it strictly after both window loads.
  SDNode *T = DAG.getMachineNode(Opc, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                                 {Chain, Glue});
  // Result readback, still inside the glued unit -- and, like the operand
  // side, an explicit post-RA pseudo (TFPU_RD_AR) instead of getCopyFromReg.
  // The CopyFromReg form was blocker 1's readback half: the allocator
  // coalesced the result vreg straight into dr4, the COPY folded away, and
  // the first real read of the window lanes became the consumer (the
  // epilogue `mov dpl, r7`), which sits AFTER the TPIN=1 `clr 0x90`.
  // Measured on the reviewer's set->sin->clear->return shape at -O2:
  //
  //   270 x nop / clr 0x90 / mov dpl, r7 / ...
  //
  // TFPU_RD_AR defs an explicit GPR32Win vreg (dr12, outside the window)
  // and expands post-RA into `mov $dst, dr4`, emitted here -- after the
  // wait chain, before anything that could consume the result.
  SDNode *RD = DAG.getMachineNode(MCS251::TFPU_RD_AR, DL,
                                  DAG.getVTList(MVT::i32, MVT::Other, MVT::Glue),
                                  {SDValue(T, 0), SDValue(T, 1)});
  return DAG.getMergeValues({SDValue(RD, 0), SDValue(RD, 1)}, DL);
}

// P09 section 2.4: a DIRECT condition (`if (B)` / `if (!B)`) is exactly one
// JB/JNB bit test, not a materialised byte plus a second re-test of that
// byte. LowerBR_CC calls this before the generic compare path; both bit
// families share it.
//
// By the time operation legalization sees the branch, the generic BRCOND
// expansion has produced BR_CC(SETNE, LHS, 0) where LHS is the promoted i1
// sample: the type legalizer's zero-or-one mask AND(MOV8ra-byte, 1),
// optionally nested inside the `if (!B)` inversion XOR(..., 1). The condition
// is traced back to the exact glued sample group built by
// ReplaceBitReadResults:
//
//   mov c, bit -> mov a, #0 -> rlc a -> mov <byte>, a ; [xor byte, 1] ; and byte, 1
//
// The rewrite is only sound when the branch is the sample's ONLY value AND
// chain user: then replacing the branch with JB (consuming the chain that fed
// mov c) keeps the access exactly once, in place, with the same ordering
// relative to every other memory operation. Anything else falls back to the
// generic compare path (fail closed, never a duplicated access).
static SDValue tryLowerDirectBitBranch(SDValue Op, SelectionDAG &DAG,
                                       SDNode *StaleChainUser) {
  SDNode *N = Op.getNode();
  assert(Op.getOpcode() == ISD::BR_CC && "direct bit branch is a BR_CC shape");
  // The branch fires when CC(sample-masked-byte, 0) holds; with the byte
  // being exactly 0/1, only the two boolean tests are meaningful:
  // SETNE -> branch when the bit is 1 (JB), SETEQ -> when it is 0 (JNB).
  // (The DAG combiner canonicalises the test to SETEQ-with-zero, so both
  // spellings must be accepted.)
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
  bool Invert;
  if (CC == ISD::SETNE)
    Invert = false;
  else if (CC == ISD::SETEQ)
    Invert = true;
  else
    return SDValue();
  SDValue Chain = N->getOperand(0);
  SDValue Cond = N->getOperand(2); // LHS
  auto *RHS = dyn_cast<ConstantSDNode>(N->getOperand(3));
  if (!RHS || !RHS->isZero())
    return SDValue();
  SDValue Dest = N->getOperand(4);

  auto IsConstOne = [](SDValue V) {
    auto *C = dyn_cast<ConstantSDNode>(V);
    return C && C->getZExtValue() == 1;
  };
  auto IsConstZero = [](SDValue V) {
    auto *C = dyn_cast<ConstantSDNode>(V);
    return C && C->isZero();
  };
  auto IsConstAllOnes = [](SDValue V) {
    auto *C = dyn_cast<ConstantSDNode>(V);
    return C && C->isAllOnes();
  };
  // The i1 zero-or-one mask `and x, 1` (constant canonicalised to the RHS),
  // inserted both by i1 promotion and by the generic BRCOND->BR_CC rewrite.
  auto StripMask = [&](SDValue V) {
    if (V.getOpcode() == ISD::AND && IsConstOne(V.getOperand(1)))
      return V.getOperand(0);
    return V;
  };

  // Peel the outer mask, then an optional `if (!B)` inversion (xor with 1,
  // or a logical NOT's all-ones xor), then the promotion mask again. The
  // inversion toggles the SETNE/SETEQ polarity above.
  Cond = StripMask(Cond);
  if (!Cond.getNode()->hasNUsesOfValue(1, Cond.getResNo()))
    return SDValue();
  if (Cond.getOpcode() == ISD::XOR &&
      (IsConstOne(Cond.getOperand(1)) || IsConstAllOnes(Cond.getOperand(1)))) {
    if (!Cond.getNode()->hasNUsesOfValue(1, Cond.getResNo()))
      return SDValue();
    Invert = !Invert;
    Cond = StripMask(Cond.getOperand(0));
  }
  if (!Cond.getNode()->hasNUsesOfValue(1, Cond.getResNo()))
    return SDValue();

  // Trace the glued sample group: MOV8ra <- RLCA <- MOVAI#0 <- MOVCBIT.
  auto Mov8ra = dyn_cast<MachineSDNode>(Cond.getNode());
  if (!Mov8ra || Cond.getResNo() != 0 ||
      Mov8ra->getMachineOpcode() != MCS251::MOV8ra ||
      Mov8ra->getNumOperands() != 1)
    return SDValue();
  auto Rlc = dyn_cast<MachineSDNode>(Mov8ra->getOperand(0).getNode());
  if (!Rlc || Mov8ra->getOperand(0).getResNo() != 0 ||
      Rlc->getMachineOpcode() != MCS251::RLCA || Rlc->getNumOperands() != 1)
    return SDValue();
  auto Zero = dyn_cast<MachineSDNode>(Rlc->getOperand(0).getNode());
  if (!Zero || Rlc->getOperand(0).getResNo() != 0 ||
      Zero->getMachineOpcode() != MCS251::MOVAI || Zero->getNumOperands() != 2)
    return SDValue();
  if (!IsConstZero(Zero->getOperand(0)))
    return SDValue();
  auto MovC = dyn_cast<MachineSDNode>(Zero->getOperand(1).getNode());
  if (!MovC || Zero->getOperand(1).getResNo() != 1 ||
      MovC->getMachineOpcode() != MCS251::MOVCBIT || MovC->getNumOperands() != 2)
    return SDValue();

  // The branch must consume the sample's chain result directly: no other
  // memory operation may sit between the sample and its test (section 1.2:
  // no reordering across ordered memory accesses), and no other user may
  // depend on the sample's chain or byte, or the access would be duplicated.
  if (Chain != SDValue(MovC, 0))
    return SDValue();
  // The sample's chain result may have exactly TWO users while this runs: the
  // BR_CC being lowered and -- when called from the BRCOND custom lowering --
  // that BRCOND itself, which is still live until the legalizer swaps it for
  // the JB/JNB returned here and whose chain use disappears with it. Any
  // OTHER chain user (a call, a store, a second branch) means the access
  // would be duplicated or reordered; fail closed.
  for (const SDUse &U : MovC->uses()) {
    if (U.getResNo() != 0)
      continue; // the glue consumer is the sample group itself
    SDNode *U2 = const_cast<SDNode *>(U.getUser());
    if (U2 == N || U2 == StaleChainUser)
      continue;
    return SDValue();
  }

  SDLoc DL(N);
  unsigned BrOpc = Invert ? MCS251::JNB : MCS251::JB;
  // Operand 0 is the branch target, operand 1 the bit address (the exact
  // target constant / bit-object handle of the sampled mov c); the trailing
  // chain takes the place of the removed sample and keeps the branch ordered
  // against everything that preceded it.
  return SDValue(DAG.getMachineNode(BrOpc, DL, MVT::Other,
                                    {Dest, MovC->getOperand(0),
                                     MovC->getOperand(1)}),
                 0);
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

// f32 has no native register class. Before type legalization it can still
// appear at a direct C ABI boundary; its binary32 payload always occupies the
// same four lanes as i32. After generic softening it is already i32, so this
// helper is intentionally idempotent for the libcall path.
static bool usesI32ABI(EVT VT) { return VT == MVT::i32 || VT == MVT::f32; }

static SDValue asI32ABIValue(SDValue Value, const SDLoc &DL, SelectionDAG &DAG) {
  return Value.getValueType() == MVT::f32 ? DAG.getBitcast(MVT::i32, Value)
                                           : Value;
}

static SDValue fromI32ABIValue(SDValue Value, EVT VT, const SDLoc &DL,
                               SelectionDAG &DAG) {
  return VT == MVT::f32 ? DAG.getBitcast(MVT::f32, Value) : Value;
}

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
  // P09 section 2.4: a direct bit condition is one JB/JNB. Try the
  // sample-trace rewrite first; anything that is not exactly a single glued
  // controlled bit sample falls through to the generic compare path.
  if (SDValue JB = tryLowerDirectBitBranch(Op, DAG, /*StaleChainUser=*/nullptr))
    return JB;
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
  // behind it below), and nothing may ever be scheduled between them.  This
  // keeps the expansion compact in the overwhelmingly common case, but it is
  // NOT a correctness invariant: MachineBlockPlacement is free to displace
  // SkipMBB (observed in practice once a function grows enough blocks), and
  // a displaced skip makes the rel8 out of range.  The post-layout
  // MCS251BranchRelaxation pass (added in addPreEmitPass, after the layout
  // is final) rewrites any branch that no longer reaches -- it parses the
  // three terminator shapes this backend emits directly, since the generic
  // analyzeBranch/insertBranch/removeBranch hooks are deliberately not
  // implemented -- so a displaced skip costs one extra relaxation, never a
  // compile failure.
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

// DF0 P0-A/P0-B, narrowed by X2. AS3 (XDATA) and AS4 (CODE) *data* access are
// now implemented -- AS3 through the MOVX @DPTR channel, AS4 loads through the
// 24-bit DR unified-space channel (`mov r,@dr`) -- so the fail-closed set is
// exactly: AS4 *stores* (CODE is read-only by the space's definition), AS5
// (bit space; the controlled-bit lvalue mechanism is the only route), AS7
// (reserved) and every unassigned AS number. AS4 *function* addresses
// (calls/returns) are a separate capability and must not be caught here.
//
// Allowed data access AS set: {0,1,2,3,4,6,8,9}. AS0 default RAM, AS1/2/8
// near RAM, AS9 far RAM share the generic DR lowering; AS3/AS4 are split off
// to their own channels in LowerLoad/LowerStore. This mirrors the implemented
// Shizuku Tiny/XTiny lowering and is NOT a blanket "number allocated" pass.
static void checkDataAddressSpace(unsigned AS, bool IsStore) {
  if (AS == 4 && IsStore)
    report_fatal_error("MCS251: store to CODE (address space 4) is not "
                       "permitted; CODE is read-only");
  if (AS == 5)
    report_fatal_error("MCS251: address space 5 (bit space) data access is not "
                       "supported; use the controlled-bit lvalue mechanism "
                       "instead");
  if (AS == 7)
    report_fatal_error("MCS251: address space 7 is reserved and its data "
                       "access is not yet implemented");
  // AS0/1/2/6/8/9 are the generic data-access set, AS3 the XDATA (MOVX)
  // channel and AS4 the CODE (DR read) channel; anything else is unassigned
  // and must not fall back to a DataLayout p0 default.
  static const unsigned Implemented[] = {0, 1, 2, 3, 4, 6, 8, 9};
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
             (AddressSpace != 0 && AddressSpace != 4 && AddressSpace != 9)) {
    // AS4 (CODE) joins the far set in X2. Channel ruling: AS4 loads use the
    // 24-bit DR unified-space read (`mov r,@dr`, L80558) rather than MOVC
    // @A+DPTR -- the G144K246 manual separates `code` (classic FF: 16-bit
    // window) from `ecode` (80:0000~FF:FFFF, 24-bit), and the DR read covers
    // both plus the EEPROM FE: mapping (L125052 explicitly forbids MOVC for
    // EEPROM) with one sequence over the frozen 32-bit p4 pointer. AS3
    // (XDATA) is deliberately NOT classified here -- it has its own MOVX
    // @DPTR builder (buildXDATAAddress below) with its per-byte DPXL
    // re-pointing discipline; reaching the generic far path would bypass it.
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

//===----------------------------------------------------------------------===//
//  XDATA (AS3) MOVX @DPTR lowering with full 24-bit addresses (X2-1)
//===----------------------------------------------------------------------===//
//
// X2-1 full 24-bit ruling (supersedes the phase-1 16-bit window): the frozen
// AS3 contract (DESIGN.md B.2, `__xdata` 32/8, "保持完整 24 位有效地址") is
// implemented literally.  Every AS3 access byte re-points the MOVX region
// register DPXL (SFR 0x84, Intel 251 manual 3.3.2.2) from the canonical
// address bits [23:16] immediately before the MOVX @DPTR over the low 16
// bits:
//
//   mov  rN, #bank      ; constant bank: folded at compile time
//   mov  0x84, rN       ; MOV8dpxl -- region before EVERY access byte
//   mov  dpl/dph, ...   ; window offset, low 16 address bits
//   movx a,@dptr / movx @dptr,a
//
// The sequence is self-healing: no DPXL value is ever assumed to survive a
// previous access, an ISR, a call or a user SFR write, so the phase-1 "DPXL
// is always 01h" placement assumption is gone.  Never-assumes is one half of
// the X2-4 retention protocol only: the frames the backend generates for
// async entries PRESERVE DPXL (ISR_PUSH/POP_DPX read/write it), while
// ordinary calls may clobber it freely (the next access re-points it) and
// the CRT has no initial-value obligation.  The DPL/DPH/DPXL/A
// implicit-def/use pins order the sequence against the MIR-level passes;
// against the SelectionDAG scheduler (which cannot see MCInstrDesc implicit
// operands and faces TokenFactor-parallel access chains at -O1+) each byte
// sequence is welded with Glue in buildMOVXByteLoad/Store below -- the X2
// fix for the X4 e2e defect where the lane writes streamed by register
// across parallelised accesses and every MOVX hit the LAST written pointer.
// The retention protocol
// (CRT/ISR/call boundaries, user SFR 0x84 writes) is documented in the
// XDATA-CODE design supplement
// (validation/mcs251-models/proposals/XDATA-CODE-DESIGN-SUPPLEMENT.md); a
// user's own AS6 write to 0x84 inside a sequence window is a documented
// undefined interaction -- the backend never interleaves foreign code into a
// generated sequence.
//
// The phase-1 constant-window rejection is gone: constant banks fold at
// compile time, and bank==01h is still emitted (the local-omission
// optimisation is deliberately deferred to a future pass with whole-function
// analysis; per-access cost is +2 instructions constant, +2~3 dynamic).
// MXAX sequences (@Ri + P2 + page register) remain out of scope, matching
// the manual's own guidance against pdata-style access.

// Materialise an i8 constant (constant XDATA bank bytes, store value bytes);
// defined with the store helpers below.
static SDValue buildMOV8ri(uint64_t Imm, const SDLoc &DL, SelectionDAG &DAG);

static void splitXDATAAddress(SDValue Full, const SDLoc &DL,
                              SelectionDAG &DAG, SDValue &Bank,
                              SDValue &Addr16) {
  // bits [15:0] drive DPTR, bits [23:16] drive DPXL.  Bits [31:24] of the
  // canonical i32 container are ignored: pollution there cannot change the
  // access.
  Addr16 = DAG.getTargetExtractSubreg(MCS251::sub_lo16, DL, MVT::i16, Full);
  SDValue Hi =
      DAG.getTargetExtractSubreg(MCS251::sub_hi16, DL, MVT::i16, Full);
  Bank = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Hi);
}

// Build (bank, window address) for one AS3 access byte at base+ByteOff.
static void buildXDATAAddress(SDValue Ptr, int64_t ByteOff, const SDLoc &DL,
                              SelectionDAG &DAG, SDValue &Bank,
                              SDValue &Addr16) {
  if (Ptr.getValueType() != MVT::i32)
    report_fatal_error("MCS251: XDATA (address space 3) pointers are 32-bit");
  int64_t Off = ByteOff;
  // Peel constant GEP offsets; a register+register add stays in the 32-bit
  // pointer value and keeps its carry into the bank byte.
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
    break;
  }
  if (isa<FrameIndexSDNode>(Ptr))
    report_fatal_error("MCS251: XDATA access through a frame object is not "
                       "supported (allocas live in address space 0)");
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Ptr)) {
    // Full canonical address through the byte-of-24 relocation channel --
    // the same shape as the AS9 far globals (pointer32.ll pins the form).
    // The bank byte is split off at runtime, so the sequence is independent
    // of where the linker places the xdata object (X3 ruling 1: never cut
    // an XSEG address down to its low 16 bits).
    SDValue Base(DAG.getMachineNode(
                     MCS251::MOVADDR32, DL, MVT::i32,
                     DAG.getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i32,
                                                GA->getOffset() + Off)),
                 0);
    splitXDATAAddress(Base, DL, DAG, Bank, Addr16);
    return;
  }
  if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Ptr)) {
    SDValue Base(DAG.getMachineNode(
                     MCS251::MOVADDR32, DL, MVT::i32,
                     DAG.getTargetExternalSymbol(ES->getSymbol(), MVT::i32)),
                 0);
    splitXDATAAddress(foldDispIntoBase(Base, Off, DL, DAG), DL, DAG, Bank,
                      Addr16);
    return;
  }
  if (isa<ConstantPoolSDNode>(Ptr) || isa<JumpTableSDNode>(Ptr) ||
      isa<BlockAddressSDNode>(Ptr))
    report_fatal_error(
        "MCS251: XDATA constant-pool/jump-table addresses are not supported");
  if (auto *C = dyn_cast<ConstantSDNode>(Ptr)) {
    // Constant canonical address: bank and window both fold at compile
    // time.  The i32 container wraps at 2^32; bits [23:0] are the address.
    uint64_t K = (C->getZExtValue() + Off) & 0xffffffffULL;
    Bank = buildMOV8ri((K >> 16) & 0xff, DL, DAG);
    Addr16 = buildMOV16ri(K & 0xffff, DL, DAG);
    return;
  }
  // Runtime pointer: fold any remaining constant offset in full 32-bit
  // arithmetic (never an i16 add -- the carry belongs in the bank byte),
  // then split bank/window at runtime.
  SDValue Full = Off ? foldDispIntoBase(Ptr, Off, DL, DAG) : Ptr;
  splitXDATAAddress(Full, DL, DAG, Bank, Addr16);
}

// One MOVX byte load: re-point DPXL to the byte's bank, set dptr from the
// 16-bit window address, movx a,@dptr, then move the byte out of A into its
// virtual register. The chain is threaded through every setup move and the
// DPL/DPH/A/DPXL pins on the instruction descriptors keep the physical-
// register dependencies explicit. The loaded byte reaches its consumer
// through A (MOV8ra), never through the node's i8 result -- which therefore
// stays unused and would let selection dead-strip the node when it rewrites
// the chain. The bit-read sequence (ReplaceBitReadResults) solves the
// identical problem for its A-valued RRCA/MOV8ra tail with a Glue edge; the
// same Glue result rides MOVXALD into MOV8ra here, pinning the access into
// the selected stream.
//
// X2-fix (X4 e2e defect, -O1+ miscompile): the Glue chain must cover the
// WHOLE byte sequence, not just the MOV8ra tail. At -O1+ the DAG combiner
// legitimately parallelises disjoint non-volatile memory ops
// (parallelizeChainedStores / FindBetterChain), so the chain inputs of two
// AS3 accesses in one block become TokenFactor-parallel; the DPL/DPH/DPXL/A
// pins live only in the MCInstrDesc implicit operand lists, and the
// SelectionDAG schedulers do not see those (AddSchedEdges only follows
// SDValue operands), so the pre-RA list scheduler streamed the lane writes
// by register across the parallel accesses and every MOVX addressed through
// the LAST written pointer. Glue merges one access into a single scheduling
// unit; independent accesses stay separate units and may still be
// inter-ordered as wholes. The glue chain also feeds InstrEmitter's
// setPhysRegsDeadExcept scan (the glued MOVX implicit uses count as uses),
// so the lane writes' implicit defs are emitted live instead of dead and
// the pre/post-RA machine schedulers keep the real physical-register
// dependencies.
static SDValue buildMOVXByteLoad(SDValue Bank, SDValue Addr16,
                                 const SDLoc &DL, SelectionDAG &DAG,
                                 SDValue Chain, MachineMemOperand *MMO,
                                 SDValue &Byte) {
  // Self-healing region switch first: DPXL always matches this byte's bank,
  // whatever any earlier access, ISR or user write left behind.
  SDVTList ChainGlue = DAG.getVTList(MVT::Other, MVT::Glue);
  SDNode *N =
      DAG.getMachineNode(MCS251::MOV8dpxl, DL, ChainGlue, {Bank, Chain});
  Chain = SDValue(N, 0);
  SDValue Glue = SDValue(N, 1);
  SDValue Lo = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Addr16);
  SDValue Hi = DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8, Addr16);
  N = DAG.getMachineNode(MCS251::MOV8dpl, DL, ChainGlue, {Lo, Chain, Glue});
  Chain = SDValue(N, 0);
  Glue = SDValue(N, 1);
  N = DAG.getMachineNode(MCS251::MOV8dph, DL, ChainGlue, {Hi, Chain, Glue});
  Chain = SDValue(N, 0);
  Glue = SDValue(N, 1);
  SDVTList ResTys = DAG.getVTList(MVT::i8, MVT::Other, MVT::Glue);
  N = DAG.getMachineNode(MCS251::MOVXALD, DL, ResTys, {Chain, Glue});
  DAG.setNodeMemRefs(cast<MachineSDNode>(N), {MMO});
  Byte = SDValue(
      DAG.getMachineNode(MCS251::MOV8ra, DL, MVT::i8, SDValue(N, 2)), 0);
  return SDValue(N, 1);
}

// One MOVX byte store: re-point DPXL, set dptr, move the value byte into A,
// movx @dptr,a. The chain is threaded through every setup move, mirroring
// the load side, and the same X2-fix Glue chain (see buildMOVXByteLoad)
// welds the five nodes into one scheduling unit so the MOVX always follows
// its own lane writes and its own A value, however the DAG combiner
// parallelised the surrounding stores.
static SDValue buildMOVXByteStore(SDValue Bank, SDValue Addr16, SDValue Val,
                                  const SDLoc &DL, SelectionDAG &DAG,
                                  SDValue Chain, MachineMemOperand *MMO) {
  SDVTList ChainGlue = DAG.getVTList(MVT::Other, MVT::Glue);
  SDNode *N =
      DAG.getMachineNode(MCS251::MOV8dpxl, DL, ChainGlue, {Bank, Chain});
  Chain = SDValue(N, 0);
  SDValue Glue = SDValue(N, 1);
  SDValue Lo = DAG.getTargetExtractSubreg(MCS251::sub_lo8, DL, MVT::i8, Addr16);
  SDValue Hi = DAG.getTargetExtractSubreg(MCS251::sub_hi8, DL, MVT::i8, Addr16);
  N = DAG.getMachineNode(MCS251::MOV8dpl, DL, ChainGlue, {Lo, Chain, Glue});
  Chain = SDValue(N, 0);
  Glue = SDValue(N, 1);
  N = DAG.getMachineNode(MCS251::MOV8dph, DL, ChainGlue, {Hi, Chain, Glue});
  Chain = SDValue(N, 0);
  Glue = SDValue(N, 1);
  N = DAG.getMachineNode(MCS251::MOV8a, DL, ChainGlue, {Val, Chain, Glue});
  Chain = SDValue(N, 0);
  Glue = SDValue(N, 1);
  N = DAG.getMachineNode(MCS251::MOVXAST, DL, MVT::Other, {Chain, Glue});
  DAG.setNodeMemRefs(cast<MachineSDNode>(N), {MMO});
  return SDValue(N, 0);
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

  // DF0 P0-B (narrowed by X2): reject the still-unimplemented data-load AS
  // set (AS5/AS7/unassigned; AS4 loads are now served by the DR channel)
  // before parseAddress classifies the pointer value. parseAddress keeps its
  // own width/AS guard as a second line of defence; the check here gives a
  // use-specific diagnostic.
  checkDataAddressSpace(LD->getAddressSpace(), /*IsStore=*/false);

  unsigned Size = MemVT.getSizeInBits() / 8;
  // X2-1: AS3 (XDATA) lowers through the MOVX @DPTR channel, one byte at a
  // time, each byte with its own DPXL bank byte and 16-bit window address
  // (see the full 24-bit ruling above buildXDATAAddress).
  if (LD->getAddressSpace() == 3) {
    SDValue Chain = LD->getChain();
    SmallVector<SDValue, 4> Bytes;
    for (unsigned I = 0; I < Size; ++I) {
      SDValue Bank, Addr16;
      buildXDATAAddress(LD->getBasePtr(), I, DL, DAG, Bank, Addr16);
      auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
          LD->getMemOperand(), I, /*Size=*/1);
      SDValue Byte;
      Chain = buildMOVXByteLoad(Bank, Addr16, DL, DAG, Chain, MMO, Byte);
      Bytes.push_back(Byte);
    }
    // Same big-endian object layout and extension forwarding as the generic
    // path below: mem[base] is the HIGH byte of the object.
    SDValue Res = Bytes[0];
    if (Size >= 2)
      Res = makeWord(Bytes[0], Bytes[1], DL, DAG);
    if (Size == 4)
      Res = makeDR(Res, makeWord(Bytes[2], Bytes[3], DL, DAG), DL, DAG);
    if (ValVT != MemVT)
      Res = DAG.getNode(LD->getExtensionType() == ISD::SEXTLOAD
                            ? ISD::SIGN_EXTEND
                            : ISD::ZERO_EXTEND,
                        DL, ValVT, Res);
    return DAG.getMergeValues({Res, Chain}, DL);
  }
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

// Materialise an i8 constant value into a GPR8 vreg for the store forms and
// for constant XDATA bank bytes (buildXDATAAddress).
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

  // DF0 P0-A (narrowed by X2): CODE (AS4) stores stay forbidden and the
  // remaining unimplemented non-zero AS data stores are still rejected,
  // before parseAddress touches the pointer value.
  checkDataAddressSpace(ST->getAddressSpace(), /*IsStore=*/true);

  unsigned Size = MemVT.getSizeInBits() / 8;

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
  // X2-1: AS3 (XDATA) stores go through the MOVX @DPTR channel (the write
  // half of the same byte-at-a-time sequence as the load side above, each
  // byte re-pointing DPXL first). The address classification is the XDATA
  // builder's; parseAddress must never see AS3.
  if (ST->getAddressSpace() == 3) {
    for (unsigned I = 0; I < Size; ++I) {
      SDValue Bank, Addr16;
      buildXDATAAddress(ST->getBasePtr(), I, DL, DAG, Bank, Addr16);
      auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
          ST->getMemOperand(), I, /*Size=*/1);
      Chain = buildMOVXByteStore(Bank, Addr16, Bytes[I], DL, DAG, Chain, MMO);
    }
    return Chain;
  }
  MCS251Address A = parseAddress(ST->getBasePtr(), DL, DAG,
                                 /*AllowDirect=*/MemVT == MVT::i8,
                                 ST->getAddressSpace(), Size);
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

  // ISR campaign T05 step 11: a stackrestore would rewind SPX across the 37B
  // save area and the fixed local frame, so RETI (or the inverse restore)
  // would pop object bytes as the interrupt frame. Capability rejection.
  if (DAG.getMachineFunction().getFunction().getCallingConv() ==
      CallingConv::MCS251_INTR)
    report_fatal_error("MCS251 ISR: llvm.stackrestore is not supported in an "
                       "interrupt entry");

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

  // ISR campaign T05 step 11: the ISR body only implements the A6 fixed
  // frame. A dynamic alloca would move SPX between the 37B save area and
  // the epilogue's inverse restore, which that frame cannot express. This
  // is a backend capability rejection, not a restored A/B safety check.
  if (DAG.getMachineFunction().getFunction().getCallingConv() ==
      CallingConv::MCS251_INTR)
    report_fatal_error("MCS251 ISR: dynamic stack allocation is not supported "
                       "in an interrupt entry");

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
  // Float softening presents f32 as its IEEE-754 i32 bit pattern at this ABI
  // boundary. True f64 is rejected by the MCS251 contract verifier before
  // lowering and must never arrive here.
  if (!Ty->isIntegerTy(8) && !Ty->isIntegerTy(16) && !Ty->isIntegerTy(32) &&
      !Ty->isFloatTy())
    report_fatal_error("MCS251: arguments must be unsplit i8/i16/i32 scalars");
}

template <typename ArgT>
static void checkParameter(const ArgT &Arg, unsigned Index,
                           bool AllowStaticPointers) {
  // f32 libcalls are softened to i32 but retain f32 as ArgVT for ABI metadata.
  // It is the sole non-identical ArgVT/VT pair accepted by this backend.
  const bool IsSoftenedF32 = Arg.ArgVT == MVT::f32 && Arg.VT == MVT::i32;
  if ((Arg.VT != MVT::i8 && Arg.VT != MVT::i16 && Arg.VT != MVT::i32) ||
      (!IsSoftenedF32 && Arg.ArgVT != Arg.VT) || Arg.PartOffset ||
      Arg.Flags.isSplit() || Arg.Flags.isByVal() || Arg.Flags.isByRef() ||
      Arg.Flags.isSRet() || Arg.Flags.isInAlloca() || Arg.Flags.isNest())
    report_fatal_error("MCS251: arguments must be unsplit i8/i16/i32 scalars");
  if (Index && Arg.Flags.isPointer() && !AllowStaticPointers)
    report_fatal_error("MCS251: static pointer parameters are not supported "
                       "by the compatibility ABI");
}

//===----------------------------------------------------------------------===//
// Variadic lowering (G2 B-S2, G2-VARIADIC-DESIGN-draft.md R3 §4.3.4)
//===----------------------------------------------------------------------===//
//
// va_start anchors the 8-byte va_list pair on THIS function's first
// continuation slot:
//
//   __base = &slot _<this>_PARM_(F+1)   F = arg_size() (>= 1)
//   __off  = 0
//
// F=1 (printf) anchors on _PARM_2, F=2 (sprintf) on _PARM_3 -- the same
// slots the static-slot runtime already consumes.  F=0 was fail-closed in
// LowerFormalArguments: the register-channel argument has no slot to
// anchor on.
//
// The base is a slot-symbol ADDRESS AS A VALUE.  A bare ExternalSymbol
// node is only selectable as a load/store base; as a value it must be
// materialised through the same MOVADDR32/target-symbol path the
// GlobalAddress custom lowering and getAddressingUse use for symbol
// addresses (LowerOperation ISD::GlobalAddress case).
//
// Backend shape gate (G2 B-S2 review fix, Alice blocker 1, backend half):
// LowerVASTART/VACOPY write the pair unconditionally -- a PtrVT-wide base
// at +0 and a 4-byte i32 offset at +4 -- so a va_start/va_copy aimed at a
// narrower object (e.g. the 6-byte {ptr, i16} an unpinned +int16 offset
// field builds) would write up to 2 bytes past the object end and llc
// would still exit 0.  clang pins the pair shape at ASTContext
// construction (32-bit offset field, ASTContext ::buildVAList); this is
// the fail-closed second gate on the IR that actually reaches codegen.
// The VASTART/VACOPY nodes carry their IR pointer operand as an SrcValue
// operand, so whenever the object is countable (an alloca or a global)
// its exact shape is verified against the frozen two-field {ptr, i32}
// 8-byte pair (field 0 may be a pointer of any address space).  An
// opaque pointer -- a va_list forwarded into a helper -- names no
// countable object and stays under the producer-side pin.  va_end needs
// no gate: it is a pure no-op here (the pair lives in the owner's frame).
static void reportVAListPairShape(Type *ValTy, const DataLayout &DL,
                                  const char *What, uint64_t Bytes) {
  // Unwrap clang's single-element __va_list_tag[1] array wrapper; any other
  // array (e.g. [2 x pair]) keeps its array shape and fails.
  Type *PairTy = ValTy;
  while (const auto *AT = dyn_cast<ArrayType>(PairTy)) {
    if (AT->getNumElements() != 1)
      break;
    PairTy = AT->getElementType();
  }
  const auto *ST = dyn_cast<StructType>(PairTy);
  const bool IsFrozenPair =
      ST && ST->getNumElements() == 2 &&
      ST->getElementType(0)->isPointerTy() &&
      ST->getElementType(1)->isIntegerTy(32) && Bytes == 8;
  if (!IsFrozenPair) {
    std::string TyStr;
    raw_string_ostream OS(TyStr);
    ValTy->print(OS);
    report_fatal_error(Twine("MCS251: ") + What +
                       " requires the frozen 8-byte va_list pair {ptr, i32}; "
                       "got a " +
                       Twine(Bytes) + "-byte object (" + TyStr + ")");
  }
}

static void checkVAListPairShape(const SDValue &SrcValueOp,
                                 const DataLayout &DL, const char *What) {
  auto *SVN = dyn_cast<SrcValueSDNode>(SrcValueOp.getNode());
  if (!SVN)
    return;
  const Value *Ptr = SVN->getValue()->stripPointerCasts();
  // Follow the pointer through all-zero (decay-like) GEPs; a GEP that
  // actually selects a field or element redirects the check to the
  // sub-object it addresses (e.g. `alloca {i8,{ptr,i16}}` + field-1 GEP is
  // judged as the 6-byte inner pair, not waved through as uncountable).
  const GetElementPtrInst *SelGEP = nullptr;
  while (const auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    bool AllZero = all_of(GEP->indices(), [](const Value *Idx) {
      const auto *C = dyn_cast<ConstantInt>(Idx);
      return C && C->isZero();
    });
    Ptr = GEP->getPointerOperand()->stripPointerCasts();
    if (!AllZero) {
      SelGEP = GEP;
      break;
    }
  }
  if (SelGEP) {
    Type *ResTy = SelGEP->getResultElementType();
    reportVAListPairShape(ResTy, DL, What, DL.getTypeAllocSize(ResTy));
    return;
  }
  Type *ValTy = nullptr;
  uint64_t Bytes = 0;
  if (auto *AI = dyn_cast<AllocaInst>(const_cast<Value *>(Ptr))) {
    ValTy = AI->getAllocatedType();
    // Honor the array-size operand: `alloca {ptr,i32}, i32 0` allocates
    // nothing and must not pass as an 8-byte pair.
    auto Bits = AI->getAllocationSizeInBits(DL);
    Bytes = Bits ? *Bits / 8 : 0;
  } else if (auto *GV = dyn_cast<GlobalVariable>(const_cast<Value *>(Ptr))) {
    ValTy = GV->getValueType();
    Bytes = DL.getTypeAllocSize(ValTy);
  } else {
    return; // no countable object behind the pointer
  }
  reportVAListPairShape(ValTy, DL, What, Bytes);
}

static void reportVAListShape(Type *ValTy, const DataLayout &DL,
                              const char *What) {

  const uint64_t Size = DL.getTypeAllocSize(ValTy);
  // Unwrap clang's single-element __va_list_tag[1] array wrapper; any
  // other array (e.g. [2 x pair]) keeps its array shape and fails.
  const Type *PairTy = ValTy;
  while (const auto *AT = dyn_cast<ArrayType>(PairTy)) {
    if (AT->getNumElements() != 1)
      break;
    PairTy = AT->getElementType();
  }
  const auto *ST = dyn_cast<StructType>(PairTy);
  const bool IsFrozenPair =
      ST && ST->getNumElements() == 2 &&
      ST->getElementType(0)->isPointerTy() &&
      ST->getElementType(1)->isIntegerTy(32) && Size == 8;
  if (!IsFrozenPair) {
    std::string TyStr;
    raw_string_ostream OS(TyStr);
    ValTy->print(OS);
    report_fatal_error(Twine("MCS251: ") + What +
                       " requires the frozen 8-byte va_list pair {ptr, i32}; "
                       "got a " +
                       Twine(Size) + "-byte object (" + OS.str() + ")");
  }
}

SDValue MCS251TargetLowering::LowerVASTART(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue AP = Op.getOperand(1); // address of the 8-byte va_list pair

  checkVAListPairShape(Op.getOperand(2), DAG.getDataLayout(), "va_start");

  MachineFunction &MF = DAG.getMachineFunction();
  const Function &F = MF.getFunction();
  EVT PtrVT = AP.getValueType();

  std::string SlotName =
      (Twine("\1") + DAG.getTarget().getSymbol(&F)->getName() + "_PARM_" +
       Twine(F.arg_size() + 1))
          .str();
  unsigned Opc = PtrVT == MVT::i16 ? MCS251::MOV16ri : MCS251::MOVADDR32;
  SDValue Base = SDValue(DAG.getMachineNode(
                             Opc, DL, PtrVT,
                             DAG.getTargetExternalSymbol(
                                 MF.createExternalSymbolName(SlotName), PtrVT)),
                         0);
  Chain = DAG.getStore(Chain, DL, Base, AP, MachinePointerInfo(), Align(1));

  SDValue OffPtr =
      DAG.getNode(ISD::ADD, DL, PtrVT, AP, DAG.getConstant(4, DL, PtrVT));
  return DAG.getStore(Chain, DL, DAG.getConstant(0, DL, MVT::i32), OffPtr,
                      MachinePointerInfo(), Align(1));
}

// va_copy duplicates the whole pair -- including the "partially consumed"
// offset state -- through two aligned-1 field copies (G2 §4.3.5; the two
// store/load shapes are probe-V selectable).
SDValue MCS251TargetLowering::LowerVACOPY(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Dst = Op.getOperand(1);
  SDValue Src = Op.getOperand(2);
  EVT PtrVT = Dst.getValueType();

  // Same shape gate as va_start: the copy reads and writes the full
  // 8-byte pair, so a narrow destination would take the same 2-byte
  // out-of-bounds write.  Operands 3/4 are the SrcValues of dst/src.
  checkVAListPairShape(Op.getOperand(3), DAG.getDataLayout(),
                       "va_copy destination");
  checkVAListPairShape(Op.getOperand(4), DAG.getDataLayout(), "va_copy source");

  SDValue Base =
      DAG.getLoad(PtrVT, DL, Chain, Src, MachinePointerInfo(), Align(1));
  Chain = Base.getValue(1);
  SDValue SrcOff =
      DAG.getNode(ISD::ADD, DL, PtrVT, Src, DAG.getConstant(4, DL, PtrVT));
  SDValue Off =
      DAG.getLoad(MVT::i32, DL, Chain, SrcOff, MachinePointerInfo(), Align(1));
  Chain = Off.getValue(1);

  Chain = DAG.getStore(Chain, DL, Base, Dst, MachinePointerInfo(), Align(1));
  SDValue DstOff =
      DAG.getNode(ISD::ADD, DL, PtrVT, Dst, DAG.getConstant(4, DL, PtrVT));
  return DAG.getStore(Chain, DL, Off, DstOff, MachinePointerInfo(),
                      Align(1));
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
  case CallingConv::MCS251_INTR:
    // ISR campaign T05 (Alice ruling 5): the interrupt entry is lowered
    // here, but it is zero-argument only and never enters the ordinary
    // parameter distribution below. The asynchronous context (PSW, DR0-28,
    // DPX and their alias SFRs) is real entry state; the frame saving of
    // that context lives in emitPrologue, not in an argument location.
    break;
  }

  // G2 B-S2 (G2-VARIADIC-DESIGN-draft.md R3 §4.3.4): variadic definitions
  // are supported -- the static continuation slots `_PARM_(F+1)..` exist in
  // addition to the fixed ones and va_start anchors the va_list pair on the
  // first of them.  One shape stays fail-closed: with zero fixed parameters
  // the first source argument occupies the DPL register channel and the
  // caller starts writing slots at `_PARM_2`, so the base formula
  // (`_PARM_(F+1)` = `_PARM_1` for F=0) would anchor one slot BEFORE the
  // first written one and va_arg would silently skip the register argument
  // and read an unwritten slot.  The shape is compiler-countable, so it is
  // a hard error, not a silent-corruption path (§4.3.6 design stance).
  if (IsVarArg && Ins.empty())
    report_fatal_error("MCS251: a variadic definition must have at least one "
                       "fixed parameter (the first source argument uses the "
                       "register channel, which va_arg cannot read)");

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

  if (CallConv == CallingConv::MCS251_INTR) {
    if (!Ins.empty())
      report_fatal_error("MCS251 ISR: interrupt entry must take no arguments");
    return Chain;
  }

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
  if (usesI32ABI(Ins[0].VT)) {
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
    InVals.push_back(fromI32ABIValue(combineI32FromBytes(Parts, DL, DAG),
                                     Ins[0].VT, DL, DAG));
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
  case CallingConv::MCS251_INTR:
    // ISR campaign T05: an interrupt entry has no call ABI (no argument or
    // return transfer, no ERET-compatible frame), so no call site may carry
    // its calling convention -- including a call written inside an ISR
    // itself. The Verifier rejects this at IR level; this is the backend
    // boundary.
    report_fatal_error("MCS251 ISR: interrupt entry may not be called",
                       /*gen_crash_diag=*/false);
  }

  // G2 B-S2: the blanket variadic rejection that stood here was REMOVED.
  // The two remaining call-side fail-closed points sit right below, once
  // IsDirect has been computed (G2-VARIADIC-DESIGN-draft.md §4.5 row 3).

  if (CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("MCS251: musttail calls are not supported",
                       /*gen_crash_diag=*/false);
  // Ordinary tail hints, including calls without an IR CallBase, are optional.
  // From an ISR this is mandatory: a tail call would place the helper's ERET
  // where the fixed frame needs RETI (T05 step 6).
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
    // A call with an ordinary CC aimed at a known interrupt entry in this
    // module is rejected here as well (T05 step 5); an unrecognized target
    // stays the domain of the Verifier and the linker.
    if (cast<Function>(GV)->getCallingConv() == CallingConv::MCS251_INTR)
      report_fatal_error("MCS251 ISR: interrupt entry may not be called",
                         /*gen_crash_diag=*/false);
  }
  const bool IsDirect = isa<GlobalAddressSDNode>(Callee) ||
                        isa<ExternalSymbolSDNode>(Callee);
  // G2 B-S2 call-side fail-closed gates (G2-VARIADIC-DESIGN-draft.md R3
  // §4.3.6 / §4.4.3).  Sema (B-S1) hard-errors both shapes at the source
  // level; these are the IR-level backstops.
  if (IsVarArg && !IsDirect)
    // C2, frozen: a function-pointer call has no named callee to derive the
    // `_PARM_n` continuation-slot symbols from.
    report_fatal_error("MCS251 variadic call form 'indirect' is not supported "
                       "(static slots require a named callee)");
  if (IsVarArg) {
    // Message F, frozen: the six continuation slots are all the ABI has.
    // NumFixedArgs is the callee's fixed-parameter count (set from the call
    // site's FunctionType), so this is exactly the Sema A-formula
    // (actuals - fixed > 6) re-checked on the lowered IR.
    unsigned NumFixed = CLI.NumFixedArgs;
    if (NumFixed == static_cast<unsigned>(-1))
      NumFixed = 0; // no call-site type survived: fail towards the gate
    if (Outs.size() > NumFixed && Outs.size() - NumFixed > 6)
      report_fatal_error("MCS251: variadic call passes more than 6 variadic "
                         "arguments (Sema cap gate missed this call; "
                         "recompile the caller with a current compiler)");
  }
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
  if (!Outs.empty() && usesI32ABI(Outs[0].VT)) {
    SmallVector<SDValue, 4> Parts;
    splitI32ToBytes(asI32ABIValue(OutVals[0], DL, DAG), DL, DAG, Parts);
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
  // Do not derive this from the generic/default pointer VT.  The MCS-251
  // executable address is always the region-qualified 32-bit CODE container;
  // in particular this must stay i32 when AS0 is the 16-bit Tiny pointer ABI.
  constexpr MVT CodePtrVT = MVT::i32;
  if (getPointerTy(DAG.getDataLayout(), ProgramAS) != CodePtrVT)
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
       Ins[0].VT != MVT::i32 && Ins[0].VT != MVT::f32))
    report_fatal_error(
        "minimal MCS251 backend only supports i8/i16/i32/f32/void return values");

  if (!Ins.empty() && usesI32ABI(Ins[0].VT)) {
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
    InVals.push_back(fromI32ABIValue(combineI32FromBytes(Parts, DL, DAG),
                                     Ins[0].VT, DL, DAG));
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
  if (CallConv == CallingConv::MCS251_INTR) {
    // ISR campaign T05: an interrupt entry returns nothing and returns via
    // RETI, never through the ordinary RetCC_MCS251 assignment machinery.
    // G2 B-S2: the ISR x variadic rejection is FROZEN (T05 zero-argument
    // ISR; G2 §4.5/§4.6 keep this branch loud while the ordinary variadic
    // rejection below it was removed).
    if (IsVarArg)
      report_fatal_error("minimal MCS251 backend does not support variadic "
                         "functions");
    if (!Outs.empty() || !RetTy->isVoidTy())
      report_fatal_error("MCS251 ISR: interrupt entry must return void");
    return true;
  }
  // G2 B-S2 (G2-VARIADIC-DESIGN-draft.md §4.5): the ordinary-definition
  // IsVarArg rejection that stood here was REMOVED.  Variadic definitions
  // now lower through the static continuation-slot ABI; the remaining
  // fail-closed points for variadic shapes are the ISR branch above, the
  // LowerCall indirect/count gates (C2 / message F) and the residual
  // llvm.va_arg reject (message E).
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
        "minimal MCS251 backend only supports i8/i16/i32/f32/void return values");
  return true;
}

SDValue MCS251TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  if (CallConv == CallingConv::MCS251_INTR) {
    // ISR campaign T05: the ISR exit is RETI (T04 opcode 0x32), which pops
    // the hardware interrupt frame and the controller in-service state --
    // ERET must never appear at an ISR exit. The frame's 37B software save
    // area is undone by the epilogue (MCS251FrameLowering), not here.
    // Defense in depth: CanLowerReturn already rejected non-void returns;
    // re-validate so a bypass of that hook cannot silently emit a value
    // return against a CC that has no ABI location for one.
    if (IsVarArg || !Outs.empty() ||
        !DAG.getMachineFunction().getFunction().getReturnType()->isVoidTy())
      report_fatal_error("MCS251 ISR: interrupt entry must return void");
    return DAG.getNode(MCS251ISD::RETI, DL, MVT::Other, Chain);
  }
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
  if (!Outs.empty() && usesI32ABI(Outs[0].VT)) {
    assert(Outs.size() == 1 && "MCS251 supports only one return value");
    SmallVector<SDValue, 4> Parts;
    splitI32ToBytes(asI32ABIValue(ReturnValue(0), DL, DAG), DL, DAG, Parts);
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
