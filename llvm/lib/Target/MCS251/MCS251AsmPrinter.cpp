//===-- MCS251AsmPrinter.cpp - MCS-251 assembly writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emits complete ASxxxx (sdas251) assembly modules directly:
//
//         .module <name>
//         .source
//         .optsdcc <SDCC ABI signature>
//         .area CSEG (CODE)
//         .globl _foo
// _foo:
//         <instructions>
//
// Design notes (Phase 12, Step 1 "strip SDCC"):
//
//  - The module prologue below replaces the ELF prologue the generic
//    AsmPrinter machinery would print.  Everything else is already
//    dialect-compatible: the InstPrinter's operand spellings (#0x12,
//    #(_sym+1), @wr14-0x1234, ...) were validated against sdas251 as-is,
//    .globl/labels/comments match, and the file just ends after the last
//    instruction (ASxxxx needs no .end).
//
//  - Sections use a target TargetLoweringObjectFile backed by native
//    MCSectionELF objects: text/read-only data select CSEG, mutable storage
//    selects NOBITS `.mcs251.dseg`, and its ROM load image selects PROGBITS
//    `.mcs251.xinit`. MCS251MCAsmInfo still suppresses generic ELF directives;
//    this printer emits the corresponding ASxxxx `.area` spellings while the
//    object writer maps the same MCSections to A records.
//
//  - The trailing ".section .note.GNU-stack" from AsmPrinter::doFinalization
//    is suppressed by MCS251MCAsmInfo::getStackSection returning nullptr.
//
//===----------------------------------------------------------------------===//

#include "MCS251.h"
#include "MCS251BitObject.h"
#include "MCS251HelperABI.h"
#include "MCS251InstrInfo.h"
#include "MCS251MCInstLower.h"
#include "MCS251TargetMachine.h"
#include "MCS251TargetObjectFile.h"
#include "MCTargetDesc/MCS251ABISignature.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MCS251Attributes.h"
#include "llvm/BinaryFormat/MCS251AttributesReader.h"
#include "llvm/BinaryFormat/MCS251AttributesWriter.h"
#include "llvm/BinaryFormat/MCS251Bit.h"
#include "llvm/BinaryFormat/MCS251ISR.h"
#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// The .optsdcc line is the SDCC ABI signature this backend interoperates
// with.  sdld checks it (when linking with --mcs251-abi / strict checks) and
// the smoke harness compares it literally, so it must stay byte-for-byte
// identical to what the reference SDCC toolchain emits.  Locked against the
// Phase 12 specimen
// (/tmp/mcs251-p12/sample/llvm-mcs251-asxxxx-specimen.asm), which was
// assembled, linked and executed under QEMU to validate the exact string.
// The payload is shared with the object writer's O record
// (MCTargetDesc/MCS251ABISignature.h).
static const std::string OptsdccSignature =
    (".optsdcc " + std::string(MCS251::ABISignaturePayload));

// Derive the ASxxxx module name the way SDCC does: the stem of the source
// file name, with anything outside [A-Za-z0-9_] mapped to '_' and a '_'
// prepended when the result would start with a digit (ASxxxx symbols must
// begin with a letter).
static std::string getMCS251ModuleName(const Module &M) {
  std::string Name = sys::path::stem(M.getSourceFileName()).str();
  if (Name.empty())
    Name = sys::path::stem(M.getModuleIdentifier()).str();
  if (Name.empty())
    return "mcs251_module";
  for (char &C : Name)
    if (!isAlnum(static_cast<unsigned char>(C)) && C != '_')
      C = '_';
  if (isdigit(static_cast<unsigned char>(Name.front())))
    Name.insert(Name.begin(), '_');
  return Name;
}

namespace {
class MCS251AsmPrinter final : public AsmPrinter {
  StringSet<> LocalParameterSlots;
  StringSet<> DeclaredExternalSymbols;
  // Next free record index in the single .mcs251.isr section of this object;
  // the A3.4 relocation of record N sits at r_offset 24*N+12.
  unsigned ISRRecordCount = 0;
  // BT12: next free record index in the single .mcs251.bit section; the
  // R_MCS251_BIT_REF association of record N sits at r_offset 8*N+4 and the
  // kind-1 symbol's st_value is 8*N.
  unsigned BitRecordCount = 0;
  // BT12: whether this module defines any persistent bit-object placeholder.
  bool ModuleHasBitObjects = false;
  // BT12: symbol identity of every bit-object placeholder in the module. A
  // handle is recognized by RESOLVING BACK TO THE GLOBALVALUE (never by
  // operand kind), because MIR can name the same symbol three ways: as a
  // MO_GlobalAddress (`@flag`), as a MO_ExternalSymbol (`&flag`, which the
  // mangler maps onto the same MCContext symbol) or as a raw MO_MCSymbol.
  // Built once per module in emitStartOfAsmFile, before any function is
  // emitted, so verifyFinalMachineBoundary can consult it.
  DenseMap<const MCSymbol *, const GlobalVariable *> BitObjectSymbols;
  // Whether this module defines any interrupt entry. The A2.2 keepalive
  // exemption (object gate, storage-reservation scan, global emission)
  // exists only for ISR modules; modules without ISR definitions keep the
  // original behavior for llvm.used unchanged.
  bool ModuleHasISRDefinitions = false;
  // A4/W2+W3, re-ruled by W3b (PM ruling 2026-09-13 #2): set by
  // classifyModule whenever ELF object output runs under a specified v2
  // layout contract (ASLayoutVersion==2, the materialized default included)
  // and the module passed the capability gate.  The object identity is the
  // CONTRACT GENERATION, never the module content; the historical downgrade
  // of a "v1-representable" module to the v1 identity is deleted.  It arms
  // the v2 identity publication: e_flags=EFlagsV2 (installed on the ELF
  // writer before initSections, so the v1 note is never emitted) and the
  // `.mcs251.attributes` section in emitEndOfAsmFile.
  bool ModuleV2Identity = false;

  // P-4 (freeze 2026-09-14): the function-signature table assembled for this
  // compilation.  Built once per module in emitStartOfAsmFile from the
  // `!mcs251.signatures` named metadata (the producer's source-typed record),
  // augmented with records for backend-generated external libcalls whose ABI
  // is registered (MCS251HelperABI.h), and handed to the attributes codec in
  // emitEndOfAsmFile.  FunctionSignaturesBuilt distinguishes "already
  // assembled" from "not a v2 object"; it is reset per module.
  MCS251Signatures::Table FunctionSignatures;
  StringSet<> FunctionSignatureNames;
  bool FunctionSignaturesBuilt = false;
  // Final ELF symbols of backend-generated external libcalls actually seen in
  // the machine stream, collected during function emission and folded into
  // FunctionSignatures before the object identity is published.
  StringSet<> PendingHelperSymbols;

  const MCS251TargetMachine &getMCS251TM() const {
    return static_cast<const MCS251TargetMachine &>(TM);
  }

  bool usesELFObjects() const { return getMCS251TM().usesELFObjects(); }

  // The slot value is canonical decimal text ("0" and "1" are legal;
  // "01", "+1", "0x1", the empty string and negative values are not).
  // (Mirrors the frozen T01 structure check.)
  static bool isCanonicalISRSlotText(StringRef SlotText, uint64_t &SlotVal) {
    if (SlotText.empty() ||
        (SlotText.size() > 1 && SlotText.front() == '0'))
      return false;
    for (char C : SlotText) {
      if (!isDigit(C))
        return false;
      SlotVal = SlotVal * 10 + (C - '0');
      if (SlotVal > 51)
        break; // already out of profile; no need to accumulate further
    }
    return true;
  }

