//===-- MCS251ContractCheck.cpp - read-only MCS-251 IR contract ------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Target-owned successor of the old lib/CodeGen MCS251ContractVerifier: the
// checking half only. Everything below reads the module; a violation is a
// report_fatal_error and nothing is ever written back to the IR. The IR
// rewrites the old verifier performed ("constantPropAndFold", which ignored
// optnone) now live exclusively in MCS251LoweringPrep; when a -O0/optnone
// shape needs folding to be judged, this check evaluates it locally through
// MCS251LocalInterp instead of mutating the function.
//
// It is mounted unconditionally by MCS251TargetMachine (never tied to the
// generic -disable-verify flag) in two phases: structural checks before any
// optimization can erase evidence, arithmetic checks just before ISel.
//
//===----------------------------------------------------------------------===//

#include "MCS251ContractCheck.h"
#include "MCS251LocalInterp.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/BinaryFormat/MCS251Bit.h"
#include "llvm/BinaryFormat/MCS251ISR.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsMCS251.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>

using namespace llvm;
using namespace llvm::MCS251;

namespace {

class MCS251ContractCheckLegacy final : public ModulePass {
  std::optional<MemoryContract> Contract;
  bool CheckArithmetic;

public:
  static char ID;
  explicit MCS251ContractCheckLegacy(
      std::optional<MemoryContract> Contract = std::nullopt,
      bool CheckArithmetic = true)
      : ModulePass(ID), Contract(Contract), CheckArithmetic(CheckArithmetic) {}

  StringRef getPassName() const override {
    return "MCS-251 contract check (read-only)";
  }

