//===- LTO.cpp - LTO / ThinLTO integration for mcs251-lld -----------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/BinaryFormat/MCS251Attributes.h"
#include "llvm/BinaryFormat/MCS251Signatures.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/MCS251TargetParser.h"
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::object;

namespace lld::mcs251 {
namespace {

static bool failLTO(raw_ostream &Err, const Twine &Msg) {
  Err << "mcs251-lld: error: " << Msg << '\n';
  return false;
}

static void initTargetsOnce() {
  static std::once_flag Flag;
  std::call_once(Flag, []() {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    InitializeAllAsmParsers();
  });
}

static bool isArchiveBuffer(StringRef Buf) {
  return Buf.starts_with("!<arch>\n") || Buf.starts_with("!<thin>\n");
}

static bool isBitcodeBuffer(StringRef Buf) {
  return identify_magic(Buf) == file_magic::bitcode;
}

static bool matchGlobSimple(StringRef Pat, StringRef Str) {
  size_t P = 0, S = 0, Star = StringRef::npos, Match = 0;
  while (S < Str.size()) {
    if (P < Pat.size() && (Pat[P] == '?' || Pat[P] == Str[S])) {
      ++P;
      ++S;
    } else if (P < Pat.size() && Pat[P] == '*') {
      Star = P++;
      Match = S;
    } else if (Star != StringRef::npos) {
      P = Star + 1;
      S = ++Match;
    } else {
      return false;
    }
  }
  while (P < Pat.size() && Pat[P] == '*')
    ++P;
  return P == Pat.size();
}

static bool isReservedBoundaryName(StringRef N) {
  if (N == "__mcs251_stack_base" || N == "__mcs251_isr_priority_init" ||
      N == "___mcs251_isr_priority_init")
    return true;
  if (N.starts_with("s_") || N.starts_with("l_")) {
    StringRef Rest = N.drop_front(2);
    static const char *Areas[] = {
        "DSEG",       "IRAM",       "HOME",     "VECS",     "BOOT",
        "CSEG",       "XINIT",      "XDATA_INIT", "BITINIT", "OSEG",
        "ISEG",       "SSEG",       "BSEG_BYTES", "BIT_BANK", "EDATA",
        "XSEG",       "REG_BANK_0", "REG_BANK_1", "REG_BANK_2", "REG_BANK_3"};
    for (const char *A : Areas)
      if (Rest == A)
        return true;
  }
  return false;
}

static void collectElfGlobals(MemoryBufferRef MB,
                              std::map<std::string, bool> &Defs,
                              std::set<std::string> &Undefs) {
  Expected<std::unique_ptr<ObjectFile>> ObjOrErr =
      ObjectFile::createObjectFile(MB);
  if (!ObjOrErr) {
    consumeError(ObjOrErr.takeError());
    return;
  }
  auto *ELFObj = dyn_cast<ELF32BEObjectFile>(ObjOrErr->get());
  if (!ELFObj)
    return;
  const ELFFile<ELF32BE> &ELF = ELFObj->getELFFile();
  Expected<ELF32BE::ShdrRange> Shdrs = ELF.sections();
  if (!Shdrs) {
    consumeError(Shdrs.takeError());
    return;
  }
  for (const ELF32BE::Shdr &H : *Shdrs) {
    if (H.sh_type != ELF::SHT_SYMTAB)
      continue;
    Expected<ELF32BE::SymRange> Syms = ELF.symbols(&H);
    Expected<StringRef> Strs = ELF.getStringTableForSymtab(H);
    if (!Syms || !Strs) {
      if (!Syms)
        consumeError(Syms.takeError());
      if (!Strs)
        consumeError(Strs.takeError());
      continue;
    }
    for (const ELF32BE::Sym &Sym : *Syms) {
      uint8_t Bind = Sym.getBinding();
      if (Bind == ELF::STB_LOCAL)
        continue;
      Expected<StringRef> NameOrErr = Sym.getName(*Strs);
      if (!NameOrErr) {
        consumeError(NameOrErr.takeError());
        continue;
      }
      if (NameOrErr->empty())
        continue;
      if (Sym.st_shndx == ELF::SHN_UNDEF) {
        if (Bind == ELF::STB_GLOBAL)
          Undefs.insert(NameOrErr->str());
      } else {
        bool IsStrong = (Bind != ELF::STB_WEAK);
        auto It = Defs.find(NameOrErr->str());
        if (It == Defs.end())
          Defs.emplace(NameOrErr->str(), IsStrong);
        else if (IsStrong)
          It->second = true;
      }
    }
  }
}

static std::optional<uint64_t> mdU32(const MDNode &N, unsigned I) {
  if (I >= N.getNumOperands())
    return std::nullopt;
  const auto *CI = mdconst::dyn_extract<ConstantInt>(N.getOperand(I));
  if (!CI || CI->getBitWidth() > 64)
    return std::nullopt;
  return CI->getZExtValue();
}

static std::string decodeSigNode(const MDNode &N,
                                 MCS251Signatures::Record &Out) {
  if (N.getNumOperands() < MCS251Signatures::MetadataOperandFirstParam)
    return "node has fewer than 3 operands";
  const auto *NameMD =
      dyn_cast<MDString>(N.getOperand(MCS251Signatures::MetadataOperandName));
  if (!NameMD || NameMD->getString().empty())
    return "operand 0 must be a non-empty MDString";
  auto Role = mdU32(N, MCS251Signatures::MetadataOperandRole);
  auto Ret = mdU32(N, MCS251Signatures::MetadataOperandRet);
  if (!Role || *Role > 0xff)
    return "operand 1 must be an i32 role byte";
  if (!Ret || *Ret > 1)
    return "operand 2 must be the i32 return bit-ness";

  uint8_t RoleByte = static_cast<uint8_t>(*Role);
  uint8_t Def = RoleByte & MCS251Signatures::Role_DefinitionMask;
  if (Def != MCS251Signatures::Role_HasDefinition &&
      Def != MCS251Signatures::Role_DeclaredNotDefined)
    return "invalid role definition bits";
  if (RoleByte & MCS251Signatures::Role_ReservedMask)
    return "reserved role bits must be zero";
  const bool NoProto = MCS251Signatures::hasNoPrototype(RoleByte);
  const bool Variadic = MCS251Signatures::isVariadic(RoleByte);
  if (NoProto && Variadic)
    return "a no-prototype record cannot also be variadic";

  const unsigned ParamCount =
      N.getNumOperands() - MCS251Signatures::MetadataOperandFirstParam;
  if (ParamCount > 255)
    return "more than 255 source parameters";
  std::vector<uint8_t> Bitmap(
      MCS251Signatures::bitmapBytes(static_cast<uint8_t>(ParamCount)), 0);
  for (unsigned I = 0; I != ParamCount; ++I) {
    auto Bit = mdU32(N, MCS251Signatures::MetadataOperandFirstParam + I);
    if (!Bit || *Bit > 1)
      return "parameter bit-ness must be 0 or 1";
    if (NoProto && *Bit != 0)
      return "a no-prototype record cannot set a __bit parameter";
    MCS251Signatures::bitmapSet(Bitmap, I, *Bit != 0);
  }
  Out = MCS251Signatures::makeRecord(
      NameMD->getString(), Def == MCS251Signatures::Role_HasDefinition,
      ParamCount, Bitmap, *Ret != 0, NoProto, Variadic,
      uint8_t(MCS251Attributes::CallABIMajor),
      uint8_t(MCS251Attributes::CallABIMinor));
  return {};
}

static MDNode *encodeSigNode(LLVMContext &Ctx,
                             const MCS251Signatures::Record &R) {
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Metadata *, 8> Ops;
  Ops.push_back(MDString::get(Ctx, R.Name));
  Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, R.Role)));
  Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, R.Ret)));
  for (unsigned I = 0; I != R.ParamCount; ++I) {
    unsigned Bit = MCS251Signatures::bitmapGet(R.Bitmap, I) ? 1u : 0u;
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, Bit)));
  }
  return MDNode::get(Ctx, Ops);
}