  // A4/W3b (design §3.3, PM ruling 2026-09-13 #2): the D.5 static-slot
  // address spaces.  Under a v2 contract the ELF identity is the contract's
  // generation regardless of content; a trailing pointer parameter in one of
  // these spaces is a registered capability, while any other pointer address
  // space in that position stays an unregistered capability (fail-closed).
  static bool isD5StaticSlotAddressSpace(unsigned AS) {
    switch (AS) {
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

  static bool hasV1PointerTypes(Type *Ty,
                                SmallPtrSetImpl<Type *> &Seen,
                                bool AllowV2SlotAS = false) {
    // Check a pointer before consulting Seen.  An unsupported pointer type is
    // rejected on every encounter, rather than becoming acceptable merely
    // because a previous walk inserted it before returning false.
    //
    // X3: this walk still governs every pointer OUTSIDE global storage
    // (signatures, instruction operands, arbitrary constants), where an
    // AS3/AS4 pointer capability remains v2-only.  AS3/AS4 pointer
    // CONSTANTS that are the exact emittable initializer leaf are admitted
    // separately by hasV1PlacementInitializer().
    //
    // A4/W3: with AllowV2SlotAS the walk is the v2 whitelist variant used
    // ONLY for function signatures and function bodies, where the D.5
    // static-slot capability (and the pointer values that feed it) is a
    // registered v2 capability.  The global-storage walks below never set
    // the flag: global pointer storage keeps the exact v1 rules.
    if (auto *PT = dyn_cast<PointerType>(Ty))
      return PT->getAddressSpace() == 0 ||
             (AllowV2SlotAS && isD5StaticSlotAddressSpace(PT->getAddressSpace()));
    if (!Seen.insert(Ty).second)
      return true;
    for (Type *SubTy : Ty->subtypes())
      if (!hasV1PointerTypes(SubTy, Seen, AllowV2SlotAS))
        return false;
    return true;
  }

  // XSmall/v2 AS6 has one object-identity-safe form: a constant byte SFR
  // address used directly as a load/store base. It lowers to a direct-byte
  // instruction and carries neither a pointer payload nor a relocation. AS6
  // in a global, function signature, value result, or any other expression
  // still requires a future v2 object identity.
  static bool isDirectSFRMemoryOperand(const Use &U, const Instruction &I) {
    bool IsLoadBase = isa<LoadInst>(I) &&
                      &U == &I.getOperandUse(LoadInst::getPointerOperandIndex());
    bool IsStoreBase =
        isa<StoreInst>(I) &&
        &U == &I.getOperandUse(StoreInst::getPointerOperandIndex());
    if ((!IsLoadBase && !IsStoreBase) ||
        U->getType()->getPointerAddressSpace() != 6)
      return false;

    const auto *CE = dyn_cast<ConstantExpr>(U.get());
    if (!CE || CE->getOpcode() != Instruction::IntToPtr)
      return false;
    const auto *Address = dyn_cast<ConstantInt>(CE->getOperand(0));
    if (!Address || !Address->getType()->isIntegerTy(16))
      return false;
    uint64_t Value = Address->getZExtValue();
    if (Value < 0x80 || Value > 0xfe)
      return false;

    if (IsLoadBase)
      return cast<LoadInst>(I).getType()->isIntegerTy(8);
    return cast<StoreInst>(I).getValueOperand()->getType()->isIntegerTy(8);
  }

  // ConstantExpr can hide an address-space-bearing operand behind an integer
  // result, for example `ptrtoint (ptr addrspace(4) @fn to i32)`. The emitted
  // byte-select relocations would serialize that CODE capability into a v1
  // object even though the outer expression itself is i32. Walk the complete
  // constant operand tree; callers exempt only an exact direct Function callee
  // or the validated direct-AS6 memory base above.
  static bool hasV1ObjectCompatibleConstant(
      const Constant *C, SmallPtrSetImpl<Type *> &SeenTypes,
      SmallPtrSetImpl<const Constant *> &SeenConstants,
      bool AllowV2SlotAS = false) {
    if (!SeenConstants.insert(C).second)
      return true;
    if (!hasV1PointerTypes(C->getType(), SeenTypes, AllowV2SlotAS))
      return false;
    for (const Use &U : C->operands()) {
      if (!hasV1PointerTypes(U->getType(), SeenTypes, AllowV2SlotAS))
        return false;
      if (const auto *Child = dyn_cast<Constant>(U.get()))
        if (!hasV1ObjectCompatibleConstant(Child, SeenTypes, SeenConstants,
                                           AllowV2SlotAS))
          return false;
    }
    return true;
  }

  //===--------------------------------------------------------------------===//
  // X3 placement support: pointer initializer leaves and the AS3/AS4
  // global-storage v1 representability walk.
  //===--------------------------------------------------------------------===//

  // The supported pointer leaf: exactly &global (a GlobalVariable or Function
  // in one of the placed storage address spaces 0/3/4) with one folded
  // constant addend -- either the legacy ConstantExpr Add form or, since
  // legal IR expresses pointer-plus-constant as a GEP, a getelementptr whose
  // base is a GlobalVariable and whose indices all fold to constants under
  // the DataLayout -- or a null pointer (a fully defined zero image that
  // serializes no capability). GEP null, non-constant indices, inttoptr,
  // ptrtoint, addrspacecast and all other expression algebra stay rejected,
  // in the style of the existing conservative support checks. \return the
  // base symbol and the folded addend on success.
  static bool isSupportedPointerLeaf(const Constant *C, const DataLayout &DL,
                                     const GlobalValue *&Base,
                                     int64_t &Addend) {
    Base = nullptr;
    Addend = 0;
    if (isa<ConstantPointerNull>(C))
      return DL.getTypeStoreSize(C->getType()) == 4;
    const Value *BaseV = nullptr;
    if (isa<GlobalValue>(C)) {
      BaseV = C;
    } else if (const auto *CE = dyn_cast<ConstantExpr>(C);
               CE && CE->getOpcode() == Instruction::Add &&
               CE->getNumOperands() == 2) {
      const Value *L = CE->getOperand(0);
      const Value *R = CE->getOperand(1);
      if (isa<GlobalValue>(L) && isa<ConstantInt>(R)) {
        BaseV = L;
        Addend = cast<ConstantInt>(R)->getSExtValue();
      } else if (isa<ConstantInt>(L) && isa<GlobalValue>(R)) {
        BaseV = R;
        Addend = cast<ConstantInt>(L)->getSExtValue();
      }
    } else if (const auto *GEP = dyn_cast<GEPOperator>(C)) {
      // X3-R4: the sanctioned form of "&global + constant". Only a GEP
      // directly over a GlobalVariable with fully constant indices folds; a
      // GEP over null, over any cast (which could launder an address space),
      // or with a non-constant index is not an object identity and keeps
      // the rejection. The folded offset must stay inside the 24-bit
      // effective-address discipline (any base plus such an addend that
      // would resolve into [0,0xffffff] needs an addend in
      // [-0xffffff,+0xffffff]; anything wider can never link legally, and
      // the linker's pointer-interval gate re-validates the final value).
      BaseV = GEP->getPointerOperand();
      if (!isa<GlobalVariable>(BaseV))
        return false;
      const unsigned AS = GEP->getPointerAddressSpace();
      APInt Offset(DL.getIndexSizeInBits(AS), 0);
      if (!GEP->accumulateConstantOffset(DL, Offset))
        return false; // non-constant index: a runtime pointer, not a leaf
      if (!Offset.isSignedIntN(32) ||
          Offset.sgt(0xffffff) || Offset.slt(int64_t(-0xffffff)))
        return false; // outside the 24-bit effective-address discipline
      Addend = Offset.getSExtValue();
    }
    if (!BaseV)
      return false;
    if (auto *F = dyn_cast<Function>(BaseV)) {
      Base = F;
    } else if (auto *GV = dyn_cast<GlobalVariable>(BaseV)) {
      unsigned AS = GV->getAddressSpace();
      if (AS != 0 && AS != 3 && AS != 4)
        return false; // target object has no placed storage class
      Base = GV;
    } else {
      return false;
    }
    // The 24-bit relocation channel needs the full 32/8 pointer container.
    return DL.getTypeStoreSize(C->getType()) == 4;
  }

  // v1 representability of a global INITIALIZER (X3). Admits aggregates of
  // the emittable leaves: integers, zero images, and pointer leaves of the
  // exact form above. The operand trees of arbitrary ConstantExprs are
  // deliberately NOT walked for leaf admission: a cast or arithmetic
  // expression that merely ends in a plain pointer type must not launder an
  // AS3/AS4 capability into a v1 object (the isr-object ESCAPE cases pin
  // exactly this). The callers use this walk only as a widening superset of
  // hasV1ObjectCompatibleConstant, never as a replacement; it keeps its own
  // seen-set so a constant rejected by the first walk can never look
  // "already verified" here.
  static bool hasV1PlacementInitializer(const Constant *C, const DataLayout &DL) {
    SmallPtrSet<const Constant *, 32> Seen;
    return hasV1PlacementInitializerImpl(C, DL, Seen);
  }

  static bool hasV1PlacementInitializerImpl(
      const Constant *C, const DataLayout &DL,
      SmallPtrSetImpl<const Constant *> &Seen) {
    if (!Seen.insert(C).second)
      return true;
    if (isa<ConstantAggregateZero>(C) || isa<ConstantInt>(C))
      return true;
    Type *Ty = C->getType();
    if (isa<PointerType>(Ty)) {
      const GlobalValue *Base;
      int64_t Addend;
      return isSupportedPointerLeaf(C, DL, Base, Addend);
    }
    unsigned Elements = 0;
    if (auto *AT = dyn_cast<ArrayType>(Ty))
      Elements = AT->getNumElements();
    else if (auto *ST = dyn_cast<StructType>(Ty))
      Elements = ST->isOpaque() ? 0 : ST->getNumElements();
    else
      return false; // casts, ptrtoint, undef, ...: not an emittable leaf
    if (!Elements)
      return false;
    for (unsigned I = 0; I != Elements; ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (!Element || !hasV1PlacementInitializerImpl(Element, DL, Seen))
        return false;
    }
    return true;
  }

  //===--------------------------------------------------------------------===//
  // ISR campaign (T06). The A2.2 keepalive-root helpers mirror the frozen
  // structure logic of the T01 IR Verifier; the object gate may exempt only
  // exactly verified keepalive paths, never a whole container by name or
  // section.
  //===--------------------------------------------------------------------===//

  // \return true when \p GV is the standard llvm.used keepalive container in
  // its full frozen structure: appending linkage, an array-of-pointers
  // initializer, the "llvm.metadata" section, and no ordinary use of the
  // container itself.
  static bool isMCS251KeepaliveRoot(const GlobalVariable &GV) {
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

  // \return the program-address-space function a verified keepalive member
  // keeps alive: the function reference itself (the "AS4 direct" form) or a
  // chain of single-operand no-op pointer casts (bitcast/addrspacecast, the
  // standard address-space adaptation of a used member) ending at one.
  // Anything else in a member position is not a verified keepalive item.
  static const Function *getKeepaliveFunction(const Constant *C) {
    while (true) {
      if (const auto *F = dyn_cast<Function>(C))
        return F;
      const auto *CE = dyn_cast<ConstantExpr>(C);
      if (!CE || CE->getNumOperands() != 1)
        return nullptr;
      unsigned Opcode = CE->getOpcode();
      if (Opcode != Instruction::BitCast &&
          Opcode != Instruction::AddrSpaceCast)
        return nullptr;
      C = cast<Constant>(CE->getOperand(0));
    }
  }

  static bool isISRDefinition(const Function &F) {
    return !F.isDeclaration() &&
           F.getCallingConv() == CallingConv::MCS251_INTR &&
           F.hasFnAttribute("mcs251-isr-vector");
  }

  // BT12: the keepalive container that registers bit-object placeholders. It is
  // the standard llvm.used / llvm.compiler.used structure (appending linkage,
  // array-of-pointers initializer, "llvm.metadata" section), and every member
  // must be a marked bit object (possibly behind a no-op pointer cast). A
  // container that also carries an ordinary global is NOT exempted here; the
  // ordinary global's own rejection stands.
  static bool isMCS251BitObjectKeepaliveRoot(const GlobalVariable &GV) {
    if (GV.getName() != "llvm.used" && GV.getName() != "llvm.compiler.used")
      return false;
    if (!GV.hasInitializer() || !GV.hasAppendingLinkage())
      return false;
    const auto *ArrTy = dyn_cast<ArrayType>(GV.getInitializer()->getType());
    if (!ArrTy || !ArrTy->getElementType()->isPointerTy())
      return false;
    if (!GV.hasSection() || GV.getSection() != "llvm.metadata")
      return false;
    if (!GV.materialized_use_empty())
      return false;
    for (unsigned I = 0, E = ArrTy->getNumElements(); I != E; ++I) {
      const Constant *Member = GV.getInitializer()->getAggregateElement(I);
      while (const auto *CE = dyn_cast_or_null<ConstantExpr>(Member)) {
        if (CE->getNumOperands() != 1 ||
            (CE->getOpcode() != Instruction::BitCast &&
             CE->getOpcode() != Instruction::AddrSpaceCast))
          return false;
        Member = cast<Constant>(CE->getOperand(0));
      }
      const auto *V = dyn_cast_or_null<GlobalVariable>(Member);
      if (!V || !MCS251::isBitObjectGlobal(*V))
        return false;
    }
    // Do not exempt an empty container (nothing to register).
    return ArrTy->getNumElements() != 0;
  }

  bool isV1ObjectCompatible(const Module &M) const {
    return scanObjectIdentity(M, /*V2Whitelist=*/false);
  }

  // A4/W3 (design §3.3), re-ruled by W3b (PM ruling 2026-09-13 #2): the
  // census of trailing pointer parameters.  Some function (definition or
  // declaration -- a cross-TU declaration creates slot references just like
  // a definition) carries a pointer formal at Index>0.  W3 used
  // "WhitelistedSlots present" as the TRIGGER that admitted a module to the
  // v2 identity; W3b deletes that trigger (the identity is the contract
  // generation).  What survives is the unregistered-capability half: a
  // trailing pointer in an address space outside the D.5 table can be a
  // static slot in no identity, v2 included.
  enum class StaticSlotScan { NoSlots, WhitelistedSlots, UnregisteredSlotAS };

  static StaticSlotScan scanStaticPointerSlots(const Module &M) {
    StaticSlotScan Result = StaticSlotScan::NoSlots;
    for (const Function &F : M) {
      unsigned Index = 0;
      for (const Argument &Arg : F.args()) {
        if (Index++ == 0)
          continue;
        auto *PT = dyn_cast<PointerType>(Arg.getType());
        if (!PT)
          continue;
        if (isD5StaticSlotAddressSpace(PT->getAddressSpace()))
          Result = StaticSlotScan::WhitelistedSlots;
        else
          return StaticSlotScan::UnregisteredSlotAS;
      }
    }
    return Result;
  }

  // The shared object-identity scan.  V2Whitelist=false is the historical
  // v1 compatibility walk, line for line (its bytes and verdicts must never
  // change); under W3b it serves the explicit v1 contract and the REL/asm
  // content boundary, which has no identity carrier.  V2Whitelist=true is
  // the A4 v2 CAPABILITY walk (PM ruling 2026-09-13 #2 decoupled it from
  // identity selection): the same globals and initializer rules (global
  // pointer storage keeps the exact v1 policy), with the function
  // signature/body pointer acceptance widened from AS0 to the D.5
  // static-slot set.  A module passing it is publishable under the
  // registered v2 identity -- whatever its content, slot-bearing or not.
  bool scanObjectIdentity(const Module &M, bool V2Whitelist) const {
    // P1-2: the contract is resolved target-side (feature string or the
    // llc command-line options), no longer through generic TargetOptions.
    const std::optional<MCS251::MemoryContract> &Resolved =
        getMCS251TM().getMemoryContract();
    if (!V2Whitelist) {
      if (!Resolved || !Resolved->isSpecified() || Resolved->ASLayoutVersion == 1)
        return true;
    } else {
      // The v2 identity exists only under a specified v2 layout contract.
      if (!Resolved || !Resolved->isSpecified() || Resolved->ASLayoutVersion != 2)
        return false;
    }
    const MCS251::MemoryContract &Contract = *Resolved;
    if (!V2Whitelist) {
      // Of the layout-v2 requests, only the 32-bit/InternalExtended profile can
      // be downgraded to the implemented v1 placement and pointer ABI. Tiny,
      // Small/InternalMovable and Large/ExternalData require v2 identity even if
      // this particular module happens not to define storage.
      if (M.getDataLayout().getPointerSizeInBits(0) != 32 ||
          Contract.DefaultPlacement != 8 || !M.alias_empty() ||
          !M.ifunc_empty())
        return false;
    } else {
      // A4/W3+W3b capability preconditions: 32-bit AS0, an
      // emission-registered memory model profile (XSmall placement 8 /
      // Small placement 1), and no aliases or ifuncs (they stay
      // unregistered v2 capabilities).
      if (M.getDataLayout().getPointerSizeInBits(0) != 32 ||
          !MCS251Attributes::isRegisteredA4Profile(32, Contract.DefaultPlacement) ||
          !M.alias_empty() || !M.ifunc_empty())
        return false;
      // W3b: the W3 TRIGGER ("the module must carry trailing pointer static
      // slots to become v2") is deleted by the ruling -- a v2-contract ELF
      // object is v2 regardless of content.  Only the
      // unregistered-capability half of the slot census survives: a
      // trailing pointer formal outside the D.5 table is not a static slot
      // in any identity.
      if (scanStaticPointerSlots(M) == StaticSlotScan::UnregisteredSlotAS)
        return false;
    }

    SmallPtrSet<Type *, 32> SeenTypes;
    SmallPtrSet<const Constant *, 32> SeenConstants;
    const DataLayout &DL = M.getDataLayout();
    for (const GlobalVariable &GV : M.globals()) {
      // T06 step 8(i): the only AS4-pointer exemption is a per-member path
      // check of a structurally verified llvm.used keepalive container (A2.2
      // "AS4 direct" or the standard single-operand cast chain). It applies
      // only to modules that actually define interrupt entries (T06 rework
      // R2): a module without ISR definitions keeps the original gate
      // behavior, so an AS4 cast root over ordinary functions is still
      // rejected by the ordinary walk below. The exemption is computed
      // without touching the shared seen-sets, so the same cast constant
      // escaping through an ordinary global or an instruction is still
      // rejected there. The container is never skipped by name or section
      // without this verification, and members that are not exactly
      // verified keepalive items (ordinary AS4 data or function pointers,
      // malformed chains) keep the original rejection.
      if (ModuleHasISRDefinitions && isMCS251KeepaliveRoot(GV)) {
        const auto *ArrTy = cast<ArrayType>(GV.getInitializer()->getType());
        for (unsigned I = 0, E = ArrTy->getNumElements(); I != E; ++I) {
          const Constant *Member = GV.getInitializer()->getAggregateElement(I);
          const Function *Kept = Member ? getKeepaliveFunction(Member) : nullptr;
          if (!Kept || Kept->getAddressSpace() !=
                           M.getDataLayout().getProgramAddressSpace())
            return false;
        }
        continue;
      }
      // X3: AS3 (__xdata) and AS4 (__code) globals are placed storage with
      // a v1 object protocol (per-object .mcs251.XSEG.* NOBITS sections plus
      // .mcs251.xdata_init records; CODE-space read-only images), so they no
      // longer make the module v2-only by themselves. Their value type must
      // be emittable storage, and an initializer is admitted only through
      // the original containment walk or the exact placement-leaf walk.
      // Pointer capabilities everywhere else keep the v2-only verdict.
      const unsigned GAS = GV.getAddressSpace();
      if (GAS != 0 && GAS != 3 && GAS != 4)
        return false;
      if (GAS != 0 && !isSupportedMutableType(GV.getValueType(), DL))
        return false;
      if (!hasV1PointerTypes(GV.getValueType(), SeenTypes) &&
          !isSupportedMutableType(GV.getValueType(), DL))
        return false;
      if (GV.hasInitializer() &&
          !hasV1ObjectCompatibleConstant(GV.getInitializer(), SeenTypes,
                                         SeenConstants) &&
          !hasV1PlacementInitializer(GV.getInitializer(), DL))
        return false;
    }
    for (const Function &F : M) {
      if (!hasV1PointerTypes(F.getFunctionType(), SeenTypes, V2Whitelist))
        return false;
      if (!V2Whitelist) {
        unsigned Index = 0;
        for (const Argument &Arg : F.args())
          if (Index++ && Arg.getType()->isPointerTy())
            return false; // v2-sized static pointer slot.
      }
      for (const BasicBlock &BB : F)
        for (const Instruction &I : BB) {
          if (!hasV1PointerTypes(I.getType(), SeenTypes, V2Whitelist))
            return false;
          for (const Use &U : I.operands()) {
            // The only direct-CODE exception is the exact Function value used
            // as the call operand. Do not strip casts or descend through a
            // ConstantExpr here: either would make a serialized CODE pointer
            // look like a direct ecall target.
            if (const auto *CB = dyn_cast<CallBase>(&I))
              if (&U == &CB->getCalledOperandUse() && isa<Function>(U.get()))
                continue;
            if (isDirectSFRMemoryOperand(U, I))
              continue;
            if (!hasV1PointerTypes(U->getType(), SeenTypes, V2Whitelist))
              return false;
            if (const auto *C = dyn_cast<Constant>(U.get()))
              if (!hasV1ObjectCompatibleConstant(C, SeenTypes,
                                                  SeenConstants, V2Whitelist))
                return false;
          }
        }
    }
    return true;
  }

  // A4/W2+W3: the v2 identity payload is assembled by the registered-value
  // codec (MCS251Attributes::renderRegisteredIdentity) and published as
  // `.mcs251.attributes` in emitEndOfAsmFile() when classifyModule() passes
  // a v2-contract ELF module through the capability gates
  // (PM ruling 2026-09-13 #2); e_flags=EFlagsV2 is installed on the ELF
  // writer before initSections so the v1 ABI note is never emitted next to
  // it.  No candidate value set can reach an object: the payload is built
  // from the registered constants alone and validated by decode() before
  // emission.

  void emitASxxxxText(const Twine &Text) {
    if (!usesELFObjects())
      OutStreamer->emitRawText(Text);
  }

  static bool isSupportedMutableType(Type *Ty, const DataLayout &DL) {
    if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32))
      return true;
    if (auto *PT = dyn_cast<PointerType>(Ty))
      // X3: pointer leaves are supported in 4-byte containers only -- the
      // initializer channel writes a big-endian 32-bit container whose low
      // 24 bits are the canonical address (zero most-significant byte at
      // container offset 0, 3-byte R_MCS251_24 field at offsets 1..3; the
      // 32/8 pointer ABI; the 16-bit contracts keep rejecting).
      return DL.getTypeStoreSize(PT) == 4;
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      return AT->getNumElements() &&
             isSupportedMutableType(AT->getElementType(), DL);
    }
    if (auto *ST = dyn_cast<StructType>(Ty)) {
      if (ST->isOpaque() || ST->getNumElements() == 0)
        return false;
      return llvm::all_of(ST->elements(), [&](Type *E) {
        return isSupportedMutableType(E, DL);
      });
    }
    return false;
  }