  // Read-only by construction: report_fatal_error on violation, never a
  // rewrite. Every analysis is preserved.
  bool runOnModule(Module &M) override {
    if (Error Err = verifyModuleContract(M, Contract, CheckArithmetic))
      report_fatal_error(Twine(toString(std::move(Err))));
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // namespace

char MCS251ContractCheckLegacy::ID = 0;

ModulePass *llvm::MCS251::createMCS251ContractCheckPass(
    std::optional<MemoryContract> Contract, bool CheckArithmetic) {
  return new MCS251ContractCheckLegacy(Contract, CheckArithmetic);
}

PreservedAnalyses llvm::MCS251::MCS251ContractCheckPass::run(
    Module &M, ModuleAnalysisManager &) {
  if (Error Err = verifyModuleContract(M, Contract, CheckArithmetic))
    report_fatal_error(Twine(toString(std::move(Err))));
  return PreservedAnalyses::all();
}

namespace {

static Error reject(StringRef What) {
  return createStringError(inconvertibleErrorCode(),
                           "MCS251 contract violation: %s",
                           What.str().c_str());
}

static Error checkType(const Type *Ty, SmallPtrSetImpl<const Type *> &Seen) {
  if (!Seen.insert(Ty).second)
    return Error::success();

  if (const auto *PT = dyn_cast<PointerType>(Ty)) {
    unsigned AS = PT->getAddressSpace();
    // AS5 is intentionally not allocated by the MCS-251 contract. The
    // whitelist also prevents DataLayout's p0 fallback from silently turning
    // an unassigned address space into a supported one.
    switch (AS) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 7:
    case 8:
    case 9:
      break;
    default:
      return reject((Twine("unsupported pointer address space ") +
                     Twine(AS))
                        .str());
    }
  }

  for (Type *Contained : Ty->subtypes())
    if (Error Err = checkType(Contained, Seen))
      return Err;
  return Error::success();
}

static Error checkValue(const Value &V, SmallPtrSetImpl<const Type *> &Seen) {
  return checkType(V.getType(), Seen);
}

// Calls through a named LLVM function lower as GlobalAddress nodes.  Validate
// their identity before ISel, rather than relying on pointer width or allowing
// a default-AS0 declaration to reach LowerCall in a v2 module.  In particular,
// a 32-bit AS0/AS3 function pointer must not be mistaken for the AS4 CODE
// container merely because it has the same scalar width.
static Error checkDirectCallTarget(const CallBase &CB, unsigned ProgramAS) {
  const Value *Callee = CB.getCalledOperand()->stripPointerCasts();
  const auto *GV = dyn_cast<GlobalValue>(Callee);
  if (!GV)
    return Error::success();
  if (!isa<Function>(GV))
    return reject("direct call target is not a function");
  if (GV->getAddressSpace() != ProgramAS)
    return reject("direct call target function is not in the configured CODE "
                  "address space");
  return Error::success();
}

// RC-3: Constant expressions (e.g. ptrtoint of a ptr addrspace(5)) can embed
// unsupported address spaces behind an innocent result type. checkType only
// inspects the type tree, so we must also walk the operand tree of Constants.
// RC-6-C: A visited set (ConstSeen) prevents infinite recursion on
// self-referential constants such as `@node = global ptr @node`.
static Error checkConstantTree(const Constant *C,
                               SmallPtrSetImpl<const Type *> &Seen,
                               SmallPtrSetImpl<const Constant *> &ConstSeen) {
  if (!ConstSeen.insert(C).second)
    return Error::success();
  if (Error Err = checkType(C->getType(), Seen))
    return Err;
  // RC-6-D: ConstantExpr GEP carries a source element type that is not visible
  // through the result type or the operand list. A GEP into ptr addrspace(5)
  // would otherwise pass the verifier while embedding an unsupported address
  // space in the source element type.
  if (const auto *GEP = dyn_cast<GEPOperator>(C))
    if (Error Err = checkType(GEP->getSourceElementType(), Seen))
      return Err;
  for (const Use &Op : C->operands()) {
    if (const auto *Child = dyn_cast<Constant>(Op.get()))
      if (Error Err = checkConstantTree(Child, Seen, ConstSeen))
        return Err;
  }
  return Error::success();
}

// MCS251 exposes only RuntimeLibcalls.td's generated binary32 subset. Real
// f64 IR is never an alias for that subset even though the frontend's target
// double ABI width is 32: routing it to f32 helpers would silently lose
// precision. Pure zext/trunc/ptrtoint/inttoptr/bitcast round-trips through i64
// are harmless (they fold to the narrow type) and are NOT rejected here; only
// operations that actually compute on the wide/float value are.
static RTLIB::Libcall getMCS251F32BinaryLibcall(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::FAdd:
    return RTLIB::ADD_F32;
  case Instruction::FSub:
    return RTLIB::SUB_F32;
  case Instruction::FMul:
    return RTLIB::MUL_F32;
  case Instruction::FDiv:
    return RTLIB::DIV_F32;
  default:
    return RTLIB::UNKNOWN_LIBCALL;
  }
}

static bool hasMCS251ConnectedF32Compare(CmpInst::Predicate Pred) {
  auto Has = [](RTLIB::Libcall LC) {
    return isMCS251ConnectedF32Libcall(LC);
  };
  switch (Pred) {
  case CmpInst::FCMP_FALSE:
  case CmpInst::FCMP_TRUE:
    return true;
  case CmpInst::FCMP_OEQ:
    return Has(RTLIB::FCMP3_PRED_OEQ_F32);
  case CmpInst::FCMP_UEQ:
  case CmpInst::FCMP_ONE:
    return Has(RTLIB::UO_F32) && Has(RTLIB::FCMP3_PRED_OEQ_F32);
  case CmpInst::FCMP_UNE:
    return Has(RTLIB::FCMP3_PRED_UNE_F32);
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_ULE:
    return Has(RTLIB::FCMP3_PRED_OGT_F32);
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_ULT:
    return Has(RTLIB::FCMP3_PRED_OGE_F32);
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_UGE:
    return Has(RTLIB::FCMP3_PRED_OLT_F32);
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_UGT:
    return Has(RTLIB::FCMP3_PRED_OLE_F32);
  case CmpInst::FCMP_ORD:
  case CmpInst::FCMP_UNO:
    return Has(RTLIB::UO_F32);
  default:
    return false;
  }
}

// Arithmetic half of the contract. LI is the per-function local interpreter
// (shared oracle with MCS251LoweringPrep); it decides, without touching the
// IR, whether every operand of I evaluates to a compile-time constant and
// whether I is effectively dead -- the two shapes MCS251LoweringPrep erases
// for non-optnone functions and that ISel handles locally for optnone ones.
static Error checkUnsupportedArithmetic(const Instruction &I,
                                        const LocalInterp &LI) {
  auto IsWideOrFloat = [](Type *Ty) {
    return Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isIntegerTy(64);
  };
  auto IsDoubleOrDoubleVector = [](Type *Ty) {
    if (auto *VecTy = dyn_cast<VectorType>(Ty))
      Ty = VecTy->getElementType();
    return Ty->isDoubleTy();
  };

  // Do this before the fold/dead-code exemptions below. A genuine f64 IR node
  // is never permitted to reach any MCS251 lowering path, even if a local
  // constant folder could erase this particular instance.
  if (IsDoubleOrDoubleVector(I.getType()))
    return reject("f64 IR is not supported; MCS251 only connects an explicit "
                  "f32 libcall subset");
  for (const Use &Op : I.operands())
    if (IsDoubleOrDoubleVector(Op->getType()))
      return reject("f64 IR is not supported; MCS251 only connects an explicit "
                    "f32 libcall subset");

  // RC-6: Skip operations that will never reach the backend:
  //  1. All-constant operations are folded by the SelectionDAG constant
  //     folder during legalization (e.g. udiv i64 100, 4 -> i64 25). At -O0
  //     the operands may still be parked in allocas, so the verdict comes
  //     from the local interpreter rather than an IR rewrite.
  //  2. Instructions whose result is only stored to write-only scratch
  //     allocas never reach observable behavior; MCS251LoweringPrep removes
  //     them for non-optnone functions and ISel substitutes them for optnone
  //     ones.
  if (LI.allOperandsConstant(I) || LI.isEffectivelyDead(I))
    return Error::success();
  // RC-5: Vector types with wide/float elements are equally unsupported.
  auto ElementTypeIsWideOrFloat = [](Type *Ty) {
    if (auto *VecTy = dyn_cast<VectorType>(Ty))
      Ty = VecTy->getElementType();
    return Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isIntegerTy(64);
  };

  // FP-specific binary/unary ops. f32 is admitted only when its generated
  // RuntimeLibcalls.td classification names a connected helper. Real f64 is
  // always rejected, regardless of TargetInfo's frontend double ABI width.
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    Type *Ty = BO->getType();
    if (Ty->isDoubleTy())
      return reject("f64 arithmetic is not supported; MCS251 only connects an "
                    "explicit f32 libcall subset");
    if (Ty->isFloatTy()) {
      RTLIB::Libcall LC = getMCS251F32BinaryLibcall(BO->getOpcode());
      if (LC != RTLIB::UNKNOWN_LIBCALL && isMCS251ConnectedF32Libcall(LC))
        return Error::success();
      return reject("f32 operation is not in the connected libcall subset");
    }
    if (IsWideOrFloat(Ty) || ElementTypeIsWideOrFloat(Ty)) {
      switch (BO->getOpcode()) {
      default:
        break;
      case Instruction::FAdd:
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:
      case Instruction::FRem:
        return reject("f32/f64 arithmetic is not yet implemented; "
                      "soft-float runtime is not connected");
      case Instruction::Add:
      case Instruction::Sub:
      case Instruction::Mul:
      case Instruction::UDiv:
      case Instruction::SDiv:
      case Instruction::URem:
      case Instruction::SRem:
      case Instruction::Shl:
      case Instruction::LShr:
      case Instruction::AShr:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
        return reject("i64 integer arithmetic is not yet implemented; "
                      "wide-integer runtime is not connected");
      }
    }
  }