// Verify that all duplicate entries inside `!mcs251.signatures` (produced when
// IRMover merges multiple bitcode modules) are pairwise ABI-compatible.
static std::string checkMergedSignatures(const Module &M) {
  const NamedMDNode *MD = M.getNamedMetadata(MCS251Signatures::MetadataName);
  if (!MD)
    return {};
  std::map<std::string, std::vector<MCS251Signatures::Record>> Groups;
  for (unsigned I = 0, E = MD->getNumOperands(); I != E; ++I) {
    const MDNode *N = MD->getOperand(I);
    if (!N)
      return "MCS251 LTO: null operand in `!mcs251.signatures`";
    MCS251Signatures::Record R;
    std::string Err = decodeSigNode(*N, R);
    if (!Err.empty())
      return "MCS251 LTO: malformed `!mcs251.signatures` node: " + Err;
    auto &Vec = Groups[R.Name];
    for (const MCS251Signatures::Record &Prev : Vec) {
      if (Error CE = MCS251Signatures::compareRecords(Prev, R, "LTO merge"))
        return "MCS251 LTO: " + toString(std::move(CE));
    }
    Vec.push_back(std::move(R));
  }
  return {};
}

static bool isEmptyLTOShellModule(const Module &M) {
  return M.getDataLayoutStr().empty() && M.empty() && M.global_empty() &&
         M.alias_empty() && M.ifunc_empty() && M.getModuleInlineAsm().empty();
}