  static bool isSupportedMutableInitializer(const Constant *C,
                                            const DataLayout &DL) {
    Type *Ty = C->getType();
    if (!isSupportedMutableType(Ty, DL))
      return false;
    if (isa<ConstantAggregateZero>(C))
      return true;
    if (isa<ConstantInt>(C))
      return true;
    if (isa<PointerType>(Ty)) {
      const GlobalValue *Base;
      int64_t Addend;
      return isSupportedPointerLeaf(C, DL, Base, Addend);
    }
    if (!isa<ArrayType>(Ty) && !isa<StructType>(Ty))
      return false;
    unsigned Elements = Ty->isArrayTy()
                            ? cast<ArrayType>(Ty)->getNumElements()
                            : cast<StructType>(Ty)->getNumElements();
    for (unsigned I = 0; I != Elements; ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (!Element || !isSupportedMutableInitializer(Element, DL))
        return false;
    }
    return true;
  }

  void emitInitializerZeros(uint64_t Count) {
    // XINIT is a ROM image. ASxxxx `.ds` advances a CODE-area location
    // counter without materializing bytes, so emit literal zero bytes here.
    while (Count--)
      OutStreamer->emitIntValue(0, 1);
  }

  void emitMutableInitializer(const DataLayout &DL, const Constant *C) {
    Type *Ty = C->getType();
    if (isa<ConstantAggregateZero>(C)) {
      emitInitializerZeros(DL.getTypeStoreSize(Ty));
      return;
    }
    if (auto *CI = dyn_cast<ConstantInt>(C)) {
      OutStreamer->emitIntValue(CI->getZExtValue(),
                               DL.getTypeStoreSize(Ty));
      return;
    }
    if (isa<PointerType>(Ty)) {
      // X3: pointer leaf -- the 24-bit relocation channel.
      const GlobalValue *Base;
      int64_t Addend;
      bool Supported = isSupportedPointerLeaf(C, DL, Base, Addend);
      assert(Supported && "initializer rejected by the support check");
      (void)Supported;
      emitPointerInitializer(Base, Addend);
      return;
    }
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      uint64_t StoreSize = DL.getTypeStoreSize(AT->getElementType());
      uint64_t Stride = DL.getTypeAllocSize(AT->getElementType());
      for (unsigned I = 0; I != AT->getNumElements(); ++I) {
        emitMutableInitializer(DL, C->getAggregateElement(I));
        emitInitializerZeros(Stride - StoreSize);
      }
      return;
    }
    auto *ST = cast<StructType>(Ty);
    const StructLayout *Layout = DL.getStructLayout(ST);
    uint64_t Pos = 0;
    for (unsigned I = 0; I != ST->getNumElements(); ++I) {
      uint64_t Offset = Layout->getElementOffset(I);
      emitInitializerZeros(Offset - Pos);
      emitMutableInitializer(DL, C->getAggregateElement(I));
      Pos = Offset + DL.getTypeStoreSize(ST->getElementType(I));
    }
    emitInitializerZeros(DL.getTypeStoreSize(ST) - Pos);
  }

  // One relocated address byte: a zero placeholder plus a `.reloc`
  // association spelling the ELF relocation name (mapped to the target
  // fixup by the asm backend). The field resolves after the linker's full
  // symbol+addend sum; byte-of-24 associations never truncate.
  void emitRelocByte(StringRef Reloc, const MCSymbol *Sym) {
    MCSymbol *Field = OutContext.createTempSymbol();
    OutStreamer->emitLabel(Field);
    OutStreamer->emitIntValue(0, 1);
    OutStreamer->emitRelocDirective(*MCSymbolRefExpr::create(Field, OutContext),
                                    Reloc,
                                    MCSymbolRefExpr::create(Sym, OutContext));
  }

  // The 24-bit destination field of one `.mcs251.xdata_init` record: bank =
  // canonical bits [23:16] (exactly the value DPXL loads), window = canonical
  // bits [15:0], big-endian like every XINIT v1 u16 field. The three bytes
  // are the HI8/MID8/LO8 byte-of-24 channel of the object symbol -- never a
  // 16-bit truncation (the linker hard-errors R_MCS251_16/J16 against XSEG
  // symbols).
  void emitXDATAInitAddress(const MCSymbol *Sym) {
    emitRelocByte("R_MCS251_HI8", Sym);
    emitRelocByte("R_MCS251_MID8", Sym);
    emitRelocByte("R_MCS251_LO8", Sym);
  }

  // One 4-byte pointer container initialized to &global[+addend] (or null).
  // The container is a BIG-ENDIAN 32-bit image whose low 24 bits are the
  // canonical effective address (the X2 load sequence reads all four bytes
  // big-endian and routes bits [23:16] to DPXL, [15:8] to DPH, [7:0] to
  // DPL; bits [31:24] are not part of the address).  The layout is
  // therefore unambiguous: a literal zero byte (the always-zero most
  // significant byte) at container offset 0, then the R_MCS251_24 field
  // occupying container offsets 1..3 in the frozen big-endian order
  // (hi/bank at 1, mid at 2, lo at 3 -- the applyVectorJumps EJMP byte
  // order is the frozen truth).  After linking, a symbol at 0x011234
  // serializes as 00 01 12 34, never as 01 12 34 00.
  // A null leaf needs no relocation and is legal in every output mode; a
  // symbol leaf is reserved to the ELF object protocol (see
  // requiresELFPointerInitializers).
  void emitPointerInitializer(const GlobalValue *Base, int64_t Addend) {
    if (!Base) { // null pointer leaf: a fully defined zero image
      emitInitializerZeros(4);
      return;
    }
    // Bits [31:24] of the container are not part of the 24-bit effective
    // address: the most significant byte is a literal zero at offset 0.
    OutStreamer->emitIntValue(0, 1);
    MCSymbol *Field = OutContext.createTempSymbol();
    OutStreamer->emitLabel(Field);
    OutStreamer->emitIntValue(0, 3);
    const MCExpr *E = MCSymbolRefExpr::create(getSymbol(Base), OutContext);
    if (Addend)
      E = MCBinaryExpr::createAdd(E,
                                  MCConstantExpr::create(Addend, OutContext),
                                  OutContext);
    OutStreamer->emitRelocDirective(*MCSymbolRefExpr::create(Field, OutContext),
                                    "R_MCS251_24", E);
  }