  if (auto *UO = dyn_cast<UnaryOperator>(&I)) {
    Type *Ty = UO->getType();
    if (UO->getOpcode() == Instruction::FNeg) {
      if (Ty->isFloatTy() && isMCS251ConnectedF32Libcall(RTLIB::NEG_F32))
        return Error::success();
      if (Ty->isDoubleTy())
        return reject("f64 fneg is not supported; MCS251 only connects an "
                      "explicit f32 libcall subset");
      return reject("f32 fneg is not in the connected libcall subset");
    }
  }

  // RC-5: Intrinsic float math (fabs, sqrt, ceil, floor, etc.) is lowered
  // through float softening to generic libcalls that have no target impl,
  // producing wrong code or cryptic "no libcall" errors. Reject at the IR
  // level for any intrinsic that operates on a float/wide operand.
  // RC-7 (P2): float and wide-integer (i64) intrinsics are classified
  // separately so that e.g. llvm.uadd.sat.i64 reports the wide-integer
  // diagnostic instead of the misleading soft-float one.
  if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
    auto BaseType = [](Type *Ty) -> Type * {
      if (auto *VecTy = dyn_cast<VectorType>(Ty))
        return VecTy->getElementType();
      return Ty;
    };
    SmallVector<Type *, 8> OperandTypes;
    OperandTypes.push_back(II->getType());
    for (const Value *Op : II->args())
      OperandTypes.push_back(Op->getType());
    bool OperatesOnFloat = false;
    bool OperatesOnI64 = false;
    for (Type *Ty : OperandTypes) {
      Ty = BaseType(Ty);
      if (Ty->isFloatTy() || Ty->isDoubleTy())
        OperatesOnFloat = true;
      else if (Ty->isIntegerTy(64))
        OperatesOnI64 = true;
    }
    if (OperatesOnFloat || OperatesOnI64) {
      // Allow lifetime and debug intrinsics through; reject math intrinsics.
      switch (II->getIntrinsicID()) {
      default:
        if (OperatesOnFloat)
          return reject("f32/f64 intrinsic operation is not yet implemented; "
                        "soft-float runtime is not connected");
        return reject("i64 intrinsic operation is not yet implemented; "
                      "wide-integer runtime is not connected");
      case Intrinsic::lifetime_start:
      case Intrinsic::lifetime_end:
      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
      case Intrinsic::dbg_label:
      case Intrinsic::invariant_start:
      case Intrinsic::invariant_end:
      case Intrinsic::assume:
        break;
      }
    }
  }

  // Float comparisons stay on TargetLowering::softenSetCCOperands. The
  // generic routine composes ordered/unordered predicates from the seven
  // predicate helpers below; no target-specific SETCC libcall selection exists.
  if (auto *Cmp = dyn_cast<CmpInst>(&I)) {
    if (Cmp->getPredicate() >= CmpInst::FCMP_FALSE &&
        Cmp->getPredicate() <= CmpInst::FCMP_TRUE) {
      if (Cmp->getOperand(0)->getType()->isFloatTy() &&
          hasMCS251ConnectedF32Compare(Cmp->getPredicate()))
        return Error::success();
      return reject("f32 comparison is not in the connected libcall subset");
    }
    if (IsWideOrFloat(Cmp->getOperand(0)->getType()) ||
        ElementTypeIsWideOrFloat(Cmp->getOperand(0)->getType()))
      return reject("i64 comparison is not yet implemented; "
                    "wide-integer runtime is not connected");
  }

  // The only conversion helpers are signed i32 <-> f32. Narrow and unsigned
  // forms deliberately remain absent: no __floatunsisf/__fixunssfsi exists.
  if (auto *CI = dyn_cast<CastInst>(&I)) {
    Type *SrcTy = CI->getSrcTy();
    Type *DstTy = CI->getDestTy();
    if (CI->getOpcode() == Instruction::SIToFP && SrcTy->isIntegerTy(32) &&
        DstTy->isFloatTy() &&
        isMCS251ConnectedF32Libcall(RTLIB::SINTTOFP_I32_F32))
      return Error::success();
    if (CI->getOpcode() == Instruction::FPToSI && SrcTy->isFloatTy() &&
        DstTy->isIntegerTy(32) &&
        isMCS251ConnectedF32Libcall(RTLIB::FPTOSINT_F32_I32))
      return Error::success();
    bool SrcFloat = SrcTy->isFloatTy() || SrcTy->isDoubleTy();
    bool DstFloat = DstTy->isFloatTy() || DstTy->isDoubleTy();
    if ((SrcFloat || DstFloat) && CI->getOpcode() != Instruction::BitCast)
      return reject("f32 conversion is not in the connected libcall subset");
    // i64 results from zext/sext are fine if they only feed trunc/ptrtoint
    // round-trips; the arithmetic check above catches real i64 use.
  }

  return Error::success();
}

//===----------------------------------------------------------------------===//
// MCS251 ISR (interrupt campaign T06, A2 interface freeze). These are target
// contract checks, not generic LLVM IR validity: they must run even when the
// generic verifier is disabled (-disable-verify / MIR entry), so the target
// contract check owns them. They reuse the frozen structure logic of
// Verifier::verifyMCS251ISR verbatim; no A/B-line safety-closure analysis is
// added here. All diagnostics keep the frozen "MCS251 ISR:" sentences.
//===----------------------------------------------------------------------===//

