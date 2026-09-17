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
#include "llvm/ADT/SetVector.h"
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
#include "llvm/CodeGen/MachineJumpTableInfo.h"
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
#include "llvm/Support/SHA256.h"

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

  //===-------------------------------------------------------------------===//
  // G11-B: fixed-placement bookkeeping (all state is per module and reset in
  // emitStartOfAsmFile, mirroring ISRRecordCount/BitRecordCount).
  //===-------------------------------------------------------------------===//

  // The placement contract of one entity, decoded from the frozen G11-A IR
  // attribute pair (see getMCS251Placement below for the grammar).  The
  // field encodings are the NOTE schema v1 encodings (design §3.2).
  struct MCS251PlacementSpec {
    uint32_t Address = 0;     // 24-bit value, zero-extended in the NOTE.
    uint8_t StorageClass = 0; // NOTE v1: 0=AS0-DATA, 1=XDATA, 2=CODE.
    uint8_t Entity = 0;       // NOTE v1: 0=object, 1=function.
    uint8_t Ownership = 0;    // NOTE v1: 0=owned, 1=bind.
    uint32_t Flags = 0;       // bit0 retain (owned records only), bit1 noinit.
    std::string Stable;
  };
  // The `.mcu.fixed.*` section names already claimed by one placed entity:
  // the design's emitter-side single-entity invariant (a second entity
  // resolving to an existing fixed-section name is an internal invariant
  // violation -- the section must never merge two placed entities).
  StringSet<> EmittedFixedSections;

  // One NOTE record per placed entity of this TU, in emission order; the
  // NOTE writer in emitEndOfAsmFile appends the bind carriers and renders
  // the whole table into the single `.mcs251.placement` section.
  struct MCS251PlacementNoteEntry {
    MCS251PlacementSpec Spec;
    uint32_t Align = 1;
    uint32_t Size = 0;            // objects and bind records: a constant.
    const MCExpr *SizeExpr = nullptr; // functions: <stable>.end-<stable>.begin
    // G11-N4 (design rev 8 §8.3): the entity's FINAL MC symbol. Its name is
    // the actual ELF symbol name written into the `.mcs251.placement.names`
    // association NOTE -- never derived from Stable (no de-prefixing, no
    // demangling), so asm-labels and target mangling are preserved verbatim.
    // The symbol is read at end-of-file, after every definition is emitted.
    const MCSymbol *ELFSym = nullptr;
  };
  SmallVector<MCS251PlacementNoteEntry, 4> PlacementNoteEntries;

  // The fixed-section span label pair of the function currently being
  // emitted (nullptr outside runOnMachineFunction of a placed function).
  struct MCS251ActiveFixedFunction {
    MCS251PlacementSpec Spec;
    uint32_t Align = 1;
    MCSectionELF *Section = nullptr;
    MCSymbol *Begin = nullptr;
    MCSymbol *End = nullptr;
    // G11-N4: the function's own final MC symbol (the fixed section's one
    // defined symbol, at offset 0); its name is the owned association.
    const MCSymbol *Sym = nullptr;
  };
  std::unique_ptr<MCS251ActiveFixedFunction> ActiveFixedFunction;

  const MCS251TargetMachine &getMCS251TM() const {
    return static_cast<const MCS251TargetMachine &>(TM);
  }

  bool usesELFObjects() const { return getMCS251TM().usesELFObjects(); }

  // G8: the v2 ELF-object predicate that gates the EDATA-movable capability
  // marks.  Same shape as the identity gate in classifyModule(): a specified
  // layout-v2 contract on ELF object output.  v1 objects and the REL stream
  // never carry the mark, so their layout stays a frozen historical asset.
  bool marksEDataMovable() const {
    const std::optional<MCS251::MemoryContract> &Contract =
        getMCS251TM().getMemoryContract();
    return usesELFObjects() && Contract && Contract->isSpecified() &&
           Contract->ASLayoutVersion == 2;
  }

  // G13b: the v2 ELF-object predicate that gates the XSEG-split capability.
  // Same mechanism as marksEDataMovable() (D3 ruling): only a v2 object may
  // declare a >65535-byte all-zero __xdata object (SHF_MCS251_XSEG_SPLIT,
  // no XDATA_INIT record -- the linker synthesizes the per-window records).
  // A v1 object keeps the frozen 16-bit record gate byte for byte.
  bool marksXsegSplit() const {
    const std::optional<MCS251::MemoryContract> &Contract =
        getMCS251TM().getMemoryContract();
    return usesELFObjects() && Contract && Contract->isSpecified() &&
           Contract->ASLayoutVersion == 2;
  }

  // The slot value is canonical decimal text ("0" and "1" are legal;
  // The canonical-decimal slot check is shared with the Verifier and the
  // target contract check through MCS251ISR::parseCanonicalSlot; the object
  // boundary re-validates with the same rules so no layer can drift.

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

  // \return the terminal GlobalValue of a verified keepalive member: the
  // global reference itself (the "AS4 direct" form) or a chain of
  // single-operand no-op pointer casts (bitcast/addrspacecast, the standard
  // address-space adaptation of a used member) ending at one.  Anything else
  // in a member position is not a verified keepalive item.  The CALLER
  // classifies the terminal (P09 identity fix: the ISR-function rule and the
  // BT12 bit-object rule are two registered member kinds of the same frozen
  // member shape).
  static const GlobalValue *getKeepaliveTerminal(const Constant *C) {
    while (true) {
      if (isa<Function>(C) || isa<GlobalVariable>(C))
        return cast<GlobalValue>(C);
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

  //===--------------------------------------------------------------------===//
  // G11-B: fixed placement (design G11-PLACEMENT-DESIGN.md rev 7, §3.2).
  //
  // G11-A publishes each placed entity's contract on the GlobalObject as the
  // frozen IR attribute pair
  //   "mcs251-place"         = "0x<HEX A>,<data|xdata|code>,<object|function>,
  //                            <owned|bind>,<decimal flags>"  (flags bit0
  //                            retain, bit1 noinit)
  //   "mcs251-stable-symbol" = the stable identity (external entities keep
  //                            the declaration name; statics are TU-qualified)
  // and this emitter consumes it to build the `.mcu.fixed.<stable-symbol>`
  // sections and the `.mcs251.placement` SHT_NOTE record table.  The NOTE
  // schema v1, the envelope (namesz=7 + 8 storage bytes) and the layout-hash
  // field set are frozen in the design; this is the single producer.
  //===--------------------------------------------------------------------===//

  static Attribute getMCS251PlacementAttribute(const GlobalObject *GO,
                                               StringRef Kind) {
    if (const auto *F = dyn_cast<Function>(GO))
      return F->getFnAttribute(Kind);
    return cast<GlobalVariable>(GO)->getAttribute(Kind);
  }

  // \return the placement contract of \p GO, or std::nullopt when the entity
  // carries no "mcs251-place" attribute.  A malformed value is an internal
  // producer bug (G11-A Sema/CodeGen guarantee the shape) and fails closed.
  static std::optional<MCS251PlacementSpec>
  getMCS251Placement(const GlobalObject *GO) {
    Attribute Place = getMCS251PlacementAttribute(GO, "mcs251-place");
    if (!Place.isValid() || !Place.isStringAttribute())
      return std::nullopt;
    // G11-N1 (blocking fix, 2026-09-17): a P09 bit object is object
    // identity, not a byte storage entity. The dispatcher diverts every bit
    // object into the `.mcs251.bit` record path before the placement
    // dispatch, so an owned bit placeholder that also carries the
    // "mcs251-place" attribute would silently lose the placement (no
    // `.mcu.fixed.*` section, no NOTE); the bind census
    // (registerMCS251BindCarriers / emitPlacementNote) would conversely emit
    // a "DATA/object/bind size=1" NOTE whose size is the i8 handle size,
    // misrepresenting the bit identity as a one-byte DATA object. The
    // design schema has no bit storage class, so the A-layer producer never
    // generates this pair; as the receiving face for hand-written IR, this
    // reader rejects the double marking fail-closed instead of choosing one
    // of the two meanings. (The legal Bit+Placement keepalive container uses
    // DIFFERENT entities and never reaches this test: the bit member carries
    // no "mcs251-place".)
    if (const auto *GV = dyn_cast<GlobalVariable>(GO))
      if (MCS251::isBitObjectGlobal(*GV))
        report_fatal_error(
            "MCS251: bit object '" + Twine(GV->getName()) +
            "' also carries the mcs251-place attribute: a bit entity is "
            "object identity without a byte storage class and cannot be "
            "placed or bound");
    MCS251PlacementSpec Spec;
    SmallVector<StringRef, 5> Fields;
    SplitString(Place.getValueAsString(), Fields, ",");
    auto Bad = [GO]() -> std::optional<MCS251PlacementSpec> {
      report_fatal_error("MCS251: malformed mcs251-place attribute on '" +
                         Twine(GO->getName()) + "'");
      return std::nullopt;
    };
    if (Fields.size() != 5 || !Fields[0].starts_with("0x") ||
        Fields[0].size() > 10)
      return Bad();
    unsigned long long Parsed;
    if (Fields[0].substr(2).getAsInteger(16, Parsed) || Parsed > 0xFFFFFF)
      return Bad();
    Spec.Address = uint32_t(Parsed);
    auto ClassOf = [](StringRef S) -> std::optional<uint8_t> {
      if (S == "data")
        return uint8_t(0);
      if (S == "xdata")
        return uint8_t(1);
      if (S == "code")
        return uint8_t(2);
      return std::nullopt;
    };
    auto EntityOf = [](StringRef S) -> std::optional<uint8_t> {
      if (S == "object")
        return uint8_t(0);
      if (S == "function")
        return uint8_t(1);
      return std::nullopt;
    };
    auto OwnOf = [](StringRef S) -> std::optional<uint8_t> {
      if (S == "owned")
        return uint8_t(0);
      if (S == "bind")
        return uint8_t(1);
      return std::nullopt;
    };
    std::optional<uint8_t> SC = ClassOf(Fields[1]);
    std::optional<uint8_t> EN = EntityOf(Fields[2]);
    std::optional<uint8_t> OW = OwnOf(Fields[3]);
    if (!SC || !EN || !OW)
      return Bad();
    Spec.StorageClass = *SC;
    Spec.Entity = *EN;
    Spec.Ownership = *OW;
    unsigned long long Flags;
    if (Fields[4].getAsInteger(10, Flags) || Flags > 3)
      return Bad();
    Spec.Flags = uint32_t(Flags);
    if (Spec.Ownership == 1 && (Spec.Flags & 1))
      // Sema rejects retain on bind declarations; the writer re-checks
      // fail-closed (a bind record with flags.bit0=1 is malformed).
      return Bad();
    Attribute Stable = getMCS251PlacementAttribute(GO, "mcs251-stable-symbol");
    if (!Stable.isValid() || !Stable.isStringAttribute() ||
        Stable.getValueAsString().empty() ||
        Stable.getValueAsString().size() > 255)
      return Bad();
    Spec.Stable = Stable.getValueAsString().str();
    return Spec;
  }

  // The G11 section name: exactly one entity per `.mcu.fixed.*` section
  // (design §3.2 "each fixed entity gets a dedicated section"; the stable
  // symbol's TU qualifier keeps the names of one TU distinct).
  static std::string getMCS251FixedSectionName(const MCS251PlacementSpec &P) {
    return std::string(".mcu.fixed.") + P.Stable;
  }

  // G11-B R1 (review 2026-09-16, Alice §一): the `.mcu.fixed.*` namespace is
  // reserved for entities REGISTERED by the placement emitters
  // (claimMCS251FixedSection). A function or global object that reaches
  // emission with an ordinary EXPLICIT section assignment (C
  // __attribute__((section(".mcu.fixed.*"))), IR `section "...")`) enters the
  // same sections without any NOTE record -- it silently violates the
  // one-entity-per-section invariant, the NOTE.size == sh_size identity and
  // the span anti-merge guarantee, no matter whether it is emitted before or
  // after the placed entity (a zero-size second FUNC is the same hole: its
  // bytes are zero, but it is still an unregistered entity symbol in the
  // section). The check is therefore at the EMISSION ENTRY of every
  // function/global and never consults registration state: the only legal
  // way into a fixed section is the "mcs251-place" attribute, which never
  // uses the generic explicit-section path (a placed entity's section is
  // derived from its stable symbol and the emitters reject contradictory
  // explicit sections). Order-independent by construction.
  static bool isMCS251FixedSectionName(StringRef S) {
    return S.starts_with(".mcu.fixed.");
  }

  // H_source (design §3.2 rev 7 B1): SHA-256 of the BE-packed
  // (schema_version, storage_class, entity, ownership, address, align,
  // flags) -- four u8 then three u32 -- truncated to the low 32 bits of the
  // digest read big-endian.  `size`, the stable symbol, the source file and
  // the line never participate: the hash must be computable at record
  // assembly time, while the size fixup resolves only after layout.
  static uint32_t computeMCS251LayoutHash(const MCS251PlacementSpec &P,
                                          uint32_t Align) {
    const uint8_t F[16] = {
        uint8_t(1), P.StorageClass, P.Entity, P.Ownership,
        uint8_t(P.Address >> 24), uint8_t(P.Address >> 16),
        uint8_t(P.Address >> 8), uint8_t(P.Address),
        uint8_t(Align >> 24), uint8_t(Align >> 16), uint8_t(Align >> 8),
        uint8_t(Align),
        uint8_t(P.Flags >> 24), uint8_t(P.Flags >> 16),
        uint8_t(P.Flags >> 8), uint8_t(P.Flags)};
    SHA256 Hash;
    Hash.update(StringRef(reinterpret_cast<const char *>(F), sizeof(F)));
    std::array<uint8_t, 32> Digest = Hash.final();
    return (uint32_t(Digest[28]) << 24) | (uint32_t(Digest[29]) << 16) |
           (uint32_t(Digest[30]) << 8) | uint32_t(Digest[31]);
  }

  // N5 (design §3.2 "keepalive root processing"): the single-point member
  // classification consumed by the identity census, the storage-reservation
  // scan, the global emission gate and (by not being reached) the Reject
  // chain.  ISR and Bit keep their frozen rules; Placement is the G11 class
  // (an entity carrying the "mcs251-place" attribute).  The coordinator's
  // pre-ruling (2026-09-16) additionally admits BIND CARRIERS -- external
  // declarations kept alive in llvm.compiler.used by this emitter -- as a
  // distinct classified kind, so a bind carrier container is registration
  // data, not an unregistered capability.  This grants no user-visible
  // keepalive semantics: mcu_retain keeps its "place_at definition only"
  // boundary.
  enum class MCS251KeepaliveMemberKind { ISR, Bit, Placement, Bind, Unmarked };

  MCS251KeepaliveMemberKind
  classifyMCS251KeepaliveMember(const Constant *Member,
                                const DataLayout &DL) const {
    const GlobalValue *Terminal = getKeepaliveTerminal(Member);
    if (!Terminal)
      return MCS251KeepaliveMemberKind::Unmarked;
    if (const auto *F = dyn_cast<Function>(Terminal)) {
      // T06 rule unchanged: in a module that defines interrupt entries, a
      // program-address-space function member is the ISR keepalive kind.
      if (ModuleHasISRDefinitions &&
          F->getAddressSpace() == DL.getProgramAddressSpace())
        return MCS251KeepaliveMemberKind::ISR;
      if (getMCS251Placement(F))
        return MCS251KeepaliveMemberKind::Placement;
      return MCS251KeepaliveMemberKind::Unmarked;
    }
    if (const auto *GV = dyn_cast<GlobalVariable>(Terminal)) {
      if (MCS251::isBitObjectGlobal(*GV))
        return MCS251KeepaliveMemberKind::Bit;
      std::optional<MCS251PlacementSpec> P = getMCS251Placement(GV);
      if (P && P->Ownership == 1)
        return MCS251KeepaliveMemberKind::Bind;
      if (P)
        return MCS251KeepaliveMemberKind::Placement;
      return MCS251KeepaliveMemberKind::Unmarked;
    }
    return MCS251KeepaliveMemberKind::Unmarked;
  }

  // The container-level shape check (llvm.used / llvm.compiler.used,
  // appending, pointer array, "llvm.metadata", no materialized use) shared
  // by every consumer of the classification.
  static bool isMCS251KeepaliveContainer(const GlobalVariable &GV) {
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
    return true;
  }

  enum class MCS251KeepaliveRootKind {
    NotRoot,
    Classified,
    UnmarkedMember,
    Unengaged
  };

  // Classify one keepalive container by its members.  `Classified` is the
  // exemption answer the storage scan and the emission gate consume;
  // `UnmarkedMember` is the fail-closed verdict for a container with any
  // member outside the registered kinds (the old census rule).  The gate
  // ENGAGES for the historical root set (an ISR module's llvm.used, T06)
  // and for every container carrying a placement member (G11 -- the
  // placement keepalive exists in ordinary modules too); a container with
  // neither property keeps the historical ordinary walk (`Unengaged`), so
  // non-placement inputs keep their frozen diagnostic positions byte for
  // byte.  An empty container is NotRoot (nothing to register).
  MCS251KeepaliveRootKind
  classifyMCS251KeepaliveRoot(const GlobalVariable &GV,
                              const DataLayout &DL) const {
    if (!isMCS251KeepaliveContainer(GV))
      return MCS251KeepaliveRootKind::NotRoot;
    const auto *ArrTy = cast<ArrayType>(GV.getInitializer()->getType());
    if (ArrTy->getNumElements() == 0)
      return MCS251KeepaliveRootKind::NotRoot;
    bool Engage = ModuleHasISRDefinitions;
    bool AnyUnmarked = false;
    for (unsigned I = 0, E = ArrTy->getNumElements(); I != E; ++I) {
      const Constant *Member = GV.getInitializer()->getAggregateElement(I);
      MCS251KeepaliveMemberKind Kind =
          classifyMCS251KeepaliveMember(Member, DL);
      if (Kind == MCS251KeepaliveMemberKind::Placement ||
          Kind == MCS251KeepaliveMemberKind::Bind)
        Engage = true;
      else if (Kind == MCS251KeepaliveMemberKind::Unmarked)
        AnyUnmarked = true;
    }
    if (!Engage)
      return MCS251KeepaliveRootKind::Unengaged;
    return AnyUnmarked ? MCS251KeepaliveRootKind::UnmarkedMember
                       : MCS251KeepaliveRootKind::Classified;
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
      // T06 step 8(i) + BT12 + G11-B (design §3.2 N5 "keepalive root
      // processing"): the only exemptions for llvm.used/llvm.compiler.used
      // are per-MEMBER classification checks of a structurally verified
      // container -- never a bare name/section test.  The single-point
      // predicate classifyMCS251KeepaliveMember decides: an ISR member (a
      // program-AS function of an ISR module, T06 unchanged), a marked
      // bit-object placeholder (BT12) or a placement carrier (G11 --
      // including the pre-ruled bind carriers of llvm.compiler.used) is a
      // registered member kind; ANY unmarked member keeps the original
      // fail-closed verdict (return false), exactly like the all-function
      // rule before it.  The exemption is computed without touching the
      // shared seen-sets, so the same constant escaping through an ordinary
      // global or an instruction is still rejected there.
      switch (classifyMCS251KeepaliveRoot(GV, M.getDataLayout())) {
      case MCS251KeepaliveRootKind::Classified:
        continue;
      case MCS251KeepaliveRootKind::UnmarkedMember:
        return false;
      case MCS251KeepaliveRootKind::NotRoot:
      case MCS251KeepaliveRootKind::Unengaged:
        break;
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
  // definitions become a zero image).  The AS4-AGGREGATE slice additionally
  // accepts struct aggregates on the AllowStructs (AS4) call site only: the
  // gate keeps the isSupportedMutableInitializer shape -- recursive type
  // qualification first, then the value-shape dispatch -- so a zero image,
  // whole-item or member at any depth, walks the same recursive type check
  // as a nonzero form and the zero image of an empty/opaque struct is
  // rejected here instead of leaking through a zero early-exit.  With
  // AllowStructs=false (the frozen AS0 path) the accepted set and the
  // caller's rejection text are unchanged.  Undef elements and all other
  // initializer expression relocations are rejected here and reported by
  // the caller's policy message.
  //
  // Type-qualification half: the isSupportedMutableType mirror, with the
  // struct clause gated by AllowStructs (packed vs. non-packed does not
  // affect the decision; packing is a layout attribute over an isomorphic
  // type tree).
  static bool isSupportedROType(Type *Ty, const DataLayout &DL,
                                bool AllowStructs) {
    if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32))
      return true;
    if (auto *PT = dyn_cast<PointerType>(Ty))
      return DL.getTypeStoreSize(PT) == 4;
    if (auto *AT = dyn_cast<ArrayType>(Ty))
      return AT->getNumElements() &&
             isSupportedROType(AT->getElementType(), DL, AllowStructs);
    if (AllowStructs)
      if (auto *ST = dyn_cast<StructType>(Ty)) {
        if (ST->isOpaque() || ST->getNumElements() == 0)
          return false;
        return llvm::all_of(ST->elements(), [&](Type *E) {
          return isSupportedROType(E, DL, AllowStructs);
        });
      }
    return false;
  }

  static bool isSupportedROInitializer(const Constant *C, const DataLayout &DL,
                                       bool AllowZeroImage,
                                       bool AllowStructs) {
    Type *Ty = C->getType();
    // Two halves, in the isSupportedMutableInitializer order (design
    // AS4-AGGREGATE-INIT-DESIGN 6A.2): type qualification first, then the
    // value dispatch -- the zero image is never exempt from the type check.
    if (!isSupportedROType(Ty, DL, AllowStructs))
      return false;
    if (isa<ConstantInt>(C))
      return true;
    if (isa<ConstantAggregateZero>(C))
      return AllowZeroImage;
    if (isa<PointerType>(Ty)) {
      const GlobalValue *Base;
      int64_t Addend;
      return isSupportedPointerLeaf(C, DL, Base, Addend);
    }
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      if (!isa<ConstantDataArray>(C) && !isa<ConstantArray>(C))
        return false;
      for (unsigned I = 0; I != AT->getNumElements(); ++I) {
        const Constant *Element = C->getAggregateElement(I);
        if (!Element || !isSupportedROInitializer(Element, DL, AllowZeroImage,
                                                  AllowStructs))
          return false;
      }
      return true;
    }
    if (AllowStructs)
      if (auto *ST = dyn_cast<StructType>(Ty)) {
        for (unsigned I = 0; I != ST->getNumElements(); ++I) {
          const Constant *Element = C->getAggregateElement(I);
          if (!Element || !isSupportedROInitializer(Element, DL,
                                                    AllowZeroImage,
                                                    AllowStructs))
            return false;
        }
        return true;
      }
    return false;
  }

  // Emits the CSEG byte image of a read-only initializer: scalars in the
  // established target (big-endian) memory order, nested arrays element by
  // element with stride padding (zero for the packed integer layouts this
  // path accepts), structs member by member over the getStructLayout offsets
  // (the AS4-AGGREGATE mirror of emitMutableInitializer; inter-member holes
  // and the tail pad are materialized as zero bytes -- both always zero
  // under this contract's align-1 datalayout yet explicit in the algorithm),
  // pointer leaves through the 24-bit relocation channel, and the CODE-space
  // zero image where the support check allowed one.
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
    if (auto *AT = dyn_cast<ArrayType>(C->getType())) {
      uint64_t StoreSize = DL.getTypeStoreSize(AT->getElementType());
      uint64_t Stride = DL.getTypeAllocSize(AT->getElementType());
      for (unsigned I = 0; I != AT->getNumElements(); ++I) {
        emitROInitializer(DL, C->getAggregateElement(I));
        emitInitializerZeros(Stride - StoreSize);
      }
      return;
    }
    auto *ST = cast<StructType>(C->getType());
    const StructLayout *Layout = DL.getStructLayout(ST);
    uint64_t Pos = 0;
    for (unsigned I = 0; I != ST->getNumElements(); ++I) {
      uint64_t Offset = Layout->getElementOffset(I);
      emitInitializerZeros(Offset - Pos);
      emitROInitializer(DL, C->getAggregateElement(I));
      Pos = Offset + DL.getTypeStoreSize(ST->getElementType(I));
    }
    emitInitializerZeros(DL.getTypeStoreSize(ST) - Pos);
  }