  // X3: a pointer initializer whose base is a symbol serializes a 24-bit
  // relocation through the `.reloc` protocol, which exists only in ELF
  // objects (the REL writer and the assembly text have no such record).
  // Null leaves carry no relocation and stay legal everywhere.
  static bool hasSymbolPointerLeaf(const Constant *C, const DataLayout &DL) {
    if (isa<PointerType>(C->getType())) {
      const GlobalValue *Base;
      int64_t Addend;
      return isSupportedPointerLeaf(C, DL, Base, Addend) && Base != nullptr;
    }
    unsigned Elements = 0;
    if (auto *AT = dyn_cast<ArrayType>(C->getType()))
      Elements = AT->getNumElements();
    else if (auto *ST = dyn_cast<StructType>(C->getType()))
      Elements = ST->isOpaque() ? 0 : ST->getNumElements();
    else
      return false;
    for (unsigned I = 0; I != Elements; ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (Element && hasSymbolPointerLeaf(Element, DL))
        return true;
    }
    return false;
  }

  void requireELFPointerInitializers(const GlobalVariable *GV,
                                     const Constant *Init,
                                     const DataLayout &DL) const {
    if ((getMCS251TM().emitsObjectFile() && usesELFObjects()) ||
        !hasSymbolPointerLeaf(Init, DL))
      return;
    report_fatal_error("MCS251: global '" + GV->getName() +
                       "': a pointer initializer requires ELF object output "
                       "(-filetype=obj -mcs251-object-format=elf)");
  }

  // Read-only CSEG data: i8/i16/i32 scalars and (possibly nested) arrays of
  // them, nonempty at every level, every leaf a ConstantInt.  X3 extends the
  // accepted leaf set with pointer initializers (&global leaves through the
  // 24-bit relocation channel) and, for CODE-space objects, the ROM zero
  // image (legal in CODE space; X3 ruling: uninitialized/tentative __code
  // definitions become a zero image).  Struct aggregates, undef elements and
  // all other initializer expression relocations are rejected here and
  // reported by the caller's policy message.
  static bool isSupportedROInitializer(const Constant *C, const DataLayout &DL,
                                       bool AllowZeroImage) {
    Type *Ty = C->getType();
    if (isa<ConstantInt>(C))
      return Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32);
    if (isa<ConstantAggregateZero>(C))
      return AllowZeroImage;
    if (isa<PointerType>(Ty)) {
      const GlobalValue *Base;
      int64_t Addend;
      return isSupportedPointerLeaf(C, DL, Base, Addend);
    }
    auto *AT = dyn_cast<ArrayType>(Ty);
    if (!AT || !AT->getNumElements() ||
        (!isa<ConstantDataArray>(C) && !isa<ConstantArray>(C)))
      return false;
    for (unsigned I = 0; I != AT->getNumElements(); ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (!Element || !isSupportedROInitializer(Element, DL, AllowZeroImage))
        return false;
    }
    return true;
  }

  // Emits the CSEG byte image of a read-only initializer: scalars in the
  // established target (big-endian) memory order, nested arrays element by
  // element with stride padding (zero for the packed integer layouts this
  // path accepts), pointer leaves through the 24-bit relocation channel,
  // and the CODE-space zero image where the support check allowed one.
  // The image is always byte-aligned: any IR-level alignment above 1 on the
  // global is deliberately demoted, because MCS-251 needs no address
  // alignment for word accesses (QEMU + real hardware verified).
  void emitROInitializer(const DataLayout &DL, const Constant *C) {
    if (auto *CI = dyn_cast<ConstantInt>(C)) {
      OutStreamer->emitIntValue(CI->getZExtValue(),
                                DL.getTypeStoreSize(C->getType()));
      return;
    }
    if (isa<ConstantAggregateZero>(C)) {
      // Reachable only when the caller's support check allowed the zero
      // image (CODE-space objects).
      emitInitializerZeros(DL.getTypeStoreSize(C->getType()));
      return;
    }
    if (isa<PointerType>(C->getType())) {
      const GlobalValue *Base;
      int64_t Addend;
      bool Supported = isSupportedPointerLeaf(C, DL, Base, Addend);
      assert(Supported && "initializer rejected by the support check");
      (void)Supported;
      emitPointerInitializer(Base, Addend);
      return;
    }
    auto *AT = cast<ArrayType>(C->getType());
    uint64_t StoreSize = DL.getTypeStoreSize(AT->getElementType());
    uint64_t Stride = DL.getTypeAllocSize(AT->getElementType());
    for (unsigned I = 0; I != AT->getNumElements(); ++I) {
      emitROInitializer(DL, C->getAggregateElement(I));
      emitInitializerZeros(Stride - StoreSize);
    }
  }