// \return true when \p GV is the standard llvm.used keepalive container in
// its full frozen structure: appending linkage, an array-of-pointers
// initializer, the "llvm.metadata" section, and no ordinary use of the
// container itself. Every other global terminates a use path as an ordinary
// escape. (Mirrors the T01 Verifier helper.)
static bool isMCS251LegalUsedRoot(const GlobalVariable &GV) {
  if (GV.getName() != "llvm.used" || !GV.hasInitializer())
    return false;
  if (!GV.hasAppendingLinkage())
    return false;
  const auto *ArrTy = dyn_cast<ArrayType>(GV.getInitializer()->getType());
  if (!ArrTy || !ArrTy->getElementType()->isPointerTy())
    return false;
  if (!GV.hasSection() || GV.getSection() != "llvm.metadata")
    return false;
  if (!GV.materialized_use_empty())
    return false;
  return true;
}

//===----------------------------------------------------------------------===//
// BT12: persistent bit-object placeholders and their escape gate.
//===----------------------------------------------------------------------===//

// \return true when \p GV is a bit-object placeholder (structural attribute).
static bool isMCS251BitObjectGlobal(const GlobalVariable &GV) {
  return GV.hasAttribute(MCS251Bit::BitObjectAttrName);
}

// A bit object must stay alive across optimization without being used as
// storage. The frozen registration channel is the standard keepalive root
// (llvm.used / llvm.compiler.used): appending linkage, an array-of-pointers
// initializer and the "llvm.metadata" section. Same structural rule as the ISR
// A2.2 exemption -- never a bare name/section test.
static bool isMCS251BitKeepaliveRoot(const GlobalVariable &GV) {
  if (GV.getName() != "llvm.used" && GV.getName() != "llvm.compiler.used")
    return false;
  if (!GV.hasInitializer() || !GV.hasAppendingLinkage())
    return false;
  const auto *ArrTy = dyn_cast<ArrayType>(GV.getInitializer()->getType());
  if (!ArrTy || !ArrTy->getElementType()->isPointerTy())
    return false;
  if (!GV.hasSection() || GV.getSection() != "llvm.metadata")
    return false;
  return true;
}

// Verify that EVERY use path out of the aggregate constant \p C terminates at
// a structurally verified keepalive root (llvm.used / llvm.compiler.used). Unlike
// a reachability walk, one path reaching a root does not excuse the constant:
// an aggregate uniqued between a keepalive root and an ordinary escape (e.g.
// `@llvm.used = ... [ptr @flag]` shared with `ret [1 x ptr] [ptr @flag]`) has a
// second, non-registration branch, so the whole constant is not exempt. Only
// whole aggregates and single-operand no-op pointer casts may act as
// intermediate nodes; any other user (an instruction, an ordinary global, a
// GEP constant) is an escape.
static bool isFullyKeepaliveConstant(const Constant *C) {
  SmallPtrSet<const Constant *, 32> Seen;
  SmallVector<const Constant *, 8> Work;
  Work.push_back(C);
  while (!Work.empty()) {
    const Constant *Cur = Work.pop_back_val();
    if (!Seen.insert(Cur).second)
      continue;
    for (const User *U : Cur->users()) {
      if (const auto *GV = dyn_cast<GlobalVariable>(U)) {
        if (!isMCS251BitKeepaliveRoot(*GV))
          return false;
        continue;
      }
      if (const auto *UC = dyn_cast<Constant>(U)) {
        if (isa<ConstantAggregate>(UC)) {
          Work.push_back(UC);
          continue;
        }
        if (const auto *CE = dyn_cast<ConstantExpr>(UC))
          if (CE->getNumOperands() == 1 &&
              (CE->getOpcode() == Instruction::BitCast ||
               CE->getOpcode() == Instruction::AddrSpaceCast)) {
            Work.push_back(UC);
            continue;
          }
        return false;
      }
      return false;
    }
  }
  return true;
}

//===----------------------------------------------------------------------===//
// P09 section 1.3: symbolic bit-access intrinsics (llvm.mcs251.bit.obj.*).
//
// The whitelist is bidirectional and identity-based, never name-based:
//   A. from the four intrinsic declarations/call sites -- so a bad operand on
//      a call that uses NO marked global is still caught;
//   B. from every Use of every handle -- so a legal consumer never masks an
//      escape on another path.
// Family identity is ONLY Function::getIntrinsicID() against the four exact
// generated enums; a same-looking name that is not the registered intrinsic,
// and a registered name with a wrong signature, are distinguished by A.2's
// explicit signature check (which also covers the -disable-verify entry).
//===----------------------------------------------------------------------===//

static bool isMCS251SymbolicBitIntrinsic(Intrinsic::ID IID) {
  switch (IID) {
  case Intrinsic::mcs251_bit_obj_read:
  case Intrinsic::mcs251_bit_obj_set:
  case Intrinsic::mcs251_bit_obj_clear:
  case Intrinsic::mcs251_bit_obj_toggle:
    return true;
  default:
    return false;
  }
}

// The frozen section 1.1 signature: `i1 (ptr)` for read, `void (ptr)` for the
// three writers -- always exactly one AS0 opaque-pointer parameter, never
// vararg. Types are context-uniqued, so pointer identity IS exact equality.
static FunctionType *getMCS251BitObjFunctionType(Intrinsic::ID IID,
                                                 LLVMContext &Ctx) {
  Type *Ret = IID == Intrinsic::mcs251_bit_obj_read
                  ? Type::getInt1Ty(Ctx)
                  : Type::getVoidTy(Ctx);
  return FunctionType::get(Ret, {PointerType::get(Ctx, 0)},
                           /*isVarArg=*/false);
}