// Reconcile `!mcs251.signatures` right before MCS251AsmPrinter runs:
// 1. Deduplicate compatible records merged across bitcode modules (promoting
//    declaration+definition to definition, keeping non-K&R prototype details).
// 2. Align each record's Role with the surviving external functions in M, and
//    drop records whose functions were internalized/eliminated by LTO so
//    LinkerCore::validateFileSignatures never sees a phantom HasDefinition.
// 3. Synthesize records for ThinLTO-promoted `.llvm.*` externalized statics.
class MCS251LTOSignatureReconcilePass final : public ModulePass {
  std::mutex &ErrMu;
  std::vector<std::string> &Errors;

public:
  static char ID;
  MCS251LTOSignatureReconcilePass(std::mutex &Mu,
                                  std::vector<std::string> &Errs)
      : ModulePass(ID), ErrMu(Mu), Errors(Errs) {}

  bool doInitialization(Module &M) override {
    if (isEmptyLTOShellModule(M))
      return false;
    if (std::string CErr =
            llvm::MCS251::verifyModuleContractMessage(M, true);
        !CErr.empty()) {
      std::lock_guard<std::mutex> Lock(ErrMu);
      Errors.push_back(std::move(CErr));
    }

    NamedMDNode *MD = M.getNamedMetadata(MCS251Signatures::MetadataName);
    if (!MD)
      return false;

    std::map<std::string, MCS251Signatures::Record> Merged;
    std::vector<std::string> Order;
    for (unsigned I = 0, E = MD->getNumOperands(); I != E; ++I) {
      const MDNode *N = MD->getOperand(I);
      if (!N)
        continue;
      MCS251Signatures::Record R;
      std::string Err = decodeSigNode(*N, R);
      if (!Err.empty()) {
        std::lock_guard<std::mutex> Lock(ErrMu);
        Errors.push_back("MCS251 LTO: malformed `!mcs251.signatures`: " + Err);
        return false;
      }
      auto It = Merged.find(R.Name);
      if (It == Merged.end()) {
        Order.push_back(R.Name);
        Merged.emplace(R.Name, std::move(R));
      } else {
        if (Error CE = MCS251Signatures::compareRecords(It->second, R,
                                                        "LTO merge")) {
          std::lock_guard<std::mutex> Lock(ErrMu);
          Errors.push_back("MCS251 LTO: " + toString(std::move(CE)));
          return false;
        }
        // Prefer prototyped record over K&R no-prototype, and HasDefinition
        // over DeclaredNotDefined.
        const bool HadDef = MCS251Signatures::isDefinitionRole(It->second.Role) ||
                            MCS251Signatures::isDefinitionRole(R.Role);
        if (MCS251Signatures::hasNoPrototype(It->second.Role) &&
            !MCS251Signatures::hasNoPrototype(R.Role))
          It->second = std::move(R);
        uint8_t Extra = It->second.Role & ~MCS251Signatures::Role_DefinitionMask;
        It->second.Role =
            Extra | (HadDef ? MCS251Signatures::Role_HasDefinition
                            : MCS251Signatures::Role_DeclaredNotDefined);
      }
    }

    Mangler Mang;
    std::map<std::string, const Function *> ExternalFuncs;
    std::set<std::string> LocalFuncNames;
    for (const Function &F : M) {
      if (F.isIntrinsic())
        continue;
      SmallString<64> Mangled;
      Mang.getNameWithPrefix(Mangled, &F, /*CannotUsePrivateLabel=*/false);
      std::string FinalName = Mangled.str().str();
      if (F.hasLocalLinkage()) {
        LocalFuncNames.insert(FinalName);
        continue;
      }
      ExternalFuncs[FinalName] = &F;
    }

    // Add ThinLTO-promoted static functions (`.llvm.<hash>`) that became
    // external symbols during cross-module importing.
    for (const auto &KV : ExternalFuncs) {
      if (Merged.count(KV.first))
        continue;
      if (!StringRef(KV.first).contains(".llvm."))
        continue;
      const Function *F = KV.second;
      unsigned NParams = static_cast<unsigned>(F->arg_size());
      std::vector<uint8_t> Bitmap(
          MCS251Signatures::bitmapBytes(static_cast<uint8_t>(NParams)), 0);
      const bool IsDecl =
          F->isDeclaration() || F->hasAvailableExternallyLinkage();
      MCS251Signatures::Record Syn = MCS251Signatures::makeRecord(
          KV.first, !IsDecl, NParams, Bitmap, /*Ret=*/false,
          /*NoPrototype=*/false, F->isVarArg(),
          uint8_t(MCS251Attributes::CallABIMajor),
          uint8_t(MCS251Attributes::CallABIMinor));
      Order.push_back(KV.first);
      Merged.emplace(KV.first, std::move(Syn));
    }

    MD->clearOperands();
    LLVMContext &Ctx = M.getContext();
    for (const std::string &Name : Order) {
      auto It = Merged.find(Name);
      if (It == Merged.end())
        continue;
      MCS251Signatures::Record &Rec = It->second;
      auto FIt = ExternalFuncs.find(Name);
      if (FIt != ExternalFuncs.end()) {
        const bool IsDecl = FIt->second->isDeclaration() ||
                            FIt->second->hasAvailableExternallyLinkage();
        uint8_t Extra = Rec.Role & ~MCS251Signatures::Role_DefinitionMask;
        Rec.Role = Extra | (IsDecl ? MCS251Signatures::Role_DeclaredNotDefined
                                   : MCS251Signatures::Role_HasDefinition);
        MD->addOperand(encodeSigNode(Ctx, Rec));
      } else {
        // Function was internalized or dead-eliminated by LTO. Drop
        // HasDefinition records (and any record matching a now-local function)
        // so validateFileSignatures stays strictly consistent with .symtab.
        if (!LocalFuncNames.count(Name) &&
            MCS251Signatures::isDeclarationRole(Rec.Role))
          MD->addOperand(encodeSigNode(Ctx, Rec));
      }
    }
    return true;
  }