public:
  static char ID;

  MCS251AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override {
    return "MCS251 Assembly Printer";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // T06 card steps 8/9: the final machine boundary is validated before any
    // byte of the function is emitted.
    verifyFinalMachineBoundary(MF);
    SetupMachineFunction(MF);
    emitParameterSlots(MF);
    emitFunctionBody();
    emitISRRecords(MF); // A3.5: ISR definitions, ELF object output only.
    return false;
  }

  // Final machine boundary. For ISR definitions every return must be the
  // dedicated RETI; ordinary functions must never contain RETI. A target
  // pseudo instruction reaching this stage is an unexpanded compiler bug and
  // is a hard error. Only *target* opcodes are checked: a blanket
  // MI->isPseudo() assert here would also fire on legitimate LLVM generic
  // pseudo instructions, which stay below INSTRUCTION_LIST_START.
  //
  // BT12: this is the machine-level boundary for a persistent bit-object
  // handle -- one layer of the handle invariant's enforcement, alongside the
  // module-level contract verifier (IR uses), the whole-module alias rejection
  // in emitStartOfAsmFile, and the constant-pool check below. A handle has no
  // byte address and is legal only as the bit-address operand of a
  // bit-addressed instruction. The check runs before any instruction of the
  // function is emitted, and covers every machine-operand spelling of the
  // symbol: MO_GlobalAddress (`@flag`), MO_ExternalSymbol (`&flag`, which the
  // mangler maps onto the same MCContext symbol) and MO_MCSymbol -- including
  // operands of a generic INLINEASM, whose asm-string distribution bypasses
  // MCS251MCInstLower entirely. A GlobalAlias resolving to a bit object never
  // reaches this point: the module-level alias rejection fires first. It does
  // NOT cover module-level standalone emission (e.g. raw metadata) or symbol
  // references that never appear as a machine operand or constant-pool entry;
  // those are the other layers' responsibility.

  // Resolve a machine operand to the bit-object global it names, or nullptr.
  // Identity is decided by resolving back to the GlobalValue (through the
  // module's symbol table), never by trusting the operand kind: `@flag` and
  // `&flag` are two spellings of one object.
  const GlobalVariable *getBitObjectHandle(const MachineOperand &MO) const {
    if (MO.isGlobal()) {
      const GlobalValue *G = MO.getGlobal();
      if (const auto *GV = dyn_cast<GlobalVariable>(G))
        return MCS251::isBitObjectGlobal(*GV) ? GV : nullptr;
      // A GlobalAddress operand may name the placeholder through an alias
      // whose aliasee is an arbitrary constant expression. getAliaseeObject()
      // is NOT a recursive containment check (an Add with base objects on
      // both sides, or a Sub with one on the right, yields nullptr), so the
      // defense-in-depth resolution here scans the whole expression for a
      // placeholder at any depth -- the same rule the module-level alias
      // rejection in emitStartOfAsmFile applies.
      if (const auto *GA = dyn_cast<GlobalAlias>(G)) {
        SmallPtrSet<const Constant *, 32> Seen;
        return findBitObjectInConstant(GA->getAliasee(), Seen);
      }
      return nullptr;
    }
    if (BitObjectSymbols.empty())
      return nullptr;
    const MCSymbol *Sym = nullptr;
    if (MO.isSymbol())
      Sym = GetExternalSymbolSymbol(MO.getSymbolName());
    else if (MO.isMCSymbol())
      Sym = MO.getMCSymbol();
    if (!Sym)
      return nullptr;
    auto It = BitObjectSymbols.find(Sym);
    return It != BitObjectSymbols.end() ? It->second : nullptr;
  }

  // \return the first bit-object placeholder reachable inside \p C, through
  // any depth of constant expressions (ptrtoint, add, sub, casts, aggregates).
  // A GlobalVariable is a TERMINAL: a marked one is the hit itself; an
  // ordinary one stops the walk, because its initializer is its own concern
  // (guarded by the module-level contract verifier), not part of this
  // expression's identity. A GlobalAlias is not terminal: its aliasee is an
  // operand, so reaching a placeholder through an alias is still found.
  static const GlobalVariable *
  findBitObjectInConstant(const Constant *C,
                          SmallPtrSetImpl<const Constant *> &Seen) {
    if (!Seen.insert(C).second)
      return nullptr;
    if (const auto *GV = dyn_cast<GlobalVariable>(C))
      return MCS251::isBitObjectGlobal(*GV) ? GV : nullptr;
    for (const Value *Op : C->operands()) {
      const auto *OC = dyn_cast<Constant>(Op);
      if (!OC)
        continue;
      if (const auto *GV = findBitObjectInConstant(OC, Seen))
        return GV;
    }
    return nullptr;
  }

  void verifyFinalMachineBoundary(const MachineFunction &MF) const {
    const Function &F = MF.getFunction();
    bool IsISR = isISRDefinition(F);
    for (const MachineBasicBlock &BB : MF)
      for (const MachineInstr &MI : BB) {
        // BT12: no bit-object handle may appear except as the bit-address
        // operand of a bit-addressed instruction. A generic INLINEASM (or any
        // other opcode) has no such operand, so any handle there is rejected.
        for (const MachineOperand &MO : MI.operands()) {
          const GlobalVariable *Handle = getBitObjectHandle(MO);
          if (!Handle)
            continue;
          if (!MCS251InstrInfo::isBitAddrOperand(MI.getOpcode(),
                                                 MO.getOperandNo()))
            report_fatal_error(
                "MCS251: bit object '" + Handle->getName() +
                "' may only be used as the bit-address operand of a bit "
                "instruction");
          if (MO.getOffset())
            report_fatal_error(
                "MCS251: bit object '" + Handle->getName() +
                "' bit-address operand must have no addend");
        }
        // Target opcodes begin right after the generic opcode range; a
        // remaining pseudo there is an unexpanded target pseudo.
        if (MI.isPseudo() &&
            MI.getOpcode() > (unsigned)TargetOpcode::GENERIC_OP_END) {
          StringRef Name =
              MF.getSubtarget().getInstrInfo()->getName(MI.getOpcode());
          report_fatal_error("MCS251: unexpanded target pseudo instruction '" +
                             Twine(Name) + "' reached the instruction emitter");
        }
        if (MI.getOpcode() == MCS251::RETI) {
          if (!IsISR)
            report_fatal_error(
                "MCS251: RETI is only valid inside an interrupt service "
                "routine; function '" +
                Twine(F.getName()) + "' is an ordinary function");
        } else if (IsISR && MI.isReturn()) {
          report_fatal_error("MCS251: interrupt service routine '" +
                             Twine(F.getName()) +
                             "' must return with RETI");
        }
      }

    // BT12: the machine constant pool is emitted (AsmPrinter::emitConstantPool
    // -> emitGlobalConstant) without passing through machine operands, so a
    // handle hidden inside a pool entry would silently become an ordinary
    // address relocation. A constant pool has no bit-address operand position
    // at all, so ANY appearance there is an escape, direct or nested.
    if (ModuleHasBitObjects)
      for (const MachineConstantPoolEntry &E :
           MF.getConstantPool()->getConstants()) {
        if (E.isMachineConstantPoolEntry())
          // This target never creates MachineConstantPoolValues; refusing the
          // shape outright keeps the layer closed if one ever appears.
          report_fatal_error(
              "MCS251: unsupported machine constant pool value in function '" +
              Twine(F.getName()) + "'");
        SmallPtrSet<const Constant *, 32> Seen;
        if (const GlobalVariable *GV =
                findBitObjectInConstant(E.Val.ConstVal, Seen))
          report_fatal_error("MCS251: bit object '" + GV->getName() +
                             "' may not appear in a constant pool entry");
      }
  }

  // A3.5: after emitFunctionBody, emit the frozen A3.2 24-byte metadata
  // records for every ISR definition -- exactly one ENTRY and one REGISTER
  // per definition, referencing the precise function symbol through the A3.4
  // type-9 RELA relocation. Emitted only for ISR definitions and only for
  // ELF object output.
  void emitISRRecords(MachineFunction &MF) {
    const Function &F = MF.getFunction();
    if (!isISRDefinition(F))
      return;
    if (!getMCS251TM().emitsObjectFile()) {
      // Assembly text is an inspection artifact: the module-level
      // .mcs251_isr_nonobject marker emitted by emitStartOfAsmFile already
      // disqualifies it from the production assembler. No records in text.
      return;
    }
    if (!usesELFObjects())
      // REL objects do not gain type-9 support; ISR object output on the REL
      // path is hard-rejected before any record is emitted.
      report_fatal_error("MCS251 ISR requires ELF object output");

    // A3.5 step 1: re-validate the ISR identity (CC, canonical slot) here at
    // the object boundary; the return-opcode boundary was checked in
    // verifyFinalMachineBoundary.
    Attribute VecAttr = F.getFnAttribute("mcs251-isr-vector");
    if (F.getCallingConv() != CallingConv::MCS251_INTR || !VecAttr.isValid())
      report_fatal_error("MCS251 ISR: calling convention and vector attribute "
                         "must appear together");
    uint64_t Slot = 0;
    if (!isCanonicalISRSlotText(VecAttr.getValueAsString(), Slot) ||
        !MCS251ISR::isLegalISRSlot(Slot))
      report_fatal_error("MCS251 ISR: vector is not a legal slot in profile "
                         "0-51");

    MCSection *Saved = OutStreamer->getCurrentSectionOnly();
    // A3.2: SHT_PROGBITS, flags 0, alignment 4, no entry size, big-endian
    // fields, no section header, no trailing padding.
    MCSectionELF *Meta = OutContext.getELFSection(
        MCS251ISR::MetaSectionName, ELF::SHT_PROGBITS, /*Flags=*/0);
    Meta->setAlignment(Align(MCS251ISR::MetaSectionAlignment));
    OutStreamer->switchSection(Meta);
    for (uint8_t Kind : {MCS251ISR::RK_ISR_ENTRY, MCS251ISR::RK_ISR_REGISTER}) {
      OutStreamer->emitIntValue(MCS251ISR::ProtocolVersion, 2);
      OutStreamer->emitIntValue(MCS251ISR::RecordSize, 2);
      OutStreamer->emitIntValue(Kind, 1);
      OutStreamer->emitIntValue(MCS251ISR::EK_IRQ_RETI, 1);
      OutStreamer->emitIntValue(MCS251ISR::HardwareProfileIRQ4, 1);
      OutStreamer->emitIntValue(MCS251ISR::SaveProfileINT37, 1);
      OutStreamer->emitIntValue(Slot, 2);
      OutStreamer->emitIntValue(MCS251ISR::RequiredCaps, 2);
      // symbol_reference stays four literal zero bytes: the A3.4 relocation
      // is a zero-write-width association with the exact function symbol, so
      // no byte of the 24B record (and nothing beyond it) is ever touched.
      OutStreamer->emitIntValue(0, 4);
      OutStreamer->emitRelocDirective(
          *MCConstantExpr::create(ISRRecordCount * MCS251ISR::RecordSize +
                                      MCS251ISR::RecordOffset::SymbolReference,
                                  OutContext),
          "R_MCS251_ISR_REF",
          MCSymbolRefExpr::create(getSymbol(&F), OutContext));
      OutStreamer->emitIntValue(MCS251ISR::AssetProfileCompiled, 4);
      OutStreamer->emitIntValue(0, 4);
      ++ISRRecordCount;
    }
    OutStreamer->switchSection(Saved);
  }

  //===--------------------------------------------------------------------===//
  // BT12: `.mcs251.bit` object-metadata records.
  //===--------------------------------------------------------------------===//

  // Validate a defined bit-object placeholder. The contract (BIT-OBJECT-
  // CONTRACT.md §3, kind 1) requires a named symbol that the linker can
  // resolve across TUs; the placeholder must not be weak/COMDAT/TLS/common and
  // must have no ordinary byte storage. An i8 value with an integer
  // initializer of 0 or 1 is the frozen shape.
  void verifyBitObjectGlobal(const GlobalVariable &GV) const {
    if (GV.isThreadLocal() || GV.getAddressSpace() != 0 || GV.hasSection() ||
        GV.hasComdat() ||
        (!GV.hasExternalLinkage() && !GV.hasLocalLinkage() &&
         !GV.hasPrivateLinkage()) ||
        GV.getVisibility() != GlobalValue::DefaultVisibility ||
        GV.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
        GV.hasCommonLinkage())
      report_fatal_error("MCS251 bit object '" + GV.getName() +
                         "': unsupported placement or linkage");
    if (!GV.getValueType()->isIntegerTy(8))
      report_fatal_error("MCS251 bit object '" + GV.getName() +
                         "': placeholder must be an i8 global");
    if (!GV.isDeclaration()) {
      const Constant *Init = GV.getInitializer();
      const auto *CI = dyn_cast_or_null<ConstantInt>(Init);
      if (!CI || CI->getValue().ugt(1))
        report_fatal_error("MCS251 bit object '" + GV.getName() +
                           "': initializer must be the constant 0 or 1");
    }
  }

  // Emit the single `.mcs251.bit` section of this object: one 8-byte big-endian
  // kind-1 record per defined bit-object placeholder, in module order. Each
  // record's symbol_reference is four literal zero bytes associated with the
  // exact defining symbol by a zero-width R_MCS251_BIT_REF at record base + 4.
  // The defining symbol is a label at the record base, so its st_value is the
  // record's byte offset (contract §3); it is an STT_OBJECT of size 1.
  void emitBitObjectRecords(Module &M) {
    SmallVector<const GlobalVariable *, 8> Defs;
    for (const GlobalVariable &GV : M.globals()) {
      if (!MCS251::isBitObjectGlobal(GV))
        continue;
      // Validate every marked global, definition or declaration, and give each
      // one the contract's STT_OBJECT identity. A declaration (extern bit) is a
      // cross-TU use: it is referenced through R_MCS251_BITADDR8 but carries no
      // record of its own (contract §3).
      verifyBitObjectGlobal(GV);
      MCSymbol *Sym = getSymbol(&GV);
      if (usesELFObjects())
        OutStreamer->emitSymbolAttribute(Sym, MCSA_ELF_TypeObject);
      if (GV.isDeclaration()) {
        emitLinkage(&GV, Sym);
        continue;
      }
      Defs.push_back(&GV);
    }
    // An extern-only TU (all marked globals are declarations) carries no
    // record of its own: the linker rejects an empty `.mcs251.bit` section, so
    // emitting no section keeps the object well-formed. It still carries one
    // BITADDR8 reference per symbolic use.
    if (Defs.empty())
      return;
    if (!getMCS251TM().emitsObjectFile() || !usesELFObjects())
      // The bit-object protocol exists only in the ELF object format; REL
      // objects and assembly text carry no record.
      report_fatal_error("MCS251 bit object requires ELF object output");

    MCSection *Saved = OutStreamer->getCurrentSectionOnly();
    // Contract §3: SHT_PROGBITS, sh_flags = 0, sh_addralign = 4, sh_entsize 0.
    MCSectionELF *Meta = OutContext.getELFSection(
        MCS251Bit::MetaSectionName, ELF::SHT_PROGBITS, /*Flags=*/0);
    Meta->setAlignment(Align(MCS251Bit::MetaSectionAlignment));
    OutStreamer->switchSection(Meta);
    for (const GlobalVariable *GV : Defs) {
      MCSymbol *Sym = getSymbol(GV);
      emitLinkage(GV, Sym);
      OutStreamer->emitELFSize(Sym, MCConstantExpr::create(1, OutContext));
      OutStreamer->emitLabel(Sym);
      const auto *CI = cast<ConstantInt>(GV->getInitializer());
      OutStreamer->emitIntValue(MCS251Bit::ProtocolVersion, 1);
      OutStreamer->emitIntValue(MCS251Bit::RK_DEFINITION, 1);
      OutStreamer->emitIntValue(CI->getValue().getZExtValue(), 1);
      OutStreamer->emitIntValue(MCS251Bit::Capabilities, 1);
      // symbol_reference stays four literal zero bytes; the association is the
      // zero-write-width R_MCS251_BIT_REF relocation below.
      OutStreamer->emitIntValue(0, 4);
      OutStreamer->emitRelocDirective(
          *MCConstantExpr::create(BitRecordCount * MCS251Bit::RecordSize +
                                      MCS251Bit::RecordOffset::SymbolReference,
                                  OutContext),
          "R_MCS251_BIT_REF", MCSymbolRefExpr::create(Sym, OutContext));
      ++BitRecordCount;
    }
    OutStreamer->switchSection(Saved);
  }

  void emitParameterSlots(const MachineFunction &MF) {
    const Function &F = MF.getFunction();
    if (F.arg_size() < 2)
      return;
    if (!F.hasLocalLinkage() && !F.hasExternalLinkage())
      report_fatal_error("MCS251: static parameter slots require local or "
                         "external function linkage");
    // SDCC overlays leaf functions only. Non-leaf slots must survive nested
    // calls, including calls into independently compiled SDCC modules.
    bool Leaf = true;
    for (const MachineBasicBlock &BB : MF)
      for (const MachineInstr &MI : BB)
        if (MI.isCall() || MI.isInlineAsm())
          Leaf = false;
    std::string Area = Leaf ? "OSEG" : "DSEG";
    unsigned Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE;
    if (Leaf && usesELFObjects())
      Flags |= ELF::SHF_MCS251_OVERLAY;
    MCSection *Sec = OutContext.getELFSection(
        ".mcs251." + Area + "." + Twine(MF.getFunctionNumber()),
        ELF::SHT_NOBITS, Flags);
    OutStreamer->switchSection(Sec);
    emitASxxxxText("\t.area " + Area +
                    (Leaf ? " (OVR,DATA)" : " (DATA)"));
    unsigned I = 0;
    for (const Argument &Arg : F.args()) {
      if (I++ == 0)
        continue;
      Type *Ty = Arg.getType();
      if (auto *PT = dyn_cast<PointerType>(Ty)) {
        unsigned AS = PT->getAddressSpace();
        if (AS == 5 || AS == 6 || AS == 7 || AS > 9)
          report_fatal_error("MCS251: pointer parameter address space has no "
                             "ordinary static-slot ABI");
      } else if (!Ty->isIntegerTy(8) && !Ty->isIntegerTy(16) &&
                 !Ty->isIntegerTy(32) && !Ty->isFloatTy()) {
        // f32 binary32 payloads use the i32 static-slot layout, matching the
        // DPL:DPH:B:A first-argument/return ABI.
        report_fatal_error("MCS251: static parameters require i8/i16/i32/f32 "
                           "or an ordinary data/CODE pointer");
      }
      uint64_t SlotSize = F.getDataLayout().getTypeStoreSize(Ty).getFixedValue();
      MCSymbol *Slot = OutContext.getOrCreateSymbol(
          getSymbol(&F)->getName() + "_PARM_" + Twine(I));
      if (!F.hasLocalLinkage())
        OutStreamer->emitSymbolAttribute(Slot, MCSA_Global);
      OutStreamer->emitLabel(Slot);
      if (usesELFObjects()) {
        OutStreamer->emitSymbolAttribute(Slot, MCSA_ELF_TypeObject);
        OutStreamer->emitELFSize(
            Slot, MCConstantExpr::create(SlotSize, OutContext));
      }
      OutStreamer->emitZeros(SlotSize);
    }
    OutStreamer->switchSection(OutContext.getObjectFileInfo()->getTextSection());
    emitASxxxxText("\t.area CSEG (CODE)");
  }

  void emitInstruction(const MachineInstr *MI) override {
    // Merely taking a function's address must not create undefined references
    // to its parameter slots. Declare only slots actually referenced by code.
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isSymbol())
        continue;
      MCSymbol *Sym = GetExternalSymbolSymbol(MO.getSymbolName());
      if (!LocalParameterSlots.contains(Sym->getName()) &&
          DeclaredExternalSymbols.insert(Sym->getName()).second)
        OutStreamer->emitSymbolAttribute(Sym, MCSA_Global);
    }
    // P-4: backend-generated external libcalls surface here as
    // MO_ExternalSymbol operands of a call (never as an IR function, so the
    // module-wide coverage check above cannot see them).  Collect the final
    // ELF symbol name of every external-symbol call target that is not a
    // static parameter slot; a registered helper ABI becomes a Tag 28 record,
    // an unregistered one is a hard error at finalization.
    if (FunctionSignaturesBuilt) {
      for (const MachineOperand &MO : MI->operands()) {
        if (!MO.isSymbol())
          continue;
        MCSymbol *Sym = GetExternalSymbolSymbol(MO.getSymbolName());
        StringRef Name = Sym->getName();
        if (LocalParameterSlots.contains(Name))
          continue;
        if (Name.contains("_PARM_"))
          continue; // a static parameter slot of any callee, never a function
        PendingHelperSymbols.insert(Name);
      }
    }
    MCS251_MC::verifyInstructionPredicates(MI->getOpcode(),
                                           getSubtargetInfo().getFeatureBits());

    MCS251MCInstLower MCInstLowering(*this);
    MCInst TmpInst;
    MCInstLowering.Lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
  }

  /// Classify the module: select the object identity and enforce the
  /// fail-closed capability gates.
  ///
  /// A4/W3b (PM ruling 2026-09-13 #2, "V1 is deprecated; the default
  /// considers V2 only"): the ELF object identity is the CONTRACT
  /// GENERATION, never the module content.  A specified ASLayoutVersion==2
  /// contract (the materialized llc/clang default included) publishing an
  /// ELF object emits the v2 identity (e_flags=0x102 +
  /// `.mcs251.attributes`) whatever the module contains; the W3 rule that
  /// a "v1-representable" module under a v2 contract downgrades to the v1
  /// identity is DELETED.  The v1 identity bytes are produced only under an
  /// explicit ASLayoutVersion==1 contract (1,1,32,8,1).
  ///
  /// Content still gates fail-closed, but as CAPABILITIES, decoupled from
  /// the identity selection (scanObjectIdentity(V2Whitelist=true)):
  /// alias/ifunc, 16-bit objects (the TargetMachine gate), unregistered
  /// model profiles (Large), a trailing pointer formal outside the D.5
  /// static-slot set, static pointer initializer algebra and any capability
  /// outside the registration keep the fatal, and the diagnostic names the
  /// unregistered-capability reason.
  ///
  /// The REL/asm boundary is unchanged: it has no identity carrier, so the
  /// historical CONTENT determination stays (downgradable content keeps
  /// emitting the v1-era REL/asm bytes; v2-only content is fatal).
  ///
  /// The routine is idempotent: emitStartOfAsmFile re-asserts the same facts,
  /// which matters because a MIR entry point (-start-after...) reaches the
  /// AsmPrinter without going through doInitialization.
  ///
  /// Returns true when the module is representable by the v1 object identity.
  bool classifyModule(Module &M) {
    // ISR identity of the module decides where the A2.2 keepalive exemption
    // applies (object gate, storage-reservation scan, global emission).
    ModuleHasISRDefinitions =
        llvm::any_of(M, [](const Function &F) { return isISRDefinition(F); });
    // BT12: a persistent bit object is object identity, not byte storage.
    ModuleHasBitObjects = llvm::any_of(M.globals(), [](const GlobalVariable &GV) {
      return MCS251::isBitObjectGlobal(GV);
    });

    ModuleV2Identity = false;
    // P1-2: the contract is resolved target-side.  The identity half of the
    // ruling keys off its layout generation alone.
    const std::optional<MCS251::MemoryContract> &Resolved =
        getMCS251TM().getMemoryContract();
    const bool V2Contract =
        Resolved && Resolved->isSpecified() && Resolved->ASLayoutVersion == 2;
    if (V2Contract && getMCS251TM().emitsObjectFile() && usesELFObjects()) {
      // ELF object output under a v2 contract: v2 identity, content-agnostic,
      // gated only by the registered-capability walk.
      if (!scanObjectIdentity(M, /*V2Whitelist=*/true))
        report_fatal_error(
            "MCS251: module uses an ABI capability outside the registered "
            "A4 v2 object identity (32-bit AS0 XSmall/Small modules with "
            "the D.5 pointer address spaces 0/1/2/3/4/8/9 in static slots, "
            "signatures and function bodies; alias/ifunc, 16-bit objects, "
            "other pointer address spaces, unregistered model profiles and "
            "static pointer initializer algebra stay unregistered)");
      enterV2ObjectMode();
      return false;
    }

    // The v1 identity (explicit v1 contract) and the REL/asm content
    // boundary keep the historical determination byte for byte.
    const bool V1Compatible = isV1ObjectCompatible(M);
    if (!V1Compatible && getMCS251TM().emitsObjectFile()) {
      // REL object output carrying v2-only content: keep the fail-closed
      // boundary.  Content v2-only through whitelisted static slots reaches
      // enterV2ObjectMode, whose REL carrier check is the historical fatal;
      // any other v2-only shape fails the capability walk here.
      if (!scanObjectIdentity(M, /*V2Whitelist=*/true))
        report_fatal_error(
            "MCS251: module uses an ABI capability that cannot be represented "
            "by the v1 relocatable-object identity; v2 object output is not "
            "implemented for this unregistered capability (outside the A4 "
            "whitelist: 32-bit AS0 modules whose v2-only capabilities are "
            "pointer static slots in the D.5 address spaces 0/1/2/3/4/8/9 "
            "plus already-supported capabilities; alias/ifunc, 16-bit "
            "objects, other address spaces and static pointer initializer "
            "algebra stay unregistered)");
      enterV2ObjectMode();
    }
    return V1Compatible;
  }

  //===--------------------------------------------------------------------===//
  // P-4 (freeze 2026-09-14): function-signature collection.
  //
  // clang publishes one `!mcs251.signatures` node per external-linkage
  // function the TU declares or defines, carrying the SOURCE-typed signature
  // (the AST still knows `bit` from `unsigned char`).  llc must never
  // re-derive a signature from the i8 boundary, so this reader is the only
  // producer of Tag 28 records, plus the registered-helper ABI table for
  // backend-generated external libcalls.
  //===--------------------------------------------------------------------===//

  /// Read one i32 operand, or std::nullopt when the operand is missing or not
  /// an integer constant.  A malformed node is a hard error at the caller.
  static std::optional<uint64_t> metadataU32(const MDNode &N, unsigned I) {
    if (I >= N.getNumOperands())
      return std::nullopt;
    const auto *CI = mdconst::dyn_extract<ConstantInt>(N.getOperand(I));
    if (!CI || CI->getBitWidth() > 64)
      return std::nullopt;
    return CI->getZExtValue();
  }

  /// Decode one metadata node into a Table record.  Returns an error string
  /// (empty on success) so the caller can fold in the node index.
  std::string decodeMetadataNode(const MDNode &N,
                                 MCS251Signatures::Record &Out) const {
    // Mandatory operands are name/role/ret; the source-parameter bit-ness
    // operands follow, so a zero-parameter function is a legal 3-operand node.
    if (N.getNumOperands() < MCS251Signatures::MetadataOperandFirstParam)
      return "node has fewer than the 3 mandatory operands "
             "(name, role, return bit-ness)";
    const auto *NameMD = dyn_cast<MDString>(N.getOperand(
        MCS251Signatures::MetadataOperandName));
    if (!NameMD || NameMD->getString().empty())
      return "operand 0 must be a non-empty MDString (the final ELF symbol)";
    std::optional<uint64_t> Role = metadataU32(
        N, MCS251Signatures::MetadataOperandRole);
    std::optional<uint64_t> Ret = metadataU32(
        N, MCS251Signatures::MetadataOperandRet);
    if (!Role || *Role > 0xff)
      return "operand 1 must be an i32 role byte";
    if (!Ret || *Ret > 1)
      return "operand 2 must be the i32 return bit-ness (0 or 1)";

    StringRef Name = NameMD->getString();
    // The name is FINAL: a `\01` asm-label escape is stripped by the producer
    // (clang's getMangledName returns the bare target name there), so the only
    // rule llc enforces is that the string is a plain symbol -- never a raw
    // source spelling and never a second prefix.
    uint8_t RoleByte = uint8_t(*Role);
    uint8_t Def = RoleByte & MCS251Signatures::Role_DefinitionMask;
    if (Def != MCS251Signatures::Role_HasDefinition &&
        Def != MCS251Signatures::Role_DeclaredNotDefined)
      return ("role bits 0..1 must be 1 (definition) or 2 (declaration), got " +
              Twine(unsigned(Def))).str();
    if (RoleByte & MCS251Signatures::Role_ReservedMask)
      return "role bits 4..7 are reserved and must be zero";

    const bool NoProto = MCS251Signatures::hasNoPrototype(RoleByte);
    const bool Variadic = MCS251Signatures::isVariadic(RoleByte);
    if (NoProto && Variadic)
      return "a K&R no-prototype record cannot also be variadic";

    const unsigned ParamCount =
        N.getNumOperands() - MCS251Signatures::MetadataOperandFirstParam;
    if (ParamCount > 255)
      return "more than 255 source parameters is not representable";
    if (NoProto && ParamCount != 0)
      return "a K&R no-prototype record must carry no source parameters";

    std::vector<uint8_t> Bitmap(MCS251Signatures::bitmapBytes(
                                    uint8_t(ParamCount)),
                                0);
    for (unsigned I = 0; I != ParamCount; ++I) {
      std::optional<uint64_t> Bit = metadataU32(
          N, MCS251Signatures::MetadataOperandFirstParam + I);
      if (!Bit || *Bit > 1)
        return ("source parameter " + Twine(I) +
                " must carry an i32 bit-ness (0 or 1)")
                   .str();
      MCS251Signatures::bitmapSet(Bitmap, I, *Bit != 0);
    }

    // The embedded call_abi generation must equal this object's identity.
    Out = MCS251Signatures::makeRecord(
        Name, Def == MCS251Signatures::Role_HasDefinition,
        ParamCount, Bitmap, *Ret != 0, NoProto, Variadic,
        uint8_t(MCS251Attributes::CallABIMajor),
        uint8_t(MCS251Attributes::CallABIMinor));
    return std::string();
  }

  /// Assemble FunctionSignatures from the module's `!mcs251.signatures`
  /// metadata plus the registered-helper records, enforcing the freeze's
  /// "clang 在发射前校验全部源外部声明/定义均已登记" on the llc side too:
  /// every in-scope external function of the final module must be covered.
  void buildFunctionSignatures(Module &M) {
    FunctionSignatures = MCS251Signatures::Table();
    FunctionSignatureNames.clear();
    FunctionSignaturesBuilt = true;

    NamedMDNode *MD = M.getNamedMetadata(MCS251Signatures::MetadataName);
    if (!MD)
      report_fatal_error(
          "MCS251: a v2 object requires `!mcs251.signatures` metadata; a "
          "module without it cannot produce a legal v2 identity (hand-written "
          "IR authors must provide it explicitly)");

    for (unsigned I = 0, E = MD->getNumOperands(); I != E; ++I) {
      const MDNode *N = MD->getOperand(I);
      if (!N)
        report_fatal_error("MCS251: `!mcs251.signatures` operand " + Twine(I) +
                           " is null");
      MCS251Signatures::Record R;
      std::string Err = decodeMetadataNode(*N, R);
      if (!Err.empty())
        report_fatal_error("MCS251: `!mcs251.signatures` node " + Twine(I) +
                           " is malformed: " + Err);
      if (!FunctionSignatureNames.insert(R.Name).second)
        report_fatal_error(Twine("MCS251: `!mcs251.signatures` carries "
                                 "duplicate function '") +
                           R.Name + "'");
      FunctionSignatures.Records.push_back(std::move(R));
    }

    // Freeze: the metadata must cover EVERY source external-linkage function
    // the TU declares or defines, not just the ones that survived to a
    // definition or a call.  llc checks the final module; clang checks the
    // AST before emitting.  Both must hold, so a node that is dropped or a
    // declaration the producer forgot is caught here.
    for (const Function &F : M) {
      if (F.isIntrinsic())
        continue;
      if (!F.hasExternalLinkage())
        continue; // local/internal functions are outside the signature domain
      std::string Final = getSymbolName(&F);
      if (!FunctionSignatureNames.contains(Final))
        report_fatal_error("MCS251: external function '" + F.getName() +
                           "' (ELF symbol '" + Final +
                           "') is missing from `!mcs251.signatures`; every "
                           "source external declaration and definition must "
                           "be registered");
    }
  }

  /// Fold a backend-generated external libcall into FunctionSignatures.
  /// \p FinalSymbol is the final ELF symbol; an unregistered ABI is the
  /// freeze's fail-closed "未登记 ABI 的外部 libcall 硬错".
  void recordHelperLibcall(StringRef FinalSymbol) {
    if (FunctionSignatureNames.contains(FinalSymbol))
      return; // already carried by the source metadata (or a previous helper).
    const MCS251::HelperABI *H = MCS251::lookupHelperABI(FinalSymbol);
    if (!H)
      report_fatal_error(
          "MCS251: backend-generated external libcall '" + FinalSymbol +
          "' has no registered helper ABI; refusing to emit a v2 object "
          "with an unregistered external helper signature");
    MCS251Signatures::Record R = MCS251Signatures::makeRecord(
        H->Symbol, /*IsDefinition=*/false, H->ParamCount, /*Bitmap=*/{},
        /*Ret=*/false, /*NoPrototype=*/false, /*Variadic=*/false,
        uint8_t(MCS251Attributes::CallABIMajor),
        uint8_t(MCS251Attributes::CallABIMinor));
    FunctionSignatureNames.insert(R.Name);
    FunctionSignatures.Records.push_back(std::move(R));
  }

  /// Finalize the table just before the carriers are published: append the
  /// helper records seen in the machine stream.  Names are kept unique.
  void finalizeFunctionSignatures() {
    if (!FunctionSignaturesBuilt)
      return;
    for (const auto &Entry : PendingHelperSymbols)
      recordHelperLibcall(Entry.getKey());
    PendingHelperSymbols.clear();
  }

  /// The final ELF symbol name of \p GV, exactly as the object will carry it
  /// (target mangling applied, `\01` escape stripped).  llc never re-prefixes
  /// a name; it reports what the mangler produced.
  ///
  /// Returns an owning std::string: the mangled name is derived into a local
  /// SmallString, so handing back a StringRef over that buffer would dangle
  /// (a short name reads freed stack, a long one freed heap).  Callers must
  /// keep the returned value alive for as long as they use the name.
  std::string getSymbolName(const GlobalValue *GV) const {
    SmallString<128> Buf;
    Mangler::getNameWithPrefix(Buf, GV->getName(), getDataLayout());
    return std::string(Buf);
  }

  // A4/W2: arm the v2 identity publication for this compilation.  Must run
  // before the ELF streamer's initSections (the codegen path guarantees
  // this: doInitialization classifies before calling the base class) so the
  // v1 note is suppressed and the header flags are already EFlagsV2.
  void enterV2ObjectMode() {
    if (!usesELFObjects())
      report_fatal_error(
          "MCS251: the v2 object identity (pointer static slots) requires "
          "ELF object output (-mcs251-object-format=elf -filetype=obj); the "
          "ASxxxx REL format has no v2 identity carrier");
    // Safe downcast: usesELFObjects() + object output is exactly the
    // combination for which MCS251TargetMachine::createMCStreamer built the
    // MCS251 ELF streamer (an MCELFStreamer subclass).
    auto &ES = *static_cast<MCELFStreamer *>(OutStreamer.get());
    ES.getWriter().setELFHeaderEFlags(MCS251Attributes::EFlagsV2);
    ModuleV2Identity = true;
  }

  void emitStartOfAsmFile(Module &M) override {
    // ASxxxx module prologue.  ".source" is emitted bare, exactly like the
    // validated specimen and the smoke crt0 template (sdas251 accepts it
    // without a file argument; a filename argument was never exercised).
    const std::string ModuleName = getMCS251ModuleName(M);
    // Re-assert the module classification.  doInitialization already ran it
    // before initSections; this second call keeps the MIR entry path
    // (-start-after..., which bypasses doInitialization) correct, and
    // classifyModule is idempotent.
    classifyModule(M);
    // Build the symbol-identity table every later boundary check resolves
    // through: one entry per placeholder, keyed by its MC symbol. `&flag` in
    // MIR mangles to the same MCContext symbol, so the ExternalSymbol and
    // GlobalAddress spellings of one object converge on one entry here.
    // P-4: assemble this module's function-signature table.  Only a v2
    // object carries Tag 28, so the metadata is mandatory exactly there; the
    // v1 identity and the REL/asm inspection artifacts never read it.
    if (ModuleV2Identity && getMCS251TM().emitsObjectFile()) {
      buildFunctionSignatures(M);
      PendingHelperSymbols.clear();
    }
    BitObjectSymbols.clear();
    if (ModuleHasBitObjects) {
      for (const GlobalVariable &GV : M.globals())
        if (MCS251::isBitObjectGlobal(GV))
          BitObjectSymbols.try_emplace(getSymbol(&GV), &GV);
      // A GlobalAlias resolving to a bit object is a second name for a handle
      // and the contract forbids it outright (a handle is identity, not a
      // byte address; an alias is a byte-address indirection). The IR-level
      // contract verifier rejects it on the normal path, but a MIR entry
      // (-start-after...) skips that verifier, so the alias is rejected here,
      // once per module, before the functions, the module inline asm, the
      // `.mcs251.bit` records and the alias itself are actually emitted.
      // (Earlier generic section initialization may already have run; the
      // claim is about the bit-object and alias emission, not about all
      // module initialization.) This covers every later spelling of the alias
      // symbol (`@alias`, `&alias`, inline asm, constant pool) in one place.
      //
      // The check scans the aliasee CONSTANT EXPRESSION for a placeholder at
      // any depth, NOT getAliaseeObject(): that helper resolves a single base
      // object and explicitly is not a containment check -- an Add with base
      // objects on both sides, or a Sub with one on the right, yields nullptr,
      // which would let an alias whose expression cancels back to (or
      // computes on) a placeholder slip through as an ordinary address.
      // Ordinary globals terminate the walk, so an alias to plain data is
      // untouched.
      for (const GlobalAlias &GA : M.aliases()) {
        SmallPtrSet<const Constant *, 32> Seen;
        const GlobalVariable *GV =
            findBitObjectInConstant(GA.getAliasee(), Seen);
        if (GV)
          report_fatal_error("MCS251: bit object '" + GV->getName() +
                             "' must not be aliased (alias '" +
                             GA.getName() + "')");
      }
    }
    // A4/W3b: a v2-identity module never claims the v1 ABI signature, not
    // even in the (ELF-swallowed) text prologue.  For REL/asm output the
    // historical content determination keeps deciding, unchanged.
    const bool V1Compatible = !ModuleV2Identity && isV1ObjectCompatible(M);
    emitASxxxxText("\t.module " + ModuleName);
    emitASxxxxText("\t.source");
    if (V1Compatible)
      emitASxxxxText(OptsdccSignature);
    else
      // Deliberately not accepted by ASxxxx: v2 assembly is an inspection
      // artifact and cannot be assembled into an identity-less or v1 object.
      emitASxxxxText("\t.mcs251_v2_nonobject");
    emitASxxxxText("");
    emitASxxxxText("\t.area CSEG (CODE)");

    // Object path (Phase 13a): the MCS251ObjectStreamer deliberately
    // swallows the raw-text prologue above and the REL writer regenerates
    // the equivalent M/O/A records.  The writer has no access to the Module,
    // so bridge the sanitized module name through the MCContext (the only
    // other MainFileName consumer is DWARF line-table setup, which this
    // target never enables).
    OutStreamer->getContext().setMainFileName(ModuleName);
    // A3.5: an ISR module's assembly text is an inspection artifact only.
    // The marker is deliberately not an ASxxxx directive, so the text can
    // never be assembled into a "production" object that carries the plain
    // ABI signature without the ISR registration protocol. The records
    // themselves are only ever emitted into ELF objects.
    if (!getMCS251TM().emitsObjectFile() && ModuleHasISRDefinitions)
      emitASxxxxText("\t.mcs251_isr_nonobject");
    ISRRecordCount = 0;
    BitRecordCount = 0;
    if (llvm::any_of(M, [](const Function &F) {
          return !F.isDeclaration() && F.arg_size() > 1;
        }) ||
        llvm::any_of(M.globals(), [this](const GlobalVariable &GV) {
          // T06 rework R1: precisely verified ISR keepalive metadata is
          // registration data and reserves no storage. The standard llvm.used
          // root has appending linkage and is NOT a constant, so the
          // exclusion needs this structural verification -- never a bare
          // name/section test. Real mutable globals keep triggering the
          // reservation exactly as before.
          // BT12: a persistent bit-object placeholder is also registration
          // data (identity, not bytes) and reserves no ordinary RAM byte, and
          // so is its verified keepalive container.
          return !GV.isDeclaration() && !GV.isConstant() &&
                 !MCS251::isBitObjectGlobal(GV) &&
                 !isMCS251BitObjectKeepaliveRoot(GV) &&
                 !(ModuleHasISRDefinitions && isMCS251KeepaliveRoot(GV));
        })) {
      // REL synthesizes the A record. ELF needs an actual NOBITS reservation.
      if (usesELFObjects()) {
        OutStreamer->switchSection(OutContext.getELFSection(
            ".mcs251.REG_BANK_0", ELF::SHT_NOBITS,
            ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_MCS251_OVERLAY));
        OutStreamer->emitZeros(8);
        OutStreamer->switchSection(
            OutContext.getObjectFileInfo()->getTextSection());
      }
      emitASxxxxText("\t.area REG_BANK_0 (OVR,DATA)\n\t.ds 8\n"
                      "\t.area CSEG (CODE)");
    }
    LocalParameterSlots.clear();
    DeclaredExternalSymbols.clear();
    for (const Function &F : M) {
      if (!F.hasLocalLinkage())
        continue;
      for (unsigned I = 1; I < F.arg_size(); ++I)
        LocalParameterSlots.insert(
            (getSymbol(&F)->getName() + "_PARM_" + Twine(I + 1)).str());
    }

    // BT12: the single `.mcs251.bit` section (if any bit-object placeholder is
    // defined) is emitted after the module prologue so its label/symbol
    // attributes do not disturb the CSEG ordering. Declarations (extern bit)
    // are validated here but carry no record; their uses are handled by the
    // symbolic BITADDR8 path in the code emitter.
    if (ModuleHasBitObjects)
      emitBitObjectRecords(M);
  }

  // A4/W2+W3b: publish the v2 identity section for a v2-contract ELF object
  // that passed the capability gate.  The payload is assembled by the
  // registered-value codec and validated by the strict decoder before a
  // byte is emitted (design §3.2): every required tag exactly once and
  // Critical, every value the approved registered one, so no candidate
  // combination can reach an object.  The ARM-attributes envelope (0x41 /
  // BE32 VendorSize=16+P / "MCS251\0" / scope tag 1 / BE32 ScopeSize=5+P)
  // is emitted by the common MCELFStreamer entry exactly as design §3.1
  // specifies; the section is SHT 0x70000003, non-ALLOC, alignment 1, and
  // it is the only v2 identity carrier: the v1 note was suppressed in
  // initSections because EFlagsV2 was installed first.
  void emitEndOfAsmFile(Module &M) override {
    if (!ModuleV2Identity)
      return; // v1 identity objects emit no extra identity bytes (unchanged).
    const std::optional<MCS251::MemoryContract> &Contract =
        getMCS251TM().getMemoryContract();
    if (!Contract || !Contract->isSpecified())
      report_fatal_error("MCS251: the v2 object identity requires a "
                         "specified v2 memory contract");
    // P-4: fold in the registered-helper records for any backend-generated
    // external libcall seen in the machine stream; an unregistered helper ABI
    // fails closed here, before any identity byte is rendered.
    finalizeFunctionSignatures();
    std::string SectionBytes = MCS251Attributes::renderRegisteredIdentity(
        Contract->AS0PointerBits, Contract->DefaultPlacement,
        FunctionSignatures);
    MCS251Attributes::Decoded Decoded;
    if (llvm::Error E = MCS251Attributes::decode(SectionBytes,
                                                 /*IsBigEndian=*/true, Decoded)) {
      std::string Msg = toString(std::move(E));
      report_fatal_error(Twine("MCS251: refusing to emit an unregistered v2 "
                               "object identity: ") +
                         Msg);
    }
    auto &ES = *static_cast<MCELFStreamer *>(OutStreamer.get());
    MCSection *AttributeSection = nullptr;
    // The codec renders the complete section (envelope + records); the
    // common MC entry builds the envelope itself from the vendor name, so
    // hand it only the self-describing record payload.  The envelope the
    // entry emits is byte-identical to the one stripped here.
    StringRef Records = StringRef(SectionBytes).substr(
        MCS251Attributes::EnvelopeSize);
    ES.emitSelfDescribingAttributesSection(
        MCS251Attributes::Vendor, MCS251Attributes::SectionName,
        MCS251Attributes::SectionType, AttributeSection, Records);
  }

  bool doInitialization(Module &M) override {
    // Run the identity classification *before* the base implementation: an
    // ELF object under a v2 contract must be armed for the v2 identity
    // (EFlagsV2 installed on the ELF writer) before any section or identity
    // carrier byte is emitted -- the base class calls initSections, which
    // emits the v1 note only when the flags word still announces v1.  A
    // module whose capabilities fall outside the registered v2 identity
    // fails here fail-closed (classifyModule).
    classifyModule(M);
    return AsmPrinter::doInitialization(M);
  }

  void emitGlobalVariable(const GlobalVariable *GV) override {
    // BT12: a persistent bit object is identity, not storage. It was already
    // turned into a kind-1 `.mcs251.bit` record (definition) or left as an
    // undefined reference (extern) by emitStartOfAsmFile; it must never reach
    // the ordinary DSEG/XINIT/CSEG paths as a byte object.
    if (MCS251::isBitObjectGlobal(*GV))
      return;

    if (GV->isDeclaration()) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    // ISR campaign T06 step 8(ii): for modules that define interrupt
    // entries, the verified keepalive metadata is registration data, not
    // global storage. (The standard llvm.used root has appending linkage and
    // is not a constant.) Route the structurally correct container through
    // the generic AsmPrinter, whose special-LLVM-global path consumes it
    // without emitting bytes, CODE pointer data, relocations, or DSEG/XINIT
    // records; the storage-reservation scan in emitStartOfAsmFile excludes
    // it as well. Modules without ISR definitions keep the original
    // rejection path unchanged (T06 rework R2).
    if (ModuleHasISRDefinitions && isMCS251KeepaliveRoot(*GV)) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    // BT12: the standard keepalive root that registers bit-object placeholders
    // is likewise registration data (identity, not bytes). Only a container
    // whose members are all marked bit objects is exempted -- a root that also
    // escapes an ordinary global keeps the ordinary rejection.
    if (isMCS251BitObjectKeepaliveRoot(*GV)) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    const DataLayout &DL = GV->getDataLayout();
    // X3: AS3 (__xdata, including `const __xdata`) and AS4 (__code) objects
    // are placed storage with their own emitters. A declaration (the
    // extern-only TU case above) emits nothing.
    if (unsigned GAS = GV->getAddressSpace(); GAS == 3 || GAS == 4) {
      emitAddressSpacedGlobal(GV, DL, GAS);
      return;
    }

    auto Reject = [GV]() {
      if (GV->isConstant())
        report_fatal_error(
            "MCS251: defined global data requires a byte-aligned read-only "
            "CSEG i8/i16/i32 scalar or nonempty initialized integer array "
            "of any alignment (emitted byte-aligned); mutable data, "
            "zeroinitializers, custom sections, TLS, weak/COMDAT, "
            "aggregates and initializer relocations are not supported");
      report_fatal_error(
          "MCS251: defined global data requires byte-aligned default-address-"
          "space i8/i16/i32 scalar, array, or struct storage with a fully "
          "defined integer initializer; custom sections, TLS, weak/COMDAT, "
          "empty aggregates and initializer relocations are not supported");
    };
    if (GV->isConstant() && GV->getAddressSpace() == 0 &&
        DL.getPointerSizeInBits(0) == 16)
      report_fatal_error(
          "MCS251: ordinary AS0 constants are not supported by the 16-bit "
          "memory contract until RAM runtime-copy initialization is "
          "implemented; use an explicit CODE object when that ABI is available");
    // Read-only integer arrays accept any declared alignment: the CSEG byte
    // image is emitted packed (byte-aligned) regardless, which MCS-251
    // permits for word accesses (QEMU + real hardware verified).  Scalars
    // and all mutable DSEG storage keep the byte-alignment requirement.
    const bool AlignedROArrayOK =
        GV->isConstant() && isa<ArrayType>(GV->getValueType());
    if (GV->isThreadLocal() || GV->getAddressSpace() != 0 || GV->hasSection() ||
        GV->hasComdat() ||
        (!GV->hasExternalLinkage() && !GV->hasLocalLinkage()) ||
        GV->getVisibility() != GlobalValue::DefaultVisibility ||
        GV->getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
        (!AlignedROArrayOK && (GV->getAlign().valueOrOne() != Align(1) ||
                               DL.getABITypeAlign(GV->getValueType()) !=
                                   Align(1))))
      Reject();

    const Constant *Init = GV->getInitializer();
    MCSymbol *Sym = getSymbol(GV);
    if (usesELFObjects()) {
      OutStreamer->emitSymbolAttribute(Sym, MCSA_ELF_TypeObject);
      OutStreamer->emitELFSize(
          Sym, MCConstantExpr::create(DL.getTypeAllocSize(GV->getValueType()),
                                      OutContext));
    }
    // X3: pointer initializer leaves (XINIT payload and RO tables alike) go
    // through the ELF-only 24-bit relocation channel.
    requireELFPointerInitializers(GV, Init, DL);
    if (!GV->isConstant()) {
      if (!isSupportedMutableInitializer(Init, DL))
        Reject();
      uint64_t Size = DL.getTypeAllocSize(GV->getValueType());
      if (!Size || Size > UINT16_MAX)
        report_fatal_error("MCS251: mutable global size must fit in 16 bits");

      const auto &TLOF =
          static_cast<const MCS251TargetObjectFile &>(getObjFileLowering());
      OutStreamer->switchSection(TLOF.getDSEGSection());
      emitASxxxxText("\t.area DSEG (DATA)");
      emitLinkage(GV, Sym);
      OutStreamer->emitLabel(Sym);
      OutStreamer->emitZeros(Size);

      // XINIT is a compact ROM-side table, not a second definition of the
      // object. Each independently linkable record is:
      //   u16 DSEG address, u16 object size, u16 payload size, payload bytes.
      // A zero payload means "clear only", so a large BSS object consumes six
      // ROM bytes. Nonzero records carry the complete target-endian byte image.
      // This sparse form remains correct when DSEG has register-bank/bit-area
      // holes or when several modules contribute independently placed slices.
      OutStreamer->switchSection(TLOF.getXINITSection());
      emitASxxxxText("\t.area XINIT (CODE)");
      OutStreamer->emitValue(MCSymbolRefExpr::create(Sym, OutContext), 2);
      OutStreamer->emitIntValue(Size, 2);
      bool HasPayload = !Init->isNullValue();
      OutStreamer->emitIntValue(HasPayload ? Size : 0, 2);
      if (HasPayload)
        emitMutableInitializer(DL, Init);

      OutStreamer->switchSection(OutContext.getObjectFileInfo()->getTextSection());
      emitASxxxxText("\t.area CSEG (CODE)");
      return;
    }

    if (!isSupportedROInitializer(Init, DL, /*AllowZeroImage=*/false))
      Reject();

    // Read-only globals and string literals stay in the established CSEG path.
    OutStreamer->switchSection(OutContext.getObjectFileInfo()->getTextSection());
    emitLinkage(GV, Sym);
    OutStreamer->emitLabel(Sym);
    emitROInitializer(DL, Init);
  }

  //===--------------------------------------------------------------------===//
  // X3: address-space placement emitters.
  //===--------------------------------------------------------------------===//

  // AS3 (`__xdata`, including `const __xdata`) and AS4 (`__code`) objects.
  //
  // AS3: one `.mcs251.XSEG.<sym>` NOBITS section per object (X3 ruling: XSEG
  // objects never straddle a 64K window; per-object sections let the linker
  // align each object into its window) plus one `.mcs251.xdata_init` record
  // carrying the 24-bit destination through the byte-of-24 channel. A single
  // object is capped at 16 bits (the u16 record fields cannot describe more;
  // >64K single objects are out of profile for the corpus and the boards).
  //
  // AS4: a read-only CODE-space image emitted exactly like the ordinary RO
  // path (in place in CSEG); an uninitialized/tentative definition is the
  // ROM zero image (legal in CODE space). Pointer leaves of both paths go
  // through the 24-bit relocation channel. Writes were already rejected
  // fail-closed by X2's memory-access checks; this slice does not redo them.
  void emitAddressSpacedGlobal(const GlobalVariable *GV, const DataLayout &DL,
                               unsigned GAS) {
    assert((GAS == 3 || GAS == 4) && "placement address space");
    StringRef Qual = GAS == 3 ? "__xdata" : "__code";
    auto Bad = [&](const Twine &What) {
      report_fatal_error("MCS251: " + Qual + " global '" + GV->getName() +
                         "': " + What);
    };
    // The record format, the per-object XSEG sections and the 24-bit pointer
    // channel exist only in the ELF object protocol (the bit records and the
    // ISR metadata use the same boundary).
    if (!getMCS251TM().emitsObjectFile() || !usesELFObjects())
      Bad("storage requires ELF object output "
          "(-filetype=obj -mcs251-object-format=elf)");
    if (GV->isThreadLocal() || GV->hasSection() || GV->hasComdat() ||
        (!GV->hasExternalLinkage() && !GV->hasLocalLinkage()) ||
        GV->getVisibility() != GlobalValue::DefaultVisibility ||
        GV->getDLLStorageClass() != GlobalValue::DefaultStorageClass)
      Bad("unsupported placement or linkage");

    const Constant *Init = GV->getInitializer();
    uint64_t Size = DL.getTypeAllocSize(GV->getValueType());
    MCSymbol *Sym = getSymbol(GV);
    if (usesELFObjects()) {
      OutStreamer->emitSymbolAttribute(Sym, MCSA_ELF_TypeObject);
      OutStreamer->emitELFSize(Sym,
                               MCConstantExpr::create(Size, OutContext));
    }

    const auto &TLOF =
        static_cast<const MCS251TargetObjectFile &>(getObjFileLowering());
    if (GAS == 3) {
      // Mutable XDATA storage is byte-aligned like every DSEG object.
      if (GV->getAlign().valueOrOne() != Align(1) ||
          DL.getABITypeAlign(GV->getValueType()) != Align(1))
        Bad("storage must be byte-aligned");
      if (!isSupportedMutableInitializer(Init, DL))
        Bad("unsupported initializer (byte-aligned i8/i16/i32 scalars, "
            "arrays and structs of integers and &global pointer leaves; "
            "expression algebra is not supported)");
      if (!Size || Size > UINT16_MAX)
        Bad("object size " + Twine(Size) + " does not fit the 16-bit XDATA "
            "record limit (65535 bytes; XSEG objects never straddle a 64K "
            "window)");

      MCSection *XSEG = OutContext.getELFSection(
          (Twine(".mcs251.XSEG.") + Sym->getName()).str(), ELF::SHT_NOBITS,
          ELF::SHF_ALLOC | ELF::SHF_WRITE);
      OutStreamer->switchSection(XSEG);
      emitLinkage(GV, Sym);
      OutStreamer->emitLabel(Sym);
      OutStreamer->emitZeros(Size);

      // Sparse XDATA init record v1 (frozen; mirrors the DSEG XINIT v1 shape
      // with the destination widened to 24 bits):
      //   u8  bank        canonical bits [23:16] -- the value DPXL loads
      //   u16 window      canonical bits [15:0], big-endian
      //   u16 object_size, u16 payload_size (0 = "clear only"), payload.
      // The CRT consumer loop is the X4 slice; this side only freezes the
      // format. A `const __xdata` object keeps its record: const is a write
      // discipline, the storage class comes from the address space.
      OutStreamer->switchSection(TLOF.getXDATAInitSection());
      emitXDATAInitAddress(Sym);
      OutStreamer->emitIntValue(Size, 2);
      bool HasPayload = !Init->isNullValue();
      OutStreamer->emitIntValue(HasPayload ? Size : 0, 2);
      if (HasPayload)
        emitMutableInitializer(DL, Init);

      OutStreamer->switchSection(
          OutContext.getObjectFileInfo()->getTextSection());
      emitASxxxxText("\t.area CSEG (CODE)");
      return;
    }

    // AS4: the read-only CODE-space image. Arrays of any declared alignment
    // are emitted byte-aligned (the ordinary RO rule); scalars stay
    // byte-aligned. STT_OBJECT/size semantics are the ordinary ones above.
    const bool AlignedArrayOK = isa<ArrayType>(GV->getValueType());
    if (!AlignedArrayOK && (GV->getAlign().valueOrOne() != Align(1) ||
                            DL.getABITypeAlign(GV->getValueType()) != Align(1)))
      Bad("scalar storage must be byte-aligned (arrays of any declared "
          "alignment are emitted byte-aligned)");
    if (!isSupportedROInitializer(Init, DL, /*AllowZeroImage=*/true))
      Bad("unsupported initializer (i8/i16/i32 scalars, nonempty arrays of "
          "integers and &global pointer leaves, or the ROM zero image)");
    OutStreamer->switchSection(OutContext.getObjectFileInfo()->getTextSection());
    emitLinkage(GV, Sym);
    OutStreamer->emitLabel(Sym);
    emitROInitializer(DL, Init);
  }
};
} // namespace

char MCS251AsmPrinter::ID = 0;

INITIALIZE_PASS(MCS251AsmPrinter, "mcs251-asm-printer",
                "MCS251 Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251AsmPrinter() {
  RegisterAsmPrinter<MCS251AsmPrinter> X(getTheMCS251Target());
}