// Section 1.3 A.5: no narrowing of the section 1.2 worst-case effect model.
// A generated ID proves nothing about the attributes that were actually
// written on the declaration or the call site, so the EFFECTIVE attributes of
// both are checked: any memory(...) narrower than unknown (read/write/argmem/
// inaccessiblemem -- the legacy readonly/readnone spellings fold into this),
// speculatable, and per-parameter noalias / dereferenceable promises.
static bool hasIncompatibleBitObjEffects(const AttributeList &AL) {
  if (AL.getFnAttrs().hasAttribute(Attribute::Speculatable))
    return true;
  if (AL.getFnAttrs().getMemoryEffects() != MemoryEffects::unknown())
    return true;
  AttributeSet Param = AL.getParamAttrs(0);
  if (Param.hasAttribute(Attribute::NoAlias) ||
      Param.hasAttribute(Attribute::Dereferenceable) ||
      Param.hasAttribute(Attribute::DereferenceableOrNull))
    return true;
  return false;
}

// Section 1.3 direction A. Verifies the declarations (even unused ones), then
// every call site -- call form first, then the operand, then effects (the
// section 1.4 diagnostic order) -- and finally that the intrinsic functions
// themselves are only ever the direct callee of a whitelisted call. Fills
// \p ValidObjCalls with each fully validated CallInst; direction B admits
// exactly these as the sole legal consumers of a handle.
static Error verifyMCS251SymbolicBitIntrinsics(
    const Module &M, unsigned ProgramAS,
    SmallPtrSetImpl<const CallBase *> &ValidObjCalls) {
  auto SigReject = [] {
    return reject("MCS251 symbolic bit intrinsic: invalid declaration or "
                  "call signature");
  };
  auto FormReject = [] {
    return reject("MCS251 symbolic bit intrinsic: only direct unbundled "
                  "calls are supported");
  };
  auto OperandReject = [] {
    return reject("MCS251 symbolic bit intrinsic: operand must be a direct "
                  "AS0 bit-object global");
  };
  auto EffectsReject = [] {
    return reject("MCS251 symbolic bit intrinsic: incompatible effects or "
                  "pointer attributes");
  };

  // A.2: the four declarations. A resolved ID does not prove the signature,
  // so the exact FunctionType, declaration-only form, C calling convention
  // and the module's program address space are all checked explicitly. This
  // also fires for an unused wrong-signature declaration.
  SmallVector<const Function *, 4> IntrinsicDecls;
  for (const Function &F : M) {
    if (!isMCS251SymbolicBitIntrinsic(F.getIntrinsicID()))
      continue;
    if (F.getFunctionType() !=
            getMCS251BitObjFunctionType(F.getIntrinsicID(), M.getContext()) ||
        F.isVarArg() || !F.isDeclaration() ||
        F.getCallingConv() != CallingConv::C ||
        F.getAddressSpace() != ProgramAS)
      return SigReject();
    if (hasIncompatibleBitObjEffects(F.getAttributes()))
      return EffectsReject();
    IntrinsicDecls.push_back(&F);
  }

  // Collect the family's call sites: identity comes from the callee operand
  // being directly one of the four Functions (getCalledFunction() does not
  // strip casts; a bitcast callee never enters this list and is rejected
  // below as an identity escape).
  SmallVector<const CallBase *, 8> FamilyCalls;
  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        const auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        const Function *Callee = dyn_cast<Function>(CB->getCalledOperand());
        if (!Callee || !isMCS251SymbolicBitIntrinsic(Callee->getIntrinsicID()))
          continue;
        FamilyCalls.push_back(CB);
      }
  }

  // A.3: the only legal call node is a plain, direct, non-tail, unbundled
  // CallInst with the exact callsite signature and C convention.
  for (const CallBase *CB : FamilyCalls) {
    if (!isa<CallInst>(CB))
      return FormReject(); // invoke / callbr
    const auto *CI = cast<CallInst>(CB);
    if (CI->isTailCall() || CI->isMustTailCall() || CB->hasOperandBundles())
      return FormReject();
    if (CB->getCallingConv() != CallingConv::C)
      return SigReject();
    if (CB->getFunctionType() !=
            getMCS251BitObjFunctionType(
                cast<Function>(CB->getCalledOperand())->getIntrinsicID(),
                M.getContext()) ||
        CB->arg_size() != 1)
      return SigReject();
  }

  // A.4: the single argument must dyn_cast DIRECTLY to a module
  // GlobalVariable in AS0 carrying the structural bit-object attribute. No
  // cast stripping, no inttoptr/GEP/select/PHI/alloca/parameter/null forms;
  // the full object-structure rules (placement, i8, initializer) were already
  // checked for every marked global by the structural phase.
  for (const CallBase *CB : FamilyCalls) {
    Value *Op0 = CB->getArgOperand(0);
    const auto *GV = dyn_cast<GlobalVariable>(Op0);
    if (!GV || GV->getAddressSpace() != 0 ||
        !GV->hasAttribute(MCS251Bit::BitObjectAttrName))
      return OperandReject();
  }

  // A.5: the effective callsite attributes and alias metadata. The handle is
  // identity, not a disjoint memory location: !alias.scope / !noalias must
  // not buy the controlled access any optimization discount.
  for (const CallBase *CB : FamilyCalls) {
    if (hasIncompatibleBitObjEffects(CB->getAttributes()))
      return EffectsReject();
    if (CB->getMetadata(LLVMContext::MD_alias_scope) ||
        CB->getMetadata(LLVMContext::MD_noalias))
      return EffectsReject();
    ValidObjCalls.insert(CB);
  }

  // A.6: the intrinsic function identity itself may only appear as the direct
  // callee of a whitelisted call. Taking its address, exporting it to an
  // initializer, or handing it to another function is rejected (a bitcast
  // callee dies here too, which is why it never entered FamilyCalls).
  for (const Function *F : IntrinsicDecls)
    for (const User *U : F->users()) {
      const auto *CB = dyn_cast<CallBase>(U);
      if (CB && ValidObjCalls.contains(CB))
        continue;
      return FormReject();
    }

  return Error::success();
}