  bool runOnModule(Module &M) override { return false; }
};

char MCS251LTOSignatureReconcilePass::ID = 0;

struct LTOBitcodeUnit {
  std::string Name;
  std::unique_ptr<MemoryBuffer> OwnedBuffer;
  std::unique_ptr<lto::InputFile> Input;
};

} // namespace

bool runLTOPass(LinkerConfig &Config, unsigned LtoOptLevel, raw_ostream &Err) {
  // Fast check: scan direct inputs and archive member headers for bitcode.
  bool HasAnyBitcode = false;
  struct ScannedInput {
    std::string Path;
    bool WholeArchive = false;
    std::unique_ptr<MemoryBuffer> Buffer;
    bool IsArchive = false;
    bool IsBitcode = false;
    bool ArchiveHasBitcode = false;
  };
  std::vector<ScannedInput> Scanned;
  Scanned.reserve(Config.Inputs.size());

  for (size_t I = 0; I != Config.Inputs.size(); ++I) {
    ScannedInput SI;
    SI.Path = Config.Inputs[I];
    SI.WholeArchive =
        I < Config.InputWholeArchive.size() && Config.InputWholeArchive[I];
    auto MB = MemoryBuffer::getFile(SI.Path);
    if (!MB) {
      // Let LinkerCore report the canonical missing-file error when there is
      // no bitcode at all.
      if (!HasAnyBitcode)
        continue;
      return failLTO(Err, SI.Path + ": cannot read input");
    }
    SI.Buffer = std::move(*MB);
    StringRef Buf = SI.Buffer->getBuffer();
    if (isBitcodeBuffer(Buf)) {
      SI.IsBitcode = true;
      HasAnyBitcode = true;
    } else if (isArchiveBuffer(Buf)) {
      SI.IsArchive = true;
      Expected<std::unique_ptr<Archive>> ArOrErr =
          Archive::create(SI.Buffer->getMemBufferRef());
      if (ArOrErr) {
        Error ChildErr = Error::success();
        for (const Archive::Child &C : (*ArOrErr)->children(ChildErr)) {
          Expected<MemoryBufferRef> MRef = C.getMemoryBufferRef();
          if (!MRef) {
            consumeError(MRef.takeError());
            continue;
          }
          if (isBitcodeBuffer(MRef->getBuffer())) {
            SI.ArchiveHasBitcode = true;
            HasAnyBitcode = true;
            break;
          }
        }
        if (ChildErr)
          consumeError(std::move(ChildErr));
      } else {
        consumeError(ArOrErr.takeError());
      }
    }
    Scanned.push_back(std::move(SI));
  }

  if (!HasAnyBitcode)
    return true;

  // Verify every input could be read before proceeding with LTO.
  if (Scanned.size() != Config.Inputs.size()) {
    for (const std::string &P : Config.Inputs) {
      auto MB = MemoryBuffer::getFile(P);
      if (!MB)
        return failLTO(Err, P + ": cannot read input");
    }
  }

  initTargetsOnce();

  // Track native ELF symbols and lazy archive members (both ELF & bitcode).
  std::map<std::string, bool> NativeDefs; // name -> isStrong
  std::set<std::string> NativeUndefs;
  std::set<std::string> DefinedGlobals;   // all strong+weak defs across ELF+BC
  std::set<std::string> BitcodeUndefs;
  std::vector<LTOBitcodeUnit> BitcodeUnits;
  std::vector<LinkerConfig::MemoryInput> ExtractedElfMembers;
  std::vector<std::string> KeptInputs;
  std::vector<bool> KeptWholeArchive;

  struct PendingArchive {
    std::string Path;
    bool HasBitcode = false;
    std::unique_ptr<MemoryBuffer> Buffer;
    std::unique_ptr<Archive> Ar;
    std::map<std::string, uint64_t> SymToOffset;
    std::map<uint64_t, Archive::Child> OffsetToChild;
    std::set<uint64_t> ExtractedOffsets;
  };
  std::vector<PendingArchive> Archives;

  auto AddBitcodeBuffer = [&](StringRef Name,
                              std::unique_ptr<MemoryBuffer> Owned) -> bool {
    Expected<std::unique_ptr<lto::InputFile>> IFOrErr =
        lto::InputFile::create(Owned->getMemBufferRef());
    if (!IFOrErr)
      return failLTO(Err, Name + ": invalid bitcode input: " +
                              toString(IFOrErr.takeError()));
    for (const lto::InputFile::Symbol &Sym : (*IFOrErr)->symbols()) {
      std::string SName = Sym.getName().str();
      if (SName.empty())
        continue;
      if (Sym.isUndefined()) {
        if (!Sym.isWeak())
          BitcodeUndefs.insert(SName);
      } else {
        DefinedGlobals.insert(SName);
      }
    }
    BitcodeUnits.push_back({Name.str(), std::move(Owned), std::move(*IFOrErr)});
    return true;
  };

  auto ExtractArchiveMember = [&](PendingArchive &PA, uint64_t Off,
                                  const Archive::Child &C) -> bool {
    if (!PA.ExtractedOffsets.insert(Off).second)
      return true;
    std::string MemberName;
    if (Expected<StringRef> NameOrErr = C.getName()) {
      MemberName = NameOrErr->str();
    } else {
      consumeError(NameOrErr.takeError());
      MemberName = "<member>";
    }
    std::string DisplayPath = PA.Path + "(" + MemberName + ")";
    Expected<MemoryBufferRef> MRef = C.getMemoryBufferRef();
    if (!MRef)
      return failLTO(Err, DisplayPath + ": " + toString(MRef.takeError()));
    if (isBitcodeBuffer(MRef->getBuffer())) {
      auto Copy =
          MemoryBuffer::getMemBufferCopy(MRef->getBuffer(), DisplayPath);
      return AddBitcodeBuffer(DisplayPath, std::move(Copy));
    }
    // Native ELF member inside an archive.
    collectElfGlobals(*MRef, NativeDefs, NativeUndefs);
    for (const auto &KV : NativeDefs)
      DefinedGlobals.insert(KV.first);
    // Only push into MemoryInputs when this archive contains bitcode members
    // (and thus won't be re-fed into LinkerCore); pure-ELF archives stay in
    // KeptInputs so LinkerCore handles them natively (including libcalls).
    if (PA.HasBitcode) {
      ExtractedElfMembers.push_back(
          {DisplayPath, MRef->getBuffer().str()});
    }
    return true;
  };

  for (ScannedInput &SI : Scanned) {
    if (SI.IsBitcode) {
      if (!AddBitcodeBuffer(SI.Path, std::move(SI.Buffer)))
        return false;
      continue;
    }
    if (!SI.IsArchive) {
      collectElfGlobals(SI.Buffer->getMemBufferRef(), NativeDefs, NativeUndefs);
      for (const auto &KV : NativeDefs)
        DefinedGlobals.insert(KV.first);
      KeptInputs.push_back(SI.Path);
      KeptWholeArchive.push_back(SI.WholeArchive);
      continue;
    }

    // Archive (pure ELF or mixed/bitcode).
    if (!SI.ArchiveHasBitcode) {
      KeptInputs.push_back(SI.Path);
      KeptWholeArchive.push_back(SI.WholeArchive);
    }
    Expected<std::unique_ptr<Archive>> ArOrErr =
        Archive::create(SI.Buffer->getMemBufferRef());
    if (!ArOrErr)
      return failLTO(Err, SI.Path + ": invalid archive: " +
                              toString(ArOrErr.takeError()));
    PendingArchive PA;
    PA.Path = SI.Path;
    PA.HasBitcode = SI.ArchiveHasBitcode;
    PA.Buffer = std::move(SI.Buffer);
    PA.Ar = std::move(*ArOrErr);
    std::vector<std::pair<uint64_t, Archive::Child>> Ordered;
    Error ChildErr = Error::success();
    for (const Archive::Child &C : PA.Ar->children(ChildErr)) {
      uint64_t Off = C.getChildOffset();
      Ordered.emplace_back(Off, C);
      PA.OffsetToChild.emplace(Off, C);
    }
    if (ChildErr)
      return failLTO(Err, SI.Path + ": invalid archive member: " +
                              toString(std::move(ChildErr)));

    if (SI.WholeArchive) {
      for (const auto &Entry : Ordered)
        if (!ExtractArchiveMember(PA, Entry.first, Entry.second))
          return false;
      continue;
    }

    if (PA.Ar->hasSymbolTable()) {
      for (const Archive::Symbol &Sym : PA.Ar->symbols()) {
        Expected<Archive::Child> MemOrErr = Sym.getMember();
        if (!MemOrErr)
          return failLTO(Err, SI.Path + ": invalid archive symbol table: " +
                                  toString(MemOrErr.takeError()));
        PA.SymToOffset.try_emplace(Sym.getName().str(),
                                   MemOrErr->getChildOffset());
      }
    } else {
      for (const auto &Entry : Ordered) {
        Expected<MemoryBufferRef> MRef = Entry.second.getMemoryBufferRef();
        if (!MRef) {
          consumeError(MRef.takeError());
          continue;
        }
        if (isBitcodeBuffer(MRef->getBuffer())) {
          Expected<std::unique_ptr<lto::InputFile>> IFOrErr =
              lto::InputFile::create(*MRef);
          if (!IFOrErr) {
            consumeError(IFOrErr.takeError());
            continue;
          }
          for (const lto::InputFile::Symbol &Sym : (*IFOrErr)->symbols())
            if (!Sym.isUndefined() && !Sym.getName().empty())
              PA.SymToOffset.try_emplace(Sym.getName().str(), Entry.first);
        } else {
          std::map<std::string, bool> MDefs;
          std::set<std::string> MUndefs;
          collectElfGlobals(*MRef, MDefs, MUndefs);
          for (const auto &KV : MDefs)
            PA.SymToOffset.try_emplace(KV.first, Entry.first);
        }
      }
    }
    Archives.push_back(std::move(PA));
  }

  // Lazy archive resolution fixpoint across native ELF + bitcode symbols.
  bool Progress = !Archives.empty();
  while (Progress) {
    Progress = false;
    std::vector<std::string> Unresolved;
    std::set<std::string> Seen;
    auto AddUndef = [&](const std::string &N) {
      if (!N.empty() && !isReservedBoundaryName(N) &&
          !DefinedGlobals.count(N) && Seen.insert(N).second)
        Unresolved.push_back(N);
    };
    if (!Config.EntrySymbol.empty())
      AddUndef(Config.EntrySymbol);
    for (const std::string &U : Config.UndefinedRoots)
      AddUndef(U);
    for (const std::string &U : NativeUndefs)
      AddUndef(U);
    for (const std::string &U : BitcodeUndefs)
      AddUndef(U);
    if (Unresolved.empty())
      break;
    for (PendingArchive &PA : Archives) {
      bool Extracted = false;
      for (const std::string &SymName : Unresolved) {
        if (DefinedGlobals.count(SymName))
          continue;
        auto It = PA.SymToOffset.find(SymName);
        if (It == PA.SymToOffset.end())
          continue;
        uint64_t Off = It->second;
        if (PA.ExtractedOffsets.count(Off))
          continue;
        auto CIt = PA.OffsetToChild.find(Off);
        if (CIt == PA.OffsetToChild.end())
          continue;
        if (!ExtractArchiveMember(PA, Off, CIt->second))
          return false;
        Progress = true;
        Extracted = true;
      }
      if (Extracted)
        break;
    }
  }

  if (BitcodeUnits.empty()) {
    // Bitcode lived only in unextracted archive members.
    Config.Inputs = std::move(KeptInputs);
    Config.InputWholeArchive = std::move(KeptWholeArchive);
    for (auto &M : ExtractedElfMembers)
      Config.MemoryInputs.push_back(std::move(M));
    return true;
  }

  // Compute prevailing symbol ownership across native ELF and bitcode units.
  struct SymOwner {
    bool FromNative = false;
    bool IsStrong = false;
    size_t UnitIdx = 0;
    size_t SymIdx = 0;
  };
  std::map<std::string, SymOwner> Winners;
  for (const auto &KV : NativeDefs)
    Winners[KV.first] = {true, KV.second, 0, 0};

  for (size_t U = 0; U != BitcodeUnits.size(); ++U) {
    ArrayRef<lto::InputFile::Symbol> Syms = BitcodeUnits[U].Input->symbols();
    for (size_t S = 0; S != Syms.size(); ++S) {
      const auto &Sym = Syms[S];
      if (Sym.isUndefined() || Sym.getName().empty())
        continue;
      std::string Name = Sym.getName().str();
      const bool Strong = !Sym.isWeak() && !Sym.isCommon();
      auto It = Winners.find(Name);
      if (It == Winners.end()) {
        Winners[Name] = {false, Strong, U, S};
      } else {
        if (It->second.IsStrong && Strong)
          return failLTO(Err, "duplicate definition of " + Name);
        if (!It->second.IsStrong && Strong)
          It->second = {false, true, U, S};
      }
    }
  }

  std::set<std::string> ScriptDefined;
  for (const auto &SS : Config.ScriptSymbols)
    ScriptDefined.insert(SS.Name);

  std::set<std::string> RootVisible;
  if (!Config.EntrySymbol.empty())
    RootVisible.insert(Config.EntrySymbol);
  else
    RootVisible.insert("_main");
  RootVisible.insert("__sdcc_gsinit_startup");
  RootVisible.insert("_start");
  RootVisible.insert("__mcs251_isr_default");
  RootVisible.insert("__mcs251_bit_init");
  RootVisible.insert("___mcs251_isr_priority_init");
  for (const std::string &U : Config.UndefinedRoots)
    RootVisible.insert(U);
  for (const std::string &U : NativeUndefs)
    RootVisible.insert(U);

  std::mutex HookMu;
  std::vector<std::string> HookErrors;

  lto::Config Conf;
  Conf.RelocModel = Reloc::Static;
  Conf.CGFileType = CodeGenFileType::ObjectFile;
  Conf.OptLevel = std::min(LtoOptLevel, 3u);
  switch (Conf.OptLevel) {
  case 0:
    Conf.CGOptLevel = CodeGenOptLevel::None;
    break;
  case 1:
    Conf.CGOptLevel = CodeGenOptLevel::Less;
    break;
  case 2:
    Conf.CGOptLevel = CodeGenOptLevel::Default;
    break;
  default:
    Conf.CGOptLevel = CodeGenOptLevel::Aggressive;
    break;
  }
  Conf.Freestanding = true;
  Conf.PTO.LoopVectorization = false;
  Conf.PTO.SLPVectorization = false;
  Conf.Options.FunctionSections = Config.GcSections;
  Conf.Options.DataSections = Config.GcSections;
  Conf.Options.EmitAddrsig = false;
  Conf.DiagHandler = [&](const DiagnosticInfo &DI) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
    OS.flush();
    if (DI.getSeverity() == DS_Error) {
      std::lock_guard<std::mutex> Lock(HookMu);
      HookErrors.push_back(std::move(Msg));
    } else if (DI.getSeverity() == DS_Warning) {
      Err << "mcs251-lld: warning: " << Msg << '\n';
    }
  };
  Conf.PreOptModuleHook = [&](unsigned /*Task*/, const Module &M) {
    if (isEmptyLTOShellModule(M))
      return true;
    if (std::string SErr = checkMergedSignatures(M); !SErr.empty()) {
      std::lock_guard<std::mutex> Lock(HookMu);
      HookErrors.push_back(std::move(SErr));
      return false;
    }
    if (std::string CErr =
            llvm::MCS251::verifyModuleContractMessage(M, true);
        !CErr.empty()) {
      std::lock_guard<std::mutex> Lock(HookMu);
      HookErrors.push_back(std::move(CErr));
      return false;
    }
    return true;
  };
  Conf.PostOptModuleHook = [&](unsigned /*Task*/, const Module &M) {
    if (isEmptyLTOShellModule(M))
      return true;
    if (std::string CErr =
            llvm::MCS251::verifyModuleContractMessage(M, true);
        !CErr.empty()) {
      std::lock_guard<std::mutex> Lock(HookMu);
      HookErrors.push_back(std::move(CErr));
      return false;
    }
    return true;
  };
  Conf.PreCodeGenPassesHook = [&](legacy::PassManager &PM) {
    PM.add(new MCS251LTOSignatureReconcilePass(HookMu, HookErrors));
  };

  lto::ThinBackend ThinBE =
      lto::createInProcessThinBackend(heavyweight_hardware_concurrency());
  lto::LTO LtoObj(std::move(Conf), std::move(ThinBE));

  for (size_t U = 0; U != BitcodeUnits.size(); ++U) {
    ArrayRef<lto::InputFile::Symbol> Syms = BitcodeUnits[U].Input->symbols();
    std::vector<lto::SymbolResolution> Resols(Syms.size());
    for (size_t S = 0; S != Syms.size(); ++S) {
      const auto &Sym = Syms[S];
      lto::SymbolResolution &R = Resols[S];
      StringRef Name = Sym.getName();
      auto It = Winners.find(Name.str());
      R.Prevailing = !Sym.isUndefined() && It != Winners.end() &&
                     !It->second.FromNative && It->second.UnitIdx == U &&
                     It->second.SymIdx == S;
      StringRef Sec = Sym.getSectionName();
      bool SpecialSec = Sec.starts_with(".mcs251.isr") ||
                        Sec.starts_with(".mcu.fixed.") ||
                        Sec.starts_with(".mcs251.home") ||
                        Sec.starts_with(".mcs251.boot") ||
                        Sec.starts_with(".mcs251.vecs");
      bool KeepByScript = false;
      if (!Config.KeepSectionPatterns.empty()) {
        std::string FnSec = ".text." + Name.str();
        for (const std::string &Pat : Config.KeepSectionPatterns) {
          if (matchGlobSimple(Pat, FnSec) ||
              (!Sec.empty() && matchGlobSimple(Pat, Sec))) {
            KeepByScript = true;
            break;
          }
        }
      }
      R.VisibleToRegularObj = RootVisible.count(Name.str()) || Sym.isUsed() ||
                              SpecialSec || KeepByScript;
      R.FinalDefinitionInLinkageUnit =
          It != Winners.end() && !ScriptDefined.count(Name.str());
      R.ExportDynamic = false;
      R.LinkerRedefined = ScriptDefined.count(Name.str()) != 0;
    }
    if (Error E = LtoObj.add(std::move(BitcodeUnits[U].Input), Resols))
      return failLTO(Err, BitcodeUnits[U].Name + ": LTO add failed: " +
                              toString(std::move(E)));
  }

  unsigned MaxTasks = LtoObj.getMaxTasks();
  std::vector<std::pair<std::string, SmallVector<char, 0>>> TaskOutputs(
      MaxTasks);
  Error RunErr = LtoObj.run([&](size_t Task, const Twine &ModuleName) {
    TaskOutputs[Task].first = ModuleName.str();
    return std::make_unique<CachedFileStream>(
        std::make_unique<raw_svector_ostream>(TaskOutputs[Task].second));
  });
  if (RunErr)
    return failLTO(Err, "LTO failed: " + toString(std::move(RunErr)));
  if (!HookErrors.empty()) {
    for (const std::string &E : HookErrors)
      failLTO(Err, E);
    return false;
  }

  Config.Inputs = std::move(KeptInputs);
  Config.InputWholeArchive = std::move(KeptWholeArchive);
  for (auto &M : ExtractedElfMembers)
    Config.MemoryInputs.push_back(std::move(M));
  for (unsigned I = 0; I != MaxTasks; ++I) {
    if (TaskOutputs[I].second.empty())
      continue;
    std::string ObjName = TaskOutputs[I].first.empty()
                              ? ("<lto-obj-" + std::to_string(I) + ">")
                              : ("<lto:" + TaskOutputs[I].first + ">");
    Config.MemoryInputs.push_back(
        {std::move(ObjName),
         std::string(TaskOutputs[I].second.data(),
                     TaskOutputs[I].second.size())});
  }
  return true;
}

} // namespace lld::mcs251
