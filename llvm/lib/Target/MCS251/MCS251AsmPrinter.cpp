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
#include "MCS251MCInstLower.h"
#include "MCS251TargetMachine.h"
#include "MCS251TargetObjectFile.h"
#include "MCTargetDesc/MCS251ABISignature.h"
#include "TargetInfo/MCS251TargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
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

  const MCS251TargetMachine &getMCS251TM() const {
    return static_cast<const MCS251TargetMachine &>(TM);
  }

  bool usesELFObjects() const { return getMCS251TM().usesELFObjects(); }

  static bool hasV1PointerTypes(Type *Ty,
                                SmallPtrSetImpl<Type *> &Seen) {
    // Check a pointer before consulting Seen.  An unsupported pointer type is
    // rejected on every encounter, rather than becoming acceptable merely
    // because a previous walk inserted it before returning false.
    if (auto *PT = dyn_cast<PointerType>(Ty))
      return PT->getAddressSpace() == 0;
    if (!Seen.insert(Ty).second)
      return true;
    for (Type *SubTy : Ty->subtypes())
      if (!hasV1PointerTypes(SubTy, Seen))
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
      SmallPtrSetImpl<const Constant *> &SeenConstants) {
    if (!SeenConstants.insert(C).second)
      return true;
    if (!hasV1PointerTypes(C->getType(), SeenTypes))
      return false;
    for (const Use &U : C->operands()) {
      if (!hasV1PointerTypes(U->getType(), SeenTypes))
        return false;
      if (const auto *Child = dyn_cast<Constant>(U.get()))
        if (!hasV1ObjectCompatibleConstant(Child, SeenTypes, SeenConstants))
          return false;
    }
    return true;
  }

  bool isV1ObjectCompatible(const Module &M) const {
    const auto &Contract = getMCS251TM().Options.MCS251Memory;
    if (!Contract.isSpecified() || Contract.ASLayoutVersion == 1)
      return true;
    // Of the layout-v2 requests, only the 32-bit/InternalExtended profile can
    // be downgraded to the implemented v1 placement and pointer ABI. Tiny,
    // Small/InternalMovable and Large/ExternalData require v2 identity even if
    // this particular module happens not to define storage.
    if (M.getDataLayout().getPointerSizeInBits(0) != 32 ||
        Contract.DefaultPlacement != 8 || !M.alias_empty() ||
        !M.ifunc_empty())
      return false;

    SmallPtrSet<Type *, 32> SeenTypes;
    SmallPtrSet<const Constant *, 32> SeenConstants;
    for (const GlobalVariable &GV : M.globals()) {
      if (GV.getAddressSpace() != 0 ||
          !hasV1PointerTypes(GV.getValueType(), SeenTypes))
        return false;
      if (GV.hasInitializer() && !hasV1ObjectCompatibleConstant(
                                     GV.getInitializer(), SeenTypes,
                                     SeenConstants))
        return false;
    }
    for (const Function &F : M) {
      if (!hasV1PointerTypes(F.getFunctionType(), SeenTypes))
        return false;
      unsigned Index = 0;
      for (const Argument &Arg : F.args())
        if (Index++ && Arg.getType()->isPointerTy())
          return false; // v2-sized static pointer slot.
      for (const BasicBlock &BB : F)
        for (const Instruction &I : BB) {
          if (!hasV1PointerTypes(I.getType(), SeenTypes))
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
            if (!hasV1PointerTypes(U->getType(), SeenTypes))
              return false;
            if (const auto *C = dyn_cast<Constant>(U.get()))
              if (!hasV1ObjectCompatibleConstant(C, SeenTypes,
                                                  SeenConstants))
                return false;
          }
        }
    }
    return true;
  }

  void emitASxxxxText(const Twine &Text) {
    if (!usesELFObjects())
      OutStreamer->emitRawText(Text);
  }

  static bool isSupportedMutableType(Type *Ty) {
    if (Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32))
      return true;
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      return AT->getNumElements() &&
             isSupportedMutableType(AT->getElementType());
    }
    if (auto *ST = dyn_cast<StructType>(Ty)) {
      if (ST->isOpaque() || ST->getNumElements() == 0)
        return false;
      return llvm::all_of(ST->elements(), isSupportedMutableType);
    }
    return false;
  }

  static bool isSupportedMutableInitializer(const Constant *C) {
    Type *Ty = C->getType();
    if (!isSupportedMutableType(Ty))
      return false;
    if (isa<ConstantAggregateZero>(C))
      return true;
    if (isa<ConstantInt>(C))
      return true;
    if (!isa<ArrayType>(Ty) && !isa<StructType>(Ty))
      return false;
    unsigned Elements = Ty->isArrayTy()
                            ? cast<ArrayType>(Ty)->getNumElements()
                            : cast<StructType>(Ty)->getNumElements();
    for (unsigned I = 0; I != Elements; ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (!Element || !isSupportedMutableInitializer(Element))
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

  // Read-only CSEG data: i8/i16/i32 scalars and (possibly nested) arrays of
  // them, nonempty at every level, every leaf a ConstantInt.  Struct
  // aggregates, zeroinitializers, undef elements and initializer relocations
  // (pointer tables) are all rejected here and reported by the caller's
  // policy message.
  static bool isSupportedROInitializer(const Constant *C) {
    Type *Ty = C->getType();
    if (isa<ConstantInt>(C))
      return Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32);
    auto *AT = dyn_cast<ArrayType>(Ty);
    if (!AT || !AT->getNumElements() ||
        (!isa<ConstantDataArray>(C) && !isa<ConstantArray>(C)))
      return false;
    for (unsigned I = 0; I != AT->getNumElements(); ++I) {
      const Constant *Element = C->getAggregateElement(I);
      if (!Element || !isSupportedROInitializer(Element))
        return false;
    }
    return true;
  }

  // Emits the CSEG byte image of a read-only initializer: scalars in the
  // established target (big-endian) memory order, nested arrays element by
  // element with stride padding (zero for the packed integer layouts this
  // path accepts).  The image is always byte-aligned: any IR-level alignment
  // above 1 on the global is deliberately demoted, because MCS-251 needs no
  // address alignment for word accesses (QEMU + real hardware verified).
  void emitROInitializer(const DataLayout &DL, const Constant *C) {
    if (auto *CI = dyn_cast<ConstantInt>(C)) {
      OutStreamer->emitIntValue(CI->getZExtValue(),
                                DL.getTypeStoreSize(C->getType()));
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
    SetupMachineFunction(MF);
    emitParameterSlots(MF);
    emitFunctionBody();
    return false;
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
                 !Ty->isIntegerTy(32)) {
        report_fatal_error("MCS251: static parameters require i8/i16/i32 or "
                           "an ordinary data/CODE pointer");
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
    MCS251_MC::verifyInstructionPredicates(MI->getOpcode(),
                                           getSubtargetInfo().getFeatureBits());

    MCS251MCInstLower MCInstLowering(*this);
    MCInst TmpInst;
    MCInstLowering.Lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
  }

  void emitStartOfAsmFile(Module &M) override {
    // ASxxxx module prologue.  ".source" is emitted bare, exactly like the
    // validated specimen and the smoke crt0 template (sdas251 accepts it
    // without a file argument; a filename argument was never exercised).
    const std::string ModuleName = getMCS251ModuleName(M);
    bool V1Compatible = isV1ObjectCompatible(M);
    if (getMCS251TM().emitsObjectFile() && !V1Compatible)
      report_fatal_error(
          "MCS251: module uses an ABI capability that cannot be represented "
          "by the v1 relocatable-object identity; v2 object output is not "
          "implemented");
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
    if (llvm::any_of(M, [](const Function &F) {
          return !F.isDeclaration() && F.arg_size() > 1;
        }) || llvm::any_of(M.globals(), [](const GlobalVariable &GV) {
          return !GV.isDeclaration() && !GV.isConstant();
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
  }

  void emitGlobalVariable(const GlobalVariable *GV) override {
    if (GV->isDeclaration()) {
      AsmPrinter::emitGlobalVariable(GV);
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
    const DataLayout &DL = GV->getDataLayout();
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
    if (!GV->isConstant()) {
      if (!isSupportedMutableInitializer(Init))
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

    if (!isSupportedROInitializer(Init))
      Reject();

    // Read-only globals and string literals stay in the established CSEG path.
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