// A bit-object placeholder is object identity only: it must never be used as
// ordinary storage. Reject every escape the frozen handle contract forbids
// (ordinary load/store, GEP, cast, ptrtoint, a call argument, alias/ifunc, or
// a constant expression / initializer export). The accepted uses of a handle
// are exactly two: the verified keepalive registration that keeps it alive,
// and the argument-0 use of a whitelisted symbolic bit-intrinsic call (P09
// section 1.3 B). A validated call has exactly one argument, no operand
// bundles and a direct marked-handle argument, so any OTHER use of the handle
// through that call node is impossible; every call-shaped user outside the
// whitelist -- ordinary call, pseudo intrinsic, operand bundle, callee use,
// llvm.assume/lifetime/debug intrinsic -- is rejected. This runs in the
// target entry pass, so it also applies with the generic verifier disabled.
static Error
verifyMCS251BitObjectUses(const Module &M,
                          const SmallPtrSetImpl<const CallBase *> &ValidCalls) {
  auto bitReject = [](const GlobalVariable &GV, StringRef What) {
    return reject(
        (Twine("MCS251 bit object '") + GV.getName() + "'" + What).str());
  };
  for (const GlobalVariable &GV : M.globals()) {
    if (!isMCS251BitObjectGlobal(GV))
      continue;
    for (const User *U : GV.users()) {
      if (isa<GlobalAlias>(U) || isa<GlobalIFunc>(U))
        return bitReject(GV, ": handle must not escape through alias/ifunc");
      if (const auto *CB = dyn_cast<CallBase>(U))
        if (ValidCalls.contains(CB))
          // Section 1.3 B.2: the ONLY whitelisted call consumer. A validated
          // call has exactly one argument, no operand bundles and this exact
          // marked global as its direct argument, so any Use of the handle
          // through it is necessarily the argument-0 use; a callee or bundle
          // use could not have passed direction A.
          continue;
      if (isa<CallBase>(U))
        return bitReject(GV, ": handle must not be used by a non-whitelisted "
                             "call or operand bundle");
      if (isa<Instruction>(U))
        return bitReject(GV,
                         ": handle escape (ordinary load/store/GEP/cast/"
                         "ptrtoint/instruction use); a bit object has no byte "
                         "address");
      if (const auto *C = dyn_cast<Constant>(U)) {
        // Require every use path of this constant to stay inside verified
        // keepalive registration; a shared aggregate with a second, ordinary
        // branch is not exempt.
        if (isFullyKeepaliveConstant(C))
          continue;
        return bitReject(GV,
                         ": handle must not escape through a constant expression "
                         "or initializer");
      }
      return bitReject(GV,
                       ": handle escape; a bit object has no byte address");
    }
  }
  return Error::success();
}

// BT12: structural rules of every marked bit-object placeholder (placement,
// linkage, i8 value type, constant 0/1 initializer). The use-side escape gate
// is verifyMCS251BitObjectUses; the split keeps the section 1.4 diagnostic
// order: structure first, then declaration signature / call form / operand /
// effects, then the remaining escapes.
static Error verifyMCS251BitObjectStructure(const Module &M) {
  auto bitReject = [](const GlobalVariable &GV, StringRef What) {
    return reject(
        (Twine("MCS251 bit object '") + GV.getName() + "'" + What).str());
  };
  for (const GlobalVariable &GV : M.globals()) {
    if (!isMCS251BitObjectGlobal(GV))
      continue;
    if (GV.isThreadLocal() || GV.getAddressSpace() != 0 || GV.hasSection() ||
        GV.hasComdat() ||
        (!GV.hasExternalLinkage() && !GV.hasLocalLinkage() &&
         !GV.hasPrivateLinkage()) ||
        GV.getVisibility() != GlobalValue::DefaultVisibility ||
        GV.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
        GV.hasCommonLinkage())
      return bitReject(GV, ": unsupported placement or linkage");
    if (!GV.getValueType()->isIntegerTy(8))
      return bitReject(GV, ": placeholder must be an i8 global");
    if (!GV.isDeclaration()) {
      const auto *CI = dyn_cast<ConstantInt>(GV.getInitializer());
      if (!CI || CI->getValue().ugt(1))
        return bitReject(GV, ": initializer must be the constant 0 or 1");
    }
  }
  return Error::success();
}