public:
  static char ID;

  MCS251AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override {
    return "MCS251 Assembly Printer";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // G11-B R1: fail-closed fixed-namespace guard at the emission entry of
    // EVERY function, before any byte or section exists. Runs first so the
    // placement-specific diagnostic is reported even when the intruder is
    // emitted before (or without) any placed entity.
    if (const Function &F = MF.getFunction();
        F.hasSection() && isMCS251FixedSectionName(F.getSection()))
      report_fatal_error(
          "MCS251: function '" + Twine(F.getName()) +
          "' explicitly assigns fixed placement section '" + F.getSection() +
          "': .mcu.fixed.* sections are reserved for mcs251-place entities");
    // T06 card steps 8/9: the final machine boundary is validated before any
    // byte of the function is emitted.
    verifyFinalMachineBoundary(MF);
    SetupMachineFunction(MF);
    emitParameterSlots(MF);
    // G11-B: arm the fixed-section emission of a placed function before the
    // body goes out; emitFunctionEntryLabel switches the streamer into the
    // dedicated `.mcu.fixed.<stable-symbol>` section (nothing may precede
    // the entry byte there -- MCS-251 emits no function-alignment padding,
    // so the section starts exactly at the entry label) and
    // finishPlacedFunction drops the span end label after the jump-table
    // columns the base emitter appends in this same section.
    ActiveFixedFunction.reset();
    if (std::optional<MCS251PlacementSpec> P =
            getMCS251Placement(&MF.getFunction())) {
      beginPlacedFunction(MF, *P);
    }
    emitFunctionBody();
    if (ActiveFixedFunction)
      finishPlacedFunction();
    emitISRRecords(MF); // A3.5: ISR definitions, ELF object output only.
    return false;
  }

  // G11-B: prepare the dedicated fixed section of a placed function. The
  // actual section switch happens at the entry label (the base
  // emitFunctionHeader re-selects the ordinary text section itself, and the
  // constant pool / linkage directives it emits carry no bytes).
  void beginPlacedFunction(MachineFunction &MF, const MCS251PlacementSpec &P) {
    auto Bad = [&](const Twine &What) {
      report_fatal_error("MCS251: fixed function '" + Twine(MF.getName()) +
                         "': " + What);
    };
    if (!getMCS251TM().emitsObjectFile() || !usesELFObjects())
      Bad("fixed placement requires ELF object output "
          "(-filetype=obj -mcs251-object-format=elf)");
    const Function &F = MF.getFunction();
    if (P.Entity != 1 || P.Ownership != 0)
      Bad("the mcs251-place attribute of a function definition must be "
          "'function,owned'");
    // G11-B R1: a placed function's section is derived from the stable
    // symbol -- any explicit section is contradictory input (and a
    // `.mcu.fixed.*` one would claim a foreign fixed section).
    if (F.hasSection())
      Bad("a placed function must not carry an explicit section (the "
          "dedicated .mcu.fixed.<stable> section is derived from the "
          "placement attribute)");
    // G11-B review [建议] (2026-09-16, probe /tmp/g11b-review-alice/prefix.ll):
    // the base emitFunctionHeader emits IR prefix data BEFORE the entry
    // label, while the stream is still in the ordinary text section -- four
    // prefix bytes would silently live outside the entity's fixed section
    // and outside every span identity. Prologue data lands after the entry
    // label but is still emitter-fed data this contract has no ruling for.
    // Both are rejected fail-closed; the entry-label geometry claim below
    // is thereby scoped to inputs without prefix/prologue data.
    if (F.hasPrefixData() || F.hasPrologueData())
      Bad("prefix/prologue data is not supported on a fixed placement "
          "function");
    claimMCS251FixedSection(P, &F);
    const uint32_t Align = F.getAlign().valueOrOne().value();
    ActiveFixedFunction = std::make_unique<MCS251ActiveFixedFunction>();
    ActiveFixedFunction->Spec = P;
    ActiveFixedFunction->Section = getMCS251FixedSection(P, Align);
    ActiveFixedFunction->Align = Align;
    // G11-N4: capture the entity's final MC symbol now; its name is the
    // owned association of the `.mcs251.placement.names` NOTE and must equal
    // the fixed section's unique defined symbol name.
    ActiveFixedFunction->Sym = getSymbol(&F);
    // Temporary notype labels (no symbol-table presence, no size): they are
    // the design's auxiliary location labels of the span fixup, never a
    // second entity of the section.
    ActiveFixedFunction->Begin = OutContext.createTempSymbol(
        Twine(".mcu.span.") + P.Stable + ".begin");
    ActiveFixedFunction->End = OutContext.createTempSymbol(
        Twine(".mcu.span.") + P.Stable + ".end");
  }

  // G11-B: close the span of a placed function. Runs after emitFunctionBody
  // returned, i.e. after the base emitter appended the jump-table columns
  // to this function's section (BasePrinter calls emitJumpTableInfo at the
  // very end of the body) -- so <stable>.end sits after every payload byte
  // of the entity and the NOTE size field (end - begin) equals the section
  // span sh_size, not just the body-label span st_size.
  void finishPlacedFunction() {
    OutStreamer->switchSection(ActiveFixedFunction->Section);
    OutStreamer->emitLabel(ActiveFixedFunction->End);
    MCS251PlacementNoteEntry Entry;
    Entry.Spec = ActiveFixedFunction->Spec;
    Entry.Align = ActiveFixedFunction->Align;
    Entry.ELFSym = ActiveFixedFunction->Sym;
    Entry.SizeExpr = MCBinaryExpr::createSub(
        MCSymbolRefExpr::create(ActiveFixedFunction->End, OutContext),
        MCSymbolRefExpr::create(ActiveFixedFunction->Begin, OutContext),
        OutContext);
    PlacementNoteEntries.push_back(std::move(Entry));
    // Stay in the fixed section until the next entity switches away (the
    // ASxxxx area text only exists for the inspection stream, which fixed
    // placement never reaches -- it requires ELF object output).
  }

  // G11-B: the fixed section of a placed function starts exactly at its
  // entry label. Scoped claim (review 2026-09-16 [建议], probe
  // /tmp/g11b-review-alice/prefix.ll): for a placed function WITHOUT prefix
  // or prologue data -- the shapes beginPlacedFunction rejects fail-closed
  // -- the base header emits no bytes before the entry label on this target
  // (no function-alignment padding, byte-addressed machine), so switching
  // here keeps the entity's defined symbol at offset 0 with the
  // <stable>.begin label in front of it. (For a general IR function the base
  // header CAN emit bytes before the entry label -- prefix data stays in
  // the ordinary .text; that is exactly why those inputs are rejected for
  // fixed functions instead of "placed".)
  void emitFunctionBodyEnd() override {}

  void emitFunctionEntryLabel() override {
    if (ActiveFixedFunction) {
      OutStreamer->switchSection(ActiveFixedFunction->Section);
      // The base emitFunctionBody unconditionally switches back to
      // MF->getSection() before the epilog ("in case basic block sections
      // was used"), which would drop CurrentFnEnd -- and the .size label
      // difference with it -- into the ordinary text section.  Pointing
      // MF's section here keeps every later switch (epilog end label, the
      // jump-table columns emitJumpTableInfo appends, our span end label)
      // inside the entity's dedicated section.
      MF->setSection(ActiveFixedFunction->Section);
      OutStreamer->emitLabel(ActiveFixedFunction->Begin);
    }
    AsmPrinter::emitFunctionEntryLabel();
  }

  // BRJT (S3, design §3.2.3.4 + §3.2.4 L1): emit each jump table as a
  // dedicated column of N contiguous 3-byte ljmp entries (`02 hi lo`, the
  // 16-bit big-endian target field at entry offset +1) immediately after the
  // function body, in the same CSEG section -- the base AsmPrinter calls
  // this hook from emitFunctionBody, so the streamer is still positioned at
  // the end of this function's bytes (the §3.2.4 L3 geometry contract: table
  // column and function body share the InputSection, table after the body).
  // The default EK entry emission is deliberately NOT used: emitValue would
  // produce .word entries through the R_MCS251_16 DATA channel, but these
  // fields are same-bank CODE control addresses and must go through the
  // J16 channel (ELF R_MCS251_J16 = 7, reused frozen number; the MC-side
  // kind is fixup_mcs251_j16).
  //
  // L1 (design §3.2.4.3): defensive whole-table span assertion before any
  // byte is emitted. With E3 in force (entries <= 86 -> 258 bytes) this is
  // unreachable; it exists so that any future relaxation of E3 fails the
  // compile loudly instead of handing the linker a table that cannot fit a
  // single 64K bank (DESIGN.md "single table, single column": a start-offset
  // fit is a LINK-time judgement (S4/L2); the compile-time bound is entry
  // count times the 3-byte pitch).
  void emitJumpTableInfo() override {
    MachineJumpTableInfo *MJTI = MF->getJumpTableInfo();
    if (!MJTI)
      return;
    const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();
    // REL objects have no J16 R-mode (the frozen REL writer carries the
    // 16/24/lo8/mid8/hi8 modes only). The rejection must be decided by the
    // OBJECT OUTPUT MODE, never by hasRawTextSupport(): the REL streamer
    // overrides hasRawTextSupport() to true, so a raw-text probe let REL
    // objects fall through to emitLjmpTableEntry and die in the REL writer's
    // generic "expected relocatable expression / unsupported relocation
    // kind" diagnostics. The established emitsObjectFile() && !ELF idiom
    // (the same boundary requireELFPointerInitializers and emitISRRecords
    // use) makes the REL path hit this explicit diagnostic. Two scope rules:
    // the hook runs for EVERY function, so a module with no jump-table column
    // must keep compiling on the REL path (only an actual table needs the
    // J16 channel), and assembly text (no object file) stays legal and
    // spells the table bytes literally.
    bool AnyTable = false;
    for (const MachineJumpTableEntry &Entry : JT)
      if (!Entry.MBBs.empty())
        AnyTable = true;
    if (AnyTable && getMCS251TM().emitsObjectFile() && !usesELFObjects())
      report_fatal_error(
          "MCS251 jump tables require ELF object output; the REL object "
          "writer has no J16 code-address relocation "
          "(-filetype=obj -mcs251-object-format=elf)");
    for (unsigned JTI = 0, E = JT.size(); JTI != E; ++JTI) {
      ArrayRef<MachineBasicBlock *> MBBs = JT[JTI].MBBs;
      if (MBBs.empty())
        continue;
      uint64_t TableBytes = uint64_t(MBBs.size()) * 3;
      if (TableBytes > 0x10000)
        report_fatal_error(
            "MCS251: jump table " + Twine(JTI) + " of function '" +
            Twine(MF->getName()) + "' spans " + Twine(TableBytes) +
            " bytes and cannot fit one 64K bank as a single ljmp column "
            "(BRJT design §3.2.4 L1)");
      OutStreamer->emitLabel(GetJTISymbol(JTI));
      for (const MachineBasicBlock *MBB : MBBs)
        emitLjmpTableEntry(MBB->getSymbol());
    }
  }

  // One ljmp table entry: byte 0x02 then the 16-bit big-endian target field
  // (hi byte first, matching lld's Put(U>>8); Put(U) write order).  ELF
  // objects emit the field as a zero placeholder plus an R_MCS251_J16
  // relocation on the entry's offset +1; assembly text spells the same bytes
  // literally ASxxxx-style (`.db 0x02, (label) >> 8, (label)`), mirroring how
  // MOVADDR32 splits its object relocations from its text bytes.
  void emitLjmpTableEntry(const MCSymbol *Target) {
    OutStreamer->emitIntValue(0x02, 1);
    if (OutStreamer->hasRawTextSupport()) {
      const MCExpr *Sym = MCSymbolRefExpr::create(Target, OutContext);
      OutStreamer->emitValue(MCBinaryExpr::createLShr(
                                 Sym, MCConstantExpr::create(8, OutContext),
                                 OutContext),
                             1);
      OutStreamer->emitValue(Sym, 1);
      return;
    }
    MCSymbol *Field = OutContext.createTempSymbol();
    OutStreamer->emitLabel(Field);
    OutStreamer->emitIntValue(0, 2);
    OutStreamer->emitRelocDirective(*MCSymbolRefExpr::create(Field, OutContext),
                                    "R_MCS251_J16",
                                    MCSymbolRefExpr::create(Target, OutContext));
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
    if (!MCS251ISR::parseCanonicalSlot(VecAttr.getValueAsString(), Slot) ||
        !MCS251ISR::isLegalISRSlot(Slot))
      report_fatal_error(
          Twine("MCS251 ISR: vector is not a legal slot in profile 0-") +
          Twine(MCS251ISR::ISRVectorMaxSlot));

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
    // G2 B-S2 (G2-VARIADIC-DESIGN-draft.md §4.5): a variadic definition
    // also needs its slot area even when it has fewer than two IR fixed
    // parameters -- the printf shape is exactly one fixed parameter plus
    // the six 4-byte continuation slots emitted below.
    if (F.arg_size() < 2 && !F.isVarArg())
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
    // G8 S2 (design §3(b1)): a non-leaf slot area is an AS0 writable slice
    // whose accesses already go through 24-bit absolute addresses (the
    // parameterSlot() ExternalSymbol path), so it may share the EDATA
    // migration candidates under a v2 ELF object.  As with the global DSEG
    // slice, this is a capability mark only: the linker keeps the low window
    // by preference and migrates on failure, so a program that fits keeps
    // its pre-G8 slot addresses byte for byte.  Leaf OSEG areas overlay and
    // are never candidates.
    if (!Leaf && marksEDataMovable())
      Flags |= ELF::SHF_MCS251_EDATA_MOVABLE;
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
    // G2 B-S2 (G2-VARIADIC-DESIGN-draft.md R3 §4.3.1/§4.3.2): a variadic
    // definition continues the same serial slot stream with exactly six
    // 4-byte continuation slots `_PARM_(F+1).._PARM_(F+6)`.  Every actual
    // promoted to the slot width is 4B (i8/i16 promote to i32; f32 and
    // pointers are 4B), so there is no narrower continuation slot to emit.
    if (F.isVarArg()) {
      for (unsigned N = F.arg_size() + 1; N <= F.arg_size() + 6; ++N) {
        MCSymbol *Slot = OutContext.getOrCreateSymbol(
            getSymbol(&F)->getName() + "_PARM_" + Twine(N));
        if (!F.hasLocalLinkage())
          OutStreamer->emitSymbolAttribute(Slot, MCSA_Global);
        OutStreamer->emitLabel(Slot);
        if (usesELFObjects()) {
          OutStreamer->emitSymbolAttribute(Slot, MCSA_ELF_TypeObject);
          OutStreamer->emitELFSize(
              Slot, MCConstantExpr::create(4, OutContext));
        }
        OutStreamer->emitZeros(4);
      }
    }
    OutStreamer->switchSection(OutContext.getObjectFileInfo()->getTextSection());
    emitASxxxxText("\t.area CSEG (CODE)");
  }

  // P09 textdecode fix (direction (a), producer-side section split): the
  // read-only byte image section.  A module that carries persistent bit
  // objects will hold R_MCS251_BITADDR8 fields in its .text, and the linker
  // answers that by decoding the *whole* .text as an instruction stream
  // (BT13/BT15, BIT-OBJECT-CONTRACT.md 4.2: once on the producer's original
  // bytes and once on the final post-relocation image).  RO constants and
  // __code images appended to .text are data, not instructions, so such a
  // module would be rejected as "not a decodable instruction stream".  The
  // invariant the fix restores is producer-side: for a bit module, read-only
  // byte images go to the canonical ELF .rodata section (SHT_PROGBITS +
  // SHF_ALLOC, align 1), which lld classifies into the SAME CSEG region,
  // area cursor, flash gate and s_CSEG/l_CSEG boundaries as .text --
  // CODE-space semantics, the addressing channels (HI8/MID8/LO8 and
  // R_MCS251_24 keep plain range checks for CSEG targets) and the G13a
  // CODE-window budget are unchanged.
  //
  // Scope is deliberately the module-level bit-object property, not "a
  // BITADDR8 fixup was actually emitted": every producer of R_MCS251_BITADDR8
  // lowers a GlobalVariable carrying the mcs251-bit-object attribute
  // (MCS251MCInstLower::LowerSymbolOperand), so ModuleHasBitObjects is the
  // exact superset of modules whose .text can be whole-stream decoded, it is
  // already final when globals are emitted (globals go through
  // doFinalization, after every function body), and keying on the wider
  // property can never leave a bit module's constants inside the decoded
  // stream.  REL and assembly-text output keep the historical single-CSEG
  // layout byte for byte (the bit-object protocol is ELF-object-only).
  MCSection *selectROImageSection() {
    if (ModuleHasBitObjects && usesELFObjects())
      return OutContext.getObjectFileInfo()->getReadOnlySection();
    return OutContext.getObjectFileInfo()->getTextSection();
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
    // G11-B pre-ruling hook: keep the bind carriers alive before any
    // capability walk or optimization-time deletion can observe them.
    registerMCS251BindCarriers(M);
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
      // Kept verbatim identical to the BinaryFormat decoder's wording so
      // both validation paths report the same diagnostic.
      return "a no-prototype record cannot also be variadic";

    const unsigned ParamCount =
        N.getNumOperands() - MCS251Signatures::MetadataOperandFirstParam;
    if (ParamCount > 255)
      return "more than 255 source parameters is not representable";

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
      // A K&R record keeps its real parameter list (zero-parameter protocol
      // revision, PM 2026-09-15), but its parameters can never be `__bit`
      // (Sema N14), so a set bit-ness next to bit2 is a producer bug.  The
      // sentence is verbatim identical to the BinaryFormat decoder's.
      if (NoProto && *Bit != 0)
        return ("a no-prototype record cannot set a __bit bit-ness for "
                "source parameter " +
                Twine(I) + " (K&R parameters cannot be __bit)")
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
    // G11-B: per-module placement bookkeeping (idempotent with the
    // doInitialization run; classifyModule itself is idempotent).
    EmittedFixedSections.clear();
    PlacementNoteEntries.clear();
    ActiveFixedFunction.reset();
    if (llvm::any_of(M, [](const Function &F) {
          return !F.isDeclaration() &&
                 // G2 B-S2: a variadic definition owns a slot area even with
                 // fewer than two fixed parameters (printf shape: 1 + six
                 // continuation slots), so it reserves the bank like any
                 // other slotted definition.
                 (F.arg_size() > 1 || F.isVarArg());
        }) ||
        llvm::any_of(M.globals(), [this](const GlobalVariable &GV) {
          // T06 rework R1 + BT12 + G11-B (design §3.2 N5): keepalive
          // metadata whose members all classify (ISR / Bit / Placement /
          // Bind) is registration data and reserves no storage. The
          // standard llvm.used root has appending linkage and is NOT a
          // constant, so the exclusion needs this structural verification --
          // never a bare name/section test. Real mutable globals keep
          // triggering the reservation exactly as before.
          return !GV.isDeclaration() && !GV.isConstant() &&
                 !MCS251::isBitObjectGlobal(GV) &&
                 !isMCS251BitObjectKeepaliveRoot(GV) &&
                 !(ModuleHasISRDefinitions && isMCS251KeepaliveRoot(GV)) &&
                 classifyMCS251KeepaliveRoot(
                     GV, GV.getParent()->getDataLayout()) !=
                     MCS251KeepaliveRootKind::Classified;
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
      // G2 B-S2: a local variadic definition owns the six continuation
      // slots too; references to them must not turn into external
      // declarations.
      if (F.isVarArg())
        for (unsigned N = F.arg_size() + 1; N <= F.arg_size() + 6; ++N)
          LocalParameterSlots.insert(
              (getSymbol(&F)->getName() + "_PARM_" + Twine(N)).str());
    }

    // BT12: the single `.mcs251.bit` section (if any bit-object placeholder is
    // defined) is emitted after the module prologue so its label/symbol
    // attributes do not disturb the CSEG ordering. Declarations (extern bit)
    // are validated here but carry no record; their uses are handled by the
    // symbolic BITADDR8 path in the code emitter.
    if (ModuleHasBitObjects)
      emitBitObjectRecords(M);
  }

  //===--------------------------------------------------------------------===//
  // G11-B: the placement NOTE writer and the bind-carrier keepalive.
  //===--------------------------------------------------------------------===//

  // The coordinator's pre-ruling (2026-09-16): an unreferenced bind
  // external declaration does not survive an O2 pipeline (G11-A round
  // evidence: the declaration disappears and takes the placement attribute
  // with it), so every bind carrier of this module is kept alive in
  // llvm.compiler.used. RESPONSIBILITY SPLIT (review 2026-09-16 R2): the
  // AUTHORITATIVE registration now happens in the A layer --
  // clang CodeGenModule::Release() appends the bind carriers to
  // llvm.compiler.used after the final attributes are written and before
  // the optimization pipeline runs, which is the only point that closes the
  // clang -O2 end-to-end path. THIS copy is the B receiving face for
  // hand-written IR and llc-side pipelines (llc -O2 does not delete
  // unreferenced declarations itself; clang's optimizer does) -- a
  // belt-and-braces registration so the NOTE writer below never silently
  // loses a carrier that reached llc. This is an INTERNAL emission
  // mechanism of the emitter -- it grants no user-visible keepalive
  // semantics: mcu_retain keeps its "place_at definition only" boundary,
  // llvm.used is untouched, and place/retain definitions stay alive through
  // the A-layer llvm.used roots. Runs before classifyModule's census so the
  // container is seen through the same member classification (kind Bind).
  // Idempotent.
  void registerMCS251BindCarriers(Module &M) {
    SmallVector<GlobalValue *, 8> Carriers;
    for (GlobalVariable &GV : M.globals()) {
      std::optional<MCS251PlacementSpec> P = getMCS251Placement(&GV);
      if (P && P->Ownership == 1)
        Carriers.push_back(&GV);
    }
    for (Function &F : M) {
      std::optional<MCS251PlacementSpec> P = getMCS251Placement(&F);
      if (P && P->Ownership == 1)
        Carriers.push_back(&F);
    }
    if (Carriers.empty())
      return;
    // Erase-and-rebuild in the ModuleUtils appendToUsedList shape (kept
    // local: the target must not grow a TransformUtils dependency).
    // Members already registered are preserved verbatim; when every carrier
    // is already registered the second (idempotent) run rebuilds nothing.
    SmallSetVector<Constant *, 16> Init;
    GlobalVariable *GV = M.getGlobalVariable("llvm.compiler.used");
    if (GV) {
      if (Constant *C = GV->getInitializer())
        for (unsigned I = 0, E = C->getNumOperands(); I != E; ++I)
          Init.insert(cast<Constant>(C->getOperand(I)));
      bool AllPresent = true;
      for (GlobalValue *Carrier : Carriers)
        if (llvm::none_of(Init, [&](Constant *Member) {
              return getKeepaliveTerminal(Member) == Carrier;
            }))
          AllPresent = false;
      if (AllPresent)
        return;
      GV->eraseFromParent();
    }
    Type *EltTy = PointerType::getUnqual(M.getContext());
    for (GlobalValue *Carrier : Carriers)
      Init.insert(ConstantExpr::getPointerBitCastOrAddrSpaceCast(Carrier,
                                                                 EltTy));
    ArrayType *ATy = ArrayType::get(EltTy, Init.size());
    GV = new GlobalVariable(M, ATy, /*isConstant=*/true,
                            GlobalValue::AppendingLinkage,
                            ConstantArray::get(ATy, Init.getArrayRef()),
                            "llvm.compiler.used");
    GV->setSection("llvm.metadata");
  }

  // The `.mcs251.placement` SHT_NOTE table (design §3.2/§3.3): one section
  // per TU, envelope namesz=7 ("MCS251\0" incl. NUL; the name field's
  // storage is padded to 8 bytes), type=1 (placement v1), descsz = the sum
  // of the 4-byte record_size prologue and each record's padded payload.
  // Every multi-byte field is big-endian like the whole ELF32-BE object.
  // The function span's `size` field is written as the MC symbol
  // difference <stable>.end - <stable>.begin: a fixup that resolves only
  // in finish() after the final layout (long-branch relaxation included),
  // which is exactly the design's "no layout-time constant exists at
  // doFinalization" ruling; both labels are temporary notype symbols of
  // the entity's own section, so the difference folds without a
  // relocation. Owned objects keep the constant path.
  // G11-N4 (design rev 8 §8.3): the `.mcs251.placement.names` association
  // NOTE. Kept separate from the v1 record table on purpose -- the v1 byte
  // layout and its `layout_hash` algorithm are frozen and carry no ELF name,
  // no symbol index and no symbol-reference relocation, so the
  // stable_symbol -> actual ELF symbol mapping cannot be recovered from them
  // (asm-labels and target mangling break the "external stable == declaration
  // name" assumption). Every multi-byte field is big-endian.
  //
  // `record_index` counts the physical records of THIS file's
  // `.mcs251.placement` from 0, i.e. exactly the emission order the v1 writer
  // above renders. Incidentally the record_index of each entry equals the
  // index into `.mcs251.placement`.  Emitted after the v1 table so the
  // association always accompanies the records it annotates ("无关联节的旧
  // bind 对象必须拒绝" is a C-side consumer rule; this writer can never
  // produce such an object).
  //
  // Ownership-specific validation, fail-closed:
  //   owned -- the association must be the fixed section's unique principal
  //            symbol: defined, in section, and the section must be exactly
  //            the `.mcu.fixed.<stable>` section derived from this record.
  //            A mismatch is an internal invariant violation and is reported,
  //            never silently resolved to "some" symbol;
  //   bind  -- the association must be an undefined external symbol of this
  //            input. It is explicitly registered (MCSA_Global) so it appears
  //            in the input symbol table even without an ordinary code
  //            reference; registering an undefined symbol creates no storage,
  //            no section and no relocation.
  void emitPlacementNames(ArrayRef<MCS251PlacementNoteEntry> Entries) {
    SmallVector<StringRef, 8> ELFNames;
    uint64_t DescSize = 8; // association_version + entry_count
    for (unsigned I = 0, N = Entries.size(); I != N; ++I) {
      const MCS251PlacementNoteEntry &Entry = Entries[I];
      const MCS251PlacementSpec &P = Entry.Spec;
      if (!Entry.ELFSym)
        report_fatal_error("MCS251: placement record " + Twine(I) +
                           " (stable '" + P.Stable +
                           "') has no ELF association symbol");
      StringRef Name = Entry.ELFSym->getName();
      if (Name.empty())
        report_fatal_error("MCS251: placement association for record " +
                           Twine(I) + " (stable '" + P.Stable +
                           "') is empty");
      if (Name.contains('\0'))
        report_fatal_error("MCS251: placement association for record " +
                           Twine(I) + " (stable '" + P.Stable +
                           "') contains an embedded NUL");
      if (P.Ownership == 0) {
        std::string Expected = getMCS251FixedSectionName(P);
        if (!Entry.ELFSym->isInSection() ||
            Entry.ELFSym->getSection().getName() != Expected)
          report_fatal_error(
              "MCS251: owned placement record " + Twine(I) + " (stable '" +
              P.Stable + "') associates ELF symbol '" + Name +
              "' which is not the unique principal symbol of fixed section '" +
              Expected + "'");
      } else if (!Entry.ELFSym->isUndefined()) {
        report_fatal_error(
            "MCS251: bind placement record " + Twine(I) + " (stable '" +
            P.Stable + "') associates ELF symbol '" + Name +
            "' which is not an undefined external symbol of this input");
      }
      ELFNames.push_back(Name);
      uint64_t E = 8 + Name.size(); // record_index + elf_name_len + bytes
      DescSize += E + ((4 - (E % 4)) % 4);
    }
    MCSectionELF *Names = OutContext.getELFSection(".mcs251.placement.names",
                                                   ELF::SHT_NOTE, /*Flags=*/0);
    Names->setAlignment(Align(4));
    OutStreamer->switchSection(Names);
    OutStreamer->emitIntValue(7, 4); // namesz, "MCS251\0" including the NUL
    OutStreamer->emitIntValue(DescSize, 4);
    OutStreamer->emitIntValue(2, 4); // type: placement-name association
    OutStreamer->emitBytes(StringRef("MCS251\0\0", 8));
    OutStreamer->emitIntValue(1, 4); // association_version
    OutStreamer->emitIntValue(Entries.size(), 4);
    for (unsigned I = 0, N = Entries.size(); I != N; ++I) {
      StringRef Name = ELFNames[I];
      OutStreamer->emitIntValue(I, 4); // placement_record_index
      OutStreamer->emitIntValue(Name.size(), 4);
      OutStreamer->emitBytes(Name);
      unsigned Pad = (4 - ((8 + Name.size()) % 4)) % 4;
      if (Pad)
        OutStreamer->emitZeros(Pad);
    }
    OutStreamer->switchSection(
        OutContext.getObjectFileInfo()->getTextSection());
  }

  void emitPlacementNote(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    // Bind carriers are recorded here (globals then functions, module
    // order); owned entities were recorded at their emission.
    for (GlobalVariable &GV : M.globals()) {
      std::optional<MCS251PlacementSpec> P = getMCS251Placement(&GV);
      if (!P || P->Ownership != 1)
        continue;
      if (!GV.isDeclaration() || !GV.hasExternalLinkage())
        report_fatal_error("MCS251: bind placement carrier '" +
                           Twine(GV.getName()) +
                           "' must be an external declaration");
      MCS251PlacementNoteEntry Entry;
      Entry.Spec = *P;
      Entry.Align = GV.getAlign().valueOrOne().value();
      if (P->Entity == 0) {
        // A bind object's size is its declared sizeof (the cross-carrier
        // comparison input); zero is malformed (P-4: Sema owns the check,
        // the writer re-validates fail-closed).
        uint64_t Size = DL.getTypeAllocSize(GV.getValueType());
        if (!Size)
          report_fatal_error("MCS251: malformed placement NOTE: bind "
                             "object '" +
                             Twine(GV.getName()) + "' has size 0");
        Entry.Size = uint32_t(Size);
      } // bind function: size 0 = "no size constraint".
      // G11-N4 §8.3: the bind association must exist as an undefined external
      // symbol even when no code references it. Registering here (before the
      // ELF writer builds the symbol table) is what puts it in the input
      // symbol table; it allocates no storage and emits no relocation.
      MCSymbol *Sym = getSymbol(&GV);
      OutStreamer->emitSymbolAttribute(Sym, MCSA_Global);
      Entry.ELFSym = Sym;
      PlacementNoteEntries.push_back(std::move(Entry));
    }
    for (Function &F : M) {
      std::optional<MCS251PlacementSpec> P = getMCS251Placement(&F);
      if (!P || P->Ownership != 1)
        continue;
      if (!F.isDeclaration() || !F.hasExternalLinkage())
        report_fatal_error("MCS251: bind placement carrier '" +
                           Twine(F.getName()) +
                           "' must be an external declaration");
      MCS251PlacementNoteEntry Entry;
      Entry.Spec = *P;
      Entry.Align = F.getAlign().valueOrOne().value();
      MCSymbol *Sym = getSymbol(&F);
      OutStreamer->emitSymbolAttribute(Sym, MCSA_Global);
      Entry.ELFSym = Sym;
      PlacementNoteEntries.push_back(std::move(Entry));
    }
    if (PlacementNoteEntries.empty())
      return; // no placed entity in this TU: no section, byte-for-byte
              // unchanged output for placement-free modules.
    if (!getMCS251TM().emitsObjectFile() || !usesELFObjects())
      report_fatal_error(
          "MCS251 placement requires ELF object output "
          "(-filetype=obj -mcs251-object-format=elf)");

    SmallVector<uint32_t, 8> RecordSizes;
    uint64_t DescSize = 0;
    for (const MCS251PlacementNoteEntry &E : PlacementNoteEntries) {
      // 24 fixed payload bytes + stable_len + stable + padding to a 4-byte
      // multiple; record_size counts everything after its own 4 bytes.
      uint32_t Tail = 24 + 1 + uint32_t(E.Spec.Stable.size());
      uint32_t RecordSize = Tail + ((4 - (Tail % 4)) % 4);
      RecordSizes.push_back(RecordSize);
      DescSize += 4 + RecordSize;
    }
    MCSectionELF *Note = OutContext.getELFSection(".mcs251.placement",
                                                  ELF::SHT_NOTE, /*Flags=*/0);
    Note->setAlignment(Align(4));
    OutStreamer->switchSection(Note);
    OutStreamer->emitIntValue(7, 4); // namesz, "MCS251\0" including the NUL
    OutStreamer->emitIntValue(DescSize, 4);
    OutStreamer->emitIntValue(1, 4); // type: placement record table v1
    OutStreamer->emitBytes(StringRef("MCS251\0\0", 8));
    for (unsigned I = 0, E = PlacementNoteEntries.size(); I != E; ++I) {
      const MCS251PlacementNoteEntry &Entry = PlacementNoteEntries[I];
      const MCS251PlacementSpec &P = Entry.Spec;
      OutStreamer->emitIntValue(RecordSizes[I], 4);
      OutStreamer->emitIntValue(1, 1); // schema_version
      OutStreamer->emitIntValue(P.StorageClass, 1);
      OutStreamer->emitIntValue(P.Entity, 1);
      OutStreamer->emitIntValue(P.Ownership, 1);
      OutStreamer->emitIntValue(P.Address, 4);
      if (Entry.SizeExpr)
        OutStreamer->emitValue(Entry.SizeExpr, 4);
      else
        OutStreamer->emitIntValue(Entry.Size, 4);
      OutStreamer->emitIntValue(Entry.Align, 4);
      OutStreamer->emitIntValue(P.Flags, 4);
      OutStreamer->emitIntValue(computeMCS251LayoutHash(P, Entry.Align), 4);
      OutStreamer->emitIntValue(P.Stable.size(), 1);
      OutStreamer->emitBytes(P.Stable);
      unsigned Pad =
          RecordSizes[I] - (24 + 1 + uint32_t(P.Stable.size()));
      if (Pad)
        OutStreamer->emitZeros(Pad);
    }
    OutStreamer->switchSection(
        OutContext.getObjectFileInfo()->getTextSection());
    // G11-N4: the association NOTE follows the v1 table, in the same physical
    // record order. Placement-free modules returned above, so this is a no-op
    // (no section, byte-for-byte unchanged output) for them.
    emitPlacementNames(PlacementNoteEntries);
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
    // G11-B: the placement NOTE is written for every object identity (the
    // v1 explicit contract and the v2 default alike) and before the v2
    // attributes section; it is a no-op for placement-free modules.
    emitPlacementNote(M);
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
    // G11-B R1: fail-closed fixed-namespace guard at the emission entry of
    // EVERY global object (same rule as the function guard in
    // runOnMachineFunction). The object-side explicit-section intrusion is
    // rejected here with the placement-specific diagnostic BEFORE any
    // dispatch (the generic custom-section rejection further below is not a
    // placement contract check and historically let the wording obscure the
    // real problem); order-independent by construction.
    if (GV->hasSection() && isMCS251FixedSectionName(GV->getSection()))
      report_fatal_error(
          "MCS251: global '" + Twine(GV->getName()) +
          "' explicitly assigns fixed placement section '" + GV->getSection() +
          "': .mcu.fixed.* sections are reserved for mcs251-place entities");
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

    // G11-B (design §3.2 N5, the third pass-through): a keepalive container
    // whose members all classify -- ISR, bit, placement (fixed carriers)
    // and, by the coordinator's pre-ruling, llvm.compiler.used bind
    // carriers -- is registration data, not bytes. The classification is
    // the same single-point predicate the census and the storage scan
    // consume; a container with any unmarked member keeps the ordinary
    // rejection path below (the Reject chain is unchanged for it).
    if (classifyMCS251KeepaliveRoot(*GV, GV->getParent()->getDataLayout()) ==
        MCS251KeepaliveRootKind::Classified) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    // G11-B: a placed entity emits into its dedicated `.mcu.fixed.*`
    // section (owned) or carries no storage at all (bind: an external
    // declaration whose NOTE record the writer at the end of the file
    // owns). The dispatch precedes the AS3/AS4 emitters: a placed __xdata
    // or __code object belongs to its fixed section, never to the ordinary
    // per-object XSEG/CSEG paths.
    if (std::optional<MCS251PlacementSpec> Placement = getMCS251Placement(GV)) {
      if (Placement->Ownership == 1) {
        if (!GV->isDeclaration())
          report_fatal_error("MCS251: bind placement carrier '" +
                             Twine(GV->getName()) +
                             "' must be an external declaration");
        return; // no storage, no section, no init records
      }
      emitFixedPlacementGlobal(GV, Placement->StorageClass, *Placement);
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

    if (!isSupportedROInitializer(Init, DL, /*AllowZeroImage=*/false,
                                  /*AllowStructs=*/false))
      Reject();

    // Read-only globals and string literals stay in the established CSEG path.
    // A bit module splits them into .rodata (selectROImageSection) so its
    // .text remains a pure, link-decodable instruction stream.
    OutStreamer->switchSection(selectROImageSection());
    emitLinkage(GV, Sym);
    OutStreamer->emitLabel(Sym);
    emitROInitializer(DL, Init);
  }

  //===--------------------------------------------------------------------===//
  // X3: address-space placement emitters.
  //===--------------------------------------------------------------------===//

  //===--------------------------------------------------------------------===//
  // G11-B: fixed placement emitters.
  //===--------------------------------------------------------------------===//

  // The storage-class consistency axis (design §2.2 N2): the NOTE
  // storage_class is the authoritative orthogonal field, NOT the ELF
  // section type -- CODE class sections are PROGBITS (EXECINSTR only for
  // functions), AS0-DATA and XDATA class sections are NOBITS with WRITE.
  // The retain flag rides the section iff NOTE flags.bit0=1 (design §3.2:
  // the attribute bit is the only writer of SHF_GNU_RETAIN; the generic
  // TLOF used/retain path is never taken for a fixed entity).
  MCSectionELF *getMCS251FixedSection(const MCS251PlacementSpec &P,
                                      uint32_t AlignVal) {
    unsigned Type, Flags = ELF::SHF_ALLOC;
    if (P.StorageClass == 2) { // CODE
      Type = ELF::SHT_PROGBITS;
      if (P.Entity == 1)
        Flags |= ELF::SHF_EXECINSTR;
    } else { // AS0-DATA / XDATA
      Type = ELF::SHT_NOBITS;
      Flags |= ELF::SHF_WRITE;
    }
    if (P.Flags & 1)
      Flags |= ELF::SHF_GNU_RETAIN;
    MCSectionELF *Sec = OutContext.getELFSection(
        getMCS251FixedSectionName(P), Type, Flags);
    Sec->setAlignment(Align(AlignVal));
    return Sec;
  }

  // The emitter-side single-entity invariant (design §3.2 rev 5/6): the
  // second placed entity resolving to an already-existing `.mcu.fixed.*`
  // section name is an internal invariant violation -- fail loudly instead
  // of letting one entity's span swallow the other's bytes.
  void claimMCS251FixedSection(const MCS251PlacementSpec &P,
                               const GlobalObject *GO) {
    std::string Name = getMCS251FixedSectionName(P);
    if (!EmittedFixedSections.insert(Name).second)
      report_fatal_error(Twine("MCS251: section " + Name +
                               " disagrees with placement NOTE for ") +
                         getSymbolName(GO));
  }

  // An owned placed object: exactly one sized defined symbol at offset 0 of
  // its dedicated `.mcu.fixed.<stable-symbol>` section. The class dispatch:
  //   AS0-DATA -- NOBITS zero fill plus the sparse XINIT record (the linker
  //               places the section at A; the record's dest relocation
  //               resolves to the entity entry);
  //   XDATA    -- NOBITS zero fill plus the `.mcs251.xdata_init` record;
  //   CODE     -- the PROGBITS read-only image is the section itself (no
  //               EXECINSTR, no init record).
  // noinit (flags.bit1) suppresses the XINIT/XDATA_INIT record entirely:
  // the entity's bytes are never cleared or copied at startup (the data
  // sections stay NOBITS zero-fill / the ROM image stands as emitted).
  void emitFixedPlacementGlobal(const GlobalVariable *GV,
                                uint8_t StorageClass,
                                const MCS251PlacementSpec &P) {
    StringRef Qual = StorageClass == 1   ? "__xdata"
                     : StorageClass == 2 ? "__code"
                                         : "data";
    auto Bad = [&](const Twine &What) {
      report_fatal_error("MCS251: fixed " + Qual + " global '" +
                         GV->getName() + "': " + What);
    };
    // The fixed-section protocol and the NOTE exist only in ELF objects.
    if (!getMCS251TM().emitsObjectFile() || !usesELFObjects())
      Bad("fixed placement requires ELF object output "
          "(-filetype=obj -mcs251-object-format=elf)");
    if (GV->isThreadLocal() || GV->hasSection() || GV->hasComdat() ||
        (!GV->hasExternalLinkage() && !GV->hasLocalLinkage()) ||
        GV->getVisibility() != GlobalValue::DefaultVisibility ||
        GV->getDLLStorageClass() != GlobalValue::DefaultStorageClass)
      Bad("unsupported placement or linkage");
    if (StorageClass == 0 && GV->getAddressSpace() != 0)
      Bad("storage class 'data' requires the default address space");
    if (StorageClass == 1 && GV->getAddressSpace() != 3)
      Bad("storage class 'xdata' requires __xdata (address_space(3))");
    if (StorageClass == 2 && GV->getAddressSpace() != 0 &&
        GV->getAddressSpace() != 4)
      Bad("storage class 'code' requires __code (address_space(4)) or a "
          "default-address-space const entity");

    const DataLayout &DL = GV->getDataLayout();
    const Constant *Init = GV->getInitializer();
    uint64_t Size = DL.getTypeAllocSize(GV->getValueType());
    if (!Size)
      Bad("zero-size entity is not placeable");
    const bool NoInit = (P.Flags & 2) != 0;
    if (NoInit && !Init->isNullValue())
      Bad("noinit entity must be zero-initialized");

    // G11-N2 (blocking fix, 2026-09-17; wording narrowed per review S3, design
    // rev 9.2): the AS0 fixed path writes the object size into the two u16
    // fields (size, payload-size) of the sparse XINIT record. The ordinary
    // DSEG path already refuses a mutable object whose size does not fit in
    // 16 bits ("mutable global size must fit in 16 bits"); the fixed path used
    // to bypass that gate, emitting size/payload-size fields truncated to 0
    // (and, with an initializer, still appending the full payload after the
    // zeroed length). Restore the same guard with the same wording before any
    // fixed section or initialization record is emitted.
    //
    // Scope of the 16-bit limit: it is these two XINIT protocol fields, not
    // the ELF section size, the symbol st_size or the placement NOTE size --
    // those stay u32 (design §3.2/§8.3). The guard does not extend to
    // CODE-class objects or to bind declarations, which never pass through
    // the AS0-DATA owned XINIT writer.
    //
    // noinit: no initialization record is emitted at all, so no XINIT field
    // is written. Applying the same 1..65535 limit to a noinit AS0-DATA owned
    // object is an explicit design decision (rev 9.2: the support boundary of
    // this slice), NOT truncation of an existing record; the guard therefore
    // sits before the `if (!NoInit)` gate so both shapes are rejected
    // identically.
    if (StorageClass == 0 && Size > UINT16_MAX)
      report_fatal_error("MCS251: mutable global size must fit in 16 bits");

    claimMCS251FixedSection(P, GV);
    const uint32_t Align = GV->getAlign().valueOrOne().value();
    MCSectionELF *Sec = getMCS251FixedSection(P, Align);
    MCSymbol *Sym = getSymbol(GV);

    if (StorageClass == 2) {
      // CODE class: the ROM image is the section payload. The ordinary RO
      // rules apply to the initializer shape; a declared alignment above 1
      // is honored by the section's own sh_addralign (the entity sits at
      // offset 0, so the linker's A % align check is the whole story --
      // the byte image itself is emitted packed like every other RO image).
      if (!isSupportedROInitializer(Init, DL, /*AllowZeroImage=*/true,
                                    /*AllowStructs=*/true))
        Bad("unsupported initializer (i8/i16/i32 scalars, nonempty arrays "
            "of integers, nonempty non-opaque structs of those at any "
            "nesting, &global pointer leaves, or the ROM zero image)");
      OutStreamer->emitSymbolAttribute(Sym, MCSA_ELF_TypeObject);
      OutStreamer->emitELFSize(Sym, MCConstantExpr::create(Size, OutContext));
      OutStreamer->switchSection(Sec);
      emitLinkage(GV, Sym);
      OutStreamer->emitLabel(Sym);
      emitROInitializer(DL, Init);
      OutStreamer->switchSection(
          OutContext.getObjectFileInfo()->getTextSection());
      emitASxxxxText("\t.area CSEG (CODE)");
    } else {
      if (!isSupportedMutableInitializer(Init, DL))
        Bad("unsupported initializer (byte-aligned i8/i16/i32 scalars, "
            "arrays and structs of integers and &global pointer leaves; "
            "expression algebra is not supported)");
      if (StorageClass == 1 && Size > UINT16_MAX)
        // FIXED XDATA keeps the non-split ruling (§4.1): a fixed section
        // never carries SHF_MCS251_XSEG_SPLIT and the v1 record's u16
        // fields cannot describe a larger single object.
        Bad("object size " + Twine(Size) + " does not fit the 16-bit XDATA "
            "record limit (65535 bytes; fixed objects are never split)");
      OutStreamer->emitSymbolAttribute(Sym, MCSA_ELF_TypeObject);
      OutStreamer->emitELFSize(Sym, MCConstantExpr::create(Size, OutContext));
      OutStreamer->switchSection(Sec);
      emitLinkage(GV, Sym);
      OutStreamer->emitLabel(Sym);
      OutStreamer->emitZeros(Size);

      const auto &TLOF =
          static_cast<const MCS251TargetObjectFile &>(getObjFileLowering());
      if (!NoInit) {
        bool HasPayload = !Init->isNullValue();
        if (StorageClass == 0) {
          // The same sparse XINIT record the ordinary DSEG path emits; the
          // destination relocation names the entity symbol, which the
          // linker resolves to A once the NOTE places the section.
          OutStreamer->switchSection(TLOF.getXINITSection());
          emitASxxxxText("\t.area XINIT (CODE)");
          OutStreamer->emitValue(MCSymbolRefExpr::create(Sym, OutContext), 2);
          OutStreamer->emitIntValue(Size, 2);
          OutStreamer->emitIntValue(HasPayload ? Size : 0, 2);
          if (HasPayload)
            emitMutableInitializer(DL, Init);
        } else {
          // The v1 xdata_init record (24-bit destination through the
          // byte-of-24 channel), exactly like the unplaced XSEG path.
          OutStreamer->switchSection(TLOF.getXDATAInitSection());
          emitXDATAInitAddress(Sym);
          OutStreamer->emitIntValue(Size, 2);
          OutStreamer->emitIntValue(HasPayload ? Size : 0, 2);
          if (HasPayload)
            emitMutableInitializer(DL, Init);
        }
      }
      OutStreamer->switchSection(
          OutContext.getObjectFileInfo()->getTextSection());
      emitASxxxxText("\t.area CSEG (CODE)");
    }

    // The NOTE record: an owned object's size is a layout-time constant
    // (the design keeps the constant write for objects; only the function
    // span needs the symbol-difference fixup).
    MCS251PlacementNoteEntry Entry;
    Entry.Spec = P;
    Entry.Align = Align;
    Entry.Size = uint32_t(Size);
    Entry.ELFSym = Sym;
    PlacementNoteEntries.push_back(std::move(Entry));
  }


  // AS3 (`__xdata`, including `const __xdata`) and AS4 (`__code`) objects.
  //
  // AS3: one `.mcs251.XSEG.<sym>` NOBITS section per object (X3 ruling: an
  // unmarked XSEG object never straddles a 64K window; per-object sections
  // let the linker align each object into its window) plus one
  // `.mcs251.xdata_init` record carrying the 24-bit destination through the
  // byte-of-24 channel.  A single object is capped at 16 bits (the u16
  // record fields cannot describe more).  G13b two-state ruling: a v2 object
  // may declare ONE logical all-zero object of any size by marking the
  // section SHF_MCS251_XSEG_SPLIT and emitting NO record -- the linker
  // places it as one contiguous range, possibly across a 64K window, and
  // synthesizes per-window clear-only v1 records for the CRT walker.
  // Unmarked sections keep the single-window ruling byte for byte.
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
      // G13b: the 16-bit limit is a RECORD-format constraint, not an
      // address-space limit.  Two states for Size > UINT16_MAX:
      //  - a nonzero initializer stays rejected (a single v1 record's u16
      //    object_size/payload_size fields cannot describe more);
      //  - an all-zero object stays ONE logical XSEG section: a v2 object
      //    marks it SHF_MCS251_XSEG_SPLIT and emits NO record -- the linker
      //    places it as one contiguous range and synthesizes per-window
      //    clear-only v1 records.  A v1 object keeps the frozen gate byte
      //    for byte (the split capability is a v2 producer capability, D3,
      //    same mechanism as SHF_MCS251_EDATA_MOVABLE).
      // Size <= UINT16_MAX keeps the established path unchanged: the record
      // is emitted exactly as before and no flag is set (existing objects
      // and all fixtures stay byte-identical).
      const bool HasPayload = !Init->isNullValue();
      if (Size > UINT16_MAX) {
        if (HasPayload)
          Bad("payload cannot exceed the 16-bit XDATA record limit (65535 "
              "bytes); zero-initialized objects are split by the linker "
              "into per-window records");
        if (!marksXsegSplit())
          Bad("object size " + Twine(Size) + " does not fit the 16-bit XDATA "
              "record limit (65535 bytes; XSEG objects never straddle a 64K "
              "window)");
      } else if (!Size) {
        Bad("object size 0 does not fit the 16-bit XDATA "
            "record limit (65535 bytes; XSEG objects never straddle a 64K "
            "window)");
      }
      const bool XsegSplit = Size > UINT16_MAX;

      MCSection *XSEG = OutContext.getELFSection(
          (Twine(".mcs251.XSEG.") + Sym->getName()).str(), ELF::SHT_NOBITS,
          ELF::SHF_ALLOC | ELF::SHF_WRITE |
              (XsegSplit ? ELF::SHF_MCS251_XSEG_SPLIT : 0));
      OutStreamer->switchSection(XSEG);
      emitLinkage(GV, Sym);
      OutStreamer->emitLabel(Sym);
      OutStreamer->emitZeros(Size);

      if (!XsegSplit) {
        // Sparse XDATA init record v1 (frozen; mirrors the DSEG XINIT v1
        // shape with the destination widened to 24 bits):
        //   u8  bank        canonical bits [23:16] -- the value DPXL loads
        //   u16 window      canonical bits [15:0], big-endian
        //   u16 object_size, u16 payload_size (0 = "clear only"), payload.
        // The CRT consumer loop is the X4 slice; this side only freezes the
        // format. A `const __xdata` object keeps its record: const is a
        // write discipline, the storage class comes from the address space.
        // A split object emits none (a single v1 record cannot describe it;
        // the linker synthesizes the per-window records).
        OutStreamer->switchSection(TLOF.getXDATAInitSection());
        emitXDATAInitAddress(Sym);
        OutStreamer->emitIntValue(Size, 2);
        OutStreamer->emitIntValue(HasPayload ? Size : 0, 2);
        if (HasPayload)
          emitMutableInitializer(DL, Init);
      }

      OutStreamer->switchSection(
          OutContext.getObjectFileInfo()->getTextSection());
      emitASxxxxText("\t.area CSEG (CODE)");
      return;
    }

    // AS4: the read-only CODE-space image. Arrays of any declared alignment
    // are emitted byte-aligned (the ordinary RO rule); non-array storage
    // (scalars and structs) stays byte-aligned. STT_OBJECT/size semantics
    // are the ordinary ones above. Struct aggregates are accepted since the
    // AS4-AGGREGATE slice (design AS4-AGGREGATE-INIT-DESIGN 6A); the AS0
    // path above stays array/scalar only.
    const bool AlignedArrayOK = isa<ArrayType>(GV->getValueType());
    if (!AlignedArrayOK && (GV->getAlign().valueOrOne() != Align(1) ||
                            DL.getABITypeAlign(GV->getValueType()) != Align(1)))
      Bad("non-array storage must be byte-aligned (arrays of any declared "
          "alignment are emitted byte-aligned)");
    if (!isSupportedROInitializer(Init, DL, /*AllowZeroImage=*/true,
                                  /*AllowStructs=*/true))
      Bad("unsupported initializer (i8/i16/i32 scalars, nonempty arrays of "
          "integers, nonempty non-opaque structs of those at any nesting, "
          "&global pointer leaves, or the ROM zero image)");
    // AS4 keeps CODE-space semantics under the bit-module split as well:
    // .rodata is the same CSEG region to the linker, so a __code image only
    // leaves the *executable* section, never CODE space.
    OutStreamer->switchSection(selectROImageSection());
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