// Verify the A2 structural contract for every interrupt entry in the module.
// Ordinary functions without the CC/attribute pair are completely unaffected.
static Error verifyMCS251ISRStructure(const Module &M) {
  DenseMap<uint64_t, const Function *> SlotDefs;
  for (const Function &F : M) {
    Attribute VecAttr = F.getFnAttribute("mcs251-isr-vector");
    bool HasCC = F.getCallingConv() == CallingConv::MCS251_INTR;
    bool HasVector = VecAttr.isValid();
    if (!HasCC && !HasVector)
      continue;

    // The convention and the vector attribute are inseparable.
    if (!HasCC || !HasVector)
      return reject("MCS251 ISR: calling convention and vector attribute must "
                    "appear together");

    uint64_t SlotVal = 0;
    if (!MCS251ISR::parseCanonicalSlot(VecAttr.getValueAsString(), SlotVal) ||
        !MCS251ISR::isLegalISRSlot(SlotVal))
      return reject((Twine("MCS251 ISR: vector is not a legal slot in profile "
                           "0-") +
                     Twine(MCS251ISR::ISRVectorMaxSlot))
                        .str());

    // An entry has hardware-fixed frame; only non-vararg void() is an entry.
    if (!(F.getFunctionType()->getReturnType()->isVoidTy() &&
          F.getFunctionType()->getNumParams() == 0 && !F.isVarArg()))
      return reject("MCS251 ISR: interrupt entry must have non-vararg type "
                    "void()");

    // External, internal and private linkage only; aliases and ifuncs are
    // separate global kinds and are rejected as ordinary uses below.
    if (!(F.hasExternalLinkage() || F.hasInternalLinkage() ||
          F.hasPrivateLinkage()))
      return reject("MCS251 ISR: unsupported interrupt entry linkage");
    if (F.hasComdat())
      return reject("MCS251 ISR: interrupt entry may not be in a COMDAT");

    // Only structurally correct llvm.used keepalive entries may reference the
    // entry; ordinary calls and ordinary pointer values may not. The
    // traversal walks every use path upward with an iterative worklist and a
    // visited set, so the verdict never depends on use-list order even when
    // constants are shared between the registration root and an ordinary
    // escape. Only aggregates and single-operand no-op pointer casts
    // (bitcast/addrspacecast) may act as intermediate nodes; aliases, ifuncs,
    // every other constant kind and every instruction are rejected as
    // non-registration escapes.
    SmallPtrSet<const User *, 32> VisitedUses;
    SmallVector<const User *, 32> UseWorklist;
    bool Rooted = false;
    auto WalkUsePaths = [&](const User *Seed) -> Error {
      UseWorklist.push_back(Seed);
      while (!UseWorklist.empty()) {
        const User *U = UseWorklist.pop_back_val();
        if (!VisitedUses.insert(U).second)
          continue;
        if (const auto *CB = dyn_cast<CallBase>(U)) {
          // A call-like instruction entering the ISR is rejected regardless
          // of the calling convention written on the call itself.
          if (CB->getCalledOperand()->stripPointerCasts() == &F)
            return reject("MCS251 ISR: interrupt entry may not be called");
          return reject("MCS251 ISR: interrupt entry has a non-registration "
                        "use");
        }
        if (const auto *GV = dyn_cast<GlobalVariable>(U)) {
          // A global is a terminal of the traversal: the standard keepalive
          // container completes only this branch; anything else (an ordinary
          // escaped global, llvm.compiler.used, a malformed container) is a
          // non-registration use.
          if (isMCS251LegalUsedRoot(*GV)) {
            Rooted = true;
            continue;
          }
          return reject("MCS251 ISR: interrupt entry has a non-registration "
                        "use");
        }
        const Constant *C = dyn_cast<Constant>(U);
        const Operator *Op = C ? dyn_cast<Operator>(C) : nullptr;
        bool MayContinue =
            C && (isa<ConstantAggregate>(C) ||
                  (Op && Op->getNumOperands() == 1 &&
                   (Op->getOpcode() == Instruction::BitCast ||
                    Op->getOpcode() == Instruction::AddrSpaceCast)));
        if (MayContinue) {
          for (const User *UU : U->users())
            UseWorklist.push_back(UU);
          continue;
        }
        return reject("MCS251 ISR: interrupt entry has a non-registration use");
      }
      return Error::success();
    };
    for (const User *U : F.users())
      if (Error Err = WalkUsePaths(U))
        return Err;

    if (F.isDeclaration())
      continue;

    // BlockAddress constants hold their BasicBlock* raw and have no
    // operands; every real use of a taken block address must pass the same
    // registration whitelist.
    for (const BasicBlock &BB : F)
      if (const BlockAddress *BA = BlockAddress::lookup(&BB))
        for (const User *U : BA->users())
          if (Error Err = WalkUsePaths(U))
            return Err;

    if (!F.hasFnAttribute(Attribute::NoInline))
      return reject("MCS251 ISR: interrupt definition must be noinline");
    if (!Rooted)
      return reject("MCS251 ISR: interrupt definition must be kept alive by "
                    "llvm.used");

    auto SlotIt = SlotDefs.insert({SlotVal, &F}).first;
    if (SlotIt->second != &F)
      return reject("MCS251 ISR: vector slot is already registered by another "
                    "definition in this module");
  }

  // A2.11: no call may itself carry the interrupt convention, and no
  // call-like instruction may target an interrupt entry.
  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        const auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        if (CB->getCallingConv() == CallingConv::MCS251_INTR)
          return reject("MCS251 ISR: interrupt entry may not be called");
        const auto *Callee =
            dyn_cast<Function>(CB->getCalledOperand()->stripPointerCasts());
        if (Callee && Callee->getCallingConv() == CallingConv::MCS251_INTR)
          return reject("MCS251 ISR: interrupt entry may not be called");
      }
  }
  return Error::success();
}
} // namespace

Error llvm::MCS251::verifyModuleContract(const Module &M,
                                         const std::optional<MemoryContract>
                                             &Contract,
                                         bool CheckArithmetic) {
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::mcs251)
    return Error::success();

  const DataLayout &DL = M.getDataLayout();
  const unsigned ProgramAS = DL.getProgramAddressSpace();
  unsigned AS0Bits = DL.getPointerSizeInBits(0);
  if (AS0Bits != 16 && AS0Bits != 32)
    return reject("AS0 pointer width must be 16 or 32 bits");

  if (Contract) {
    StringRef ExpectedLayout = getCompatibilityDataLayout();
    if (Contract->isSpecified()) {
      if (!isValidMemoryContract(*Contract))
        return reject("invalid numeric memory contract");
      auto Desc = getLayoutDesc(
          static_cast<ASLayoutVersion>(Contract->ASLayoutVersion),
          static_cast<AS0PointerBits>(Contract->AS0PointerBits));
      if (!Desc)
        return reject("numeric memory contract has no data layout");
      ExpectedLayout = Desc->DataLayout;
    }
    if (DL != DataLayout(ExpectedLayout))
      return reject("module data layout conflicts with the selected memory "
                    "contract");
  }

  // ISR campaign T06 step 1: the A2 ISR identity/registration checks are
  // target contract, not generic LLVM IR validity. They run here on every
  // invocation of this pass (both the structural pre-pass and the post-
  // optimization arithmetic pass), even when the generic verifier is turned
  // off. The A2 structure logic is reused verbatim; no safety-closure
  // analysis is added.
  if (Error Err = verifyMCS251ISRStructure(M))
    return Err;

  // BT12 + P09 section 1.3/1.4: the bit-object contract is checked in the
  // frozen diagnostic order -- placeholder STRUCTURE first, then the symbolic
  // intrinsic family's declaration signature, call form, operand and effects
  // (direction A), then the remaining handle escapes (direction B, which
  // admits exactly direction A's whitelisted calls as the sole consumers).
  // All of it runs on every invocation of this pass (structural pre-pass and
  // post-optimization arithmetic pass alike), even with the generic verifier
  // turned off.
  if (Error Err = verifyMCS251BitObjectStructure(M))
    return Err;
  SmallPtrSet<const CallBase *, 16> ValidObjCalls;
  if (Error Err = verifyMCS251SymbolicBitIntrinsics(M, ProgramAS, ValidObjCalls))
    return Err;
  if (Error Err = verifyMCS251BitObjectUses(M, ValidObjCalls))
    return Err;

  SmallPtrSet<const Type *, 32> Seen;
  SmallPtrSet<const Constant *, 32> ConstSeen;
  // Cover declarations, definitions, aliases and ifuncs. Looking only at
  // globals() misses an unsupported pointer hidden in a declaration or an
  // alias operand, while looking only at instruction result types misses a
  // pointer payload loaded from otherwise ordinary AS0 storage.
  for (const GlobalValue &GV : M.global_values()) {
    if (Error Err = checkValue(GV, Seen))
      return Err;
    if (const auto *GVar = dyn_cast<GlobalVariable>(&GV)) {
      if (Error Err = checkType(GVar->getValueType(), Seen))
        return Err;
      if (GVar->hasInitializer())
        if (Error Err = checkConstantTree(GVar->getInitializer(), Seen,
                                          ConstSeen))
          return Err;
    }
    if (const auto *F = dyn_cast<Function>(&GV))
      if (Error Err = checkType(F->getFunctionType(), Seen))
        return Err;
    if (const auto *U = dyn_cast<User>(&GV))
      for (const Use &Operand : U->operands()) {
        if (Error Err = checkValue(*Operand, Seen))
          return Err;
        if (const auto *C = dyn_cast<Constant>(Operand.get()))
          if (Error Err = checkConstantTree(C, Seen, ConstSeen))
            return Err;
      }
  }

  for (const Function &F : M) {
    for (const Argument &A : F.args())
      if (Error Err = checkValue(A, Seen))
        return Err;
    // Dominance information for the shared alloca constant-propagation
    // safety query. DominatorTree never mutates the function; the const_cast
    // only satisfies the legacy non-const analysis constructor API. The
    // LocalInterp itself is a pure query oracle: it evaluates instructions
    // by folding their constant operands locally and never writes back.
    std::optional<DominatorTree> MaybeDT;
    std::optional<LocalInterp> LI;
    if (CheckArithmetic && !F.isDeclaration()) {
      MaybeDT.emplace(const_cast<Function &>(F));
      LI.emplace(M.getDataLayout(), &*MaybeDT);
    }
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        // Direct function calls must use the module's executable address
        // space.  This catches a stale AS0 declaration in v2 before the DAG
        // has an opportunity to coerce its scalar representation.
        if (const auto *CB = dyn_cast<CallBase>(&I))
          if (Error Err = checkDirectCallTarget(*CB, ProgramAS))
            return Err;
        // RC-6: arithmetic checks run post-optimization only, so that
        // foldable or dead i64/f32/f64 operations are not falsely rejected.
        // Structural checks (types, address spaces) always run. LI is
        // engaged for every function that reaches this loop (definitions
        // only); declarations have no instructions.
        if (LI)
          if (Error Err = checkUnsupportedArithmetic(I, *LI))
            return Err;
        if (Error Err = checkValue(I, Seen))
          return Err;
        // RC-3: alloca allocated type and GEP source element type carry
        // pointer payloads that are not visible through the instruction result
        // type. An alloca of ptr addrspace(5) or a GEP into ptr addrspace(5)
        // would otherwise pass the verifier while embedding an unsupported
        // address space in the allocated/element type.
        if (const auto *AI = dyn_cast<AllocaInst>(&I))
          if (Error Err = checkType(AI->getAllocatedType(), Seen))
            return Err;
        if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I))
          if (Error Err = checkType(GEP->getSourceElementType(), Seen))
            return Err;
        for (const Use &U : I.operands()) {
          if (Error Err = checkValue(*U, Seen))
            return Err;
          // RC-3: recurse into constant-expression operand trees so that
          // unsupported address spaces hidden behind ptrtoint/inttoptr
          // round-trips are caught.
          if (const auto *C = dyn_cast<Constant>(U.get()))
            if (Error Err = checkConstantTree(C, Seen, ConstSeen))
              return Err;
        }
      }
  }
  return Error::success();
}
