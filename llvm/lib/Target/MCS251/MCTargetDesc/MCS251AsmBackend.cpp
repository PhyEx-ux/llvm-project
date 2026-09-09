//===-- MCS251AsmBackend.cpp - MCS-251 assembler backend -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MC assembler backend for the MCS-251 (Phase 13a).  Responsibilities:
//
//  * Fixup application.  Only two target fixups exist (MCS251FixupKinds.h):
//    16-bit and 24-bit big-endian symbol/area values.  When a fixup is not
//    fully resolvable at assembly time (any symbolic target -- MC never
//    resolves non-PCRel symbol fixups), the Value written into the payload
//    is the area offset plus addend for defined symbols and the bare addend
//    for undefined ones, and the relocation is recorded for the ASxxxx REL
//    writer.  This fork's MCAssembler convention lets applyFixup do the
//    recording itself (there is no central recordRelocation call).
//
//  * PC-relative byte branches (FK_Data_1, created by the code emitter for
//    the rel8 jump instructions).  MC's PC-relative Value is target minus
//    the displacement byte's position, while the MCS-251 rel8 is measured
//    from the byte AFTER the two-byte instruction, hence the extra -1.
//    Range overflow (a rel8 that no longer reaches) is a hard error: the
//    compiler's long-branch expansion plus the post-layout BranchRelaxation
//    pass (driven by the TargetInstrInfo branch hooks) guarantee that every
//    rel8 branch fits, so a failure here is a backend bug, not user input to
//    accommodate.
//
//  * createObjectTargetWriter supplies real ELF only for the opt-in format.
//    Legacy assembly still uses the throw-away stub, and REL objects retain
//    the explicitly supplied writer. ELF records RELA before applying its
//    zero FixedValue; REL needs the old area-relative payload, so its fixup
//    application order below is deliberately unchanged.
//
//===----------------------------------------------------------------------===//

#include "MCS251FixupKinds.h"
#include "MCS251MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class MCS251ELFStubWriter final : public MCELFObjectTargetWriter {
public:
  MCS251ELFStubWriter()
      : MCELFObjectTargetWriter(/*Is64Bit=*/false,
                                /*OSABI=*/ELF::ELFOSABI_NONE,
                                /*EMachine=*/ELF::EM_8051,
                                /*HasRelocationAddend=*/true) {}

  unsigned getRelocType(const MCFixup &, const MCValue &, bool) const override {
    return 0;
  }
};

class MCS251AsmBackend final : public MCAsmBackend {
  const bool IsELF;

public:
  explicit MCS251AsmBackend(bool IsELF)
      : MCAsmBackend(llvm::endianness::big), IsELF(IsELF) {}
  ~MCS251AsmBackend() override = default;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    // The REL path bypasses this method. Its assembly streamer only needs a
    // throw-away writer; ELF output instead uses the native target writer.
    if (IsELF)
      return createMCS251ELFObjectWriter();
    return std::make_unique<MCS251ELFStubWriter>();
  }

  // The A3.4 ISR metadata association is a literal relocation: `.reloc`
  // spells the frozen protocol name and the fixup carries relocation number
  // 9 directly. No target fixup mode exists for it (zero write width, no
  // address arithmetic, exact symbol association only).
  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    if (Name == "R_MCS251_ISR_REF")
      return MCFixupKind(FirstLiteralRelocationKind + ELF::R_MCS251_ISR_REF);
    return MCAsmBackend::getFixupKind(Name);
  }

  static bool isISRRefFixup(MCFixupKind Kind) {
    return Kind == MCFixupKind(FirstLiteralRelocationKind +
                               ELF::R_MCS251_ISR_REF);
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    if (!IsResolved && IsELF) {
      if (Target.getSubSym()) {
        getContext().reportError(Fixup.getLoc(),
                                 "MCS251 ELF: symbol-difference relocations are "
                                 "not supported");
        return;
      }
      // This fork delegates relocation recording to the backend. RELA owns
      // the addend: record FIRST, then apply the returned FixedValue (zero),
      // never the section offset which the REL writer needs in its payload.
      Asm->getWriter().recordRelocation(F, Fixup, Target, Value);
      // Literal ISR_REF relocation: zero write width. The four bytes at the
      // 24B record's symbol_reference field were emitted as literal zeros
      // and must stay zero; no byte of the record -- and nothing beyond it
      // -- is ever accessed.
      if (isISRRefFixup(Fixup.getKind()))
        return;
      unsigned Width;
      switch (Fixup.getKind()) {
      case FK_Data_1:
      case MCS251::fixup_mcs251_lo8:
      case MCS251::fixup_mcs251_mid8:
      case MCS251::fixup_mcs251_hi8:
        Width = 1;
        break;
      case FK_Data_2:
      case MCS251::fixup_mcs251_16:
        Width = 2;
        break;
      case MCS251::fixup_mcs251_24:
        Width = 3;
        break;
      default:
        llvm_unreachable("ELF writer rejected unsupported fixup");
      }
      for (unsigned I = 0; I != Width; ++I)
        Data[I] = uint8_t(Value >> (8 * (Width - I - 1)));
      return;
    }
    if (!IsResolved) {
      // The REL path gains no type-9 support: ISR records never reach it
      // (the AsmPrinter hard-rejects ISR object output first), and a
      // hand-written .reloc in a REL module is rejected here as well.
      if (isISRRefFixup(Fixup.getKind())) {
        getContext().reportError(Fixup.getLoc(),
                                 "MCS251 ISR requires ELF object output");
        return;
      }
      uint64_t V = Value;
      if (Fixup.getKind() == FK_Data_2 ||
          Fixup.getKind() == MCS251::fixup_mcs251_16) {
        Data[0] = uint8_t(V >> 8);
        Data[1] = uint8_t(V);
      } else if (Fixup.getKind() == MCS251::fixup_mcs251_24) {
        Data[0] = uint8_t(V >> 16);
        Data[1] = uint8_t(V >> 8);
        Data[2] = uint8_t(V);
      }
      Asm->getWriter().recordRelocation(F, Fixup, Target, Value);
      return;
    }

    uint64_t V = Value;
    switch (Fixup.getKind()) {
    case FK_Data_1: {
      int64_t D = static_cast<int64_t>(V);
      if (Fixup.isPCRel()) {
        // MCAssembler computes target - the displacement byte. MCS251's
        // rel8 base is the byte after the two-byte instruction.
        --D;
      }
      if (Fixup.isPCRel() && !isInt<8>(D))
        getContext().reportError(Fixup.getLoc(),
                                 "MCS251 PC-relative branch out of range");
      Data[0] |= uint8_t(D);
      return;
    }
    case FK_Data_2:
      Data[0] |= uint8_t(V >> 8);
      Data[1] |= uint8_t(V);
      return;
    case FK_Data_4:
      Data[0] |= uint8_t(V >> 24);
      Data[1] |= uint8_t(V >> 16);
      Data[2] |= uint8_t(V >> 8);
      Data[3] |= uint8_t(V);
      return;
    case MCS251::fixup_mcs251_lo8:
    case MCS251::fixup_mcs251_mid8:
    case MCS251::fixup_mcs251_hi8:
      Data[0] = uint8_t(V >> (8 * (Fixup.getKind() - MCS251::fixup_mcs251_lo8)));
      return;
    case MCS251::fixup_mcs251_16:
      if (!isIntN(16, static_cast<int64_t>(V)) && !isUIntN(16, V))
        getContext().reportError(Fixup.getLoc(),
                                 "MCS251 16-bit fixup out of range");
      V &= 0xffff;
      Data[0] |= uint8_t(V >> 8);
      Data[1] |= uint8_t(V);
      return;
    case MCS251::fixup_mcs251_24:
      if (!isUIntN(24, V))
        getContext().reportError(Fixup.getLoc(),
                                 "MCS251 24-bit fixup out of range");
      V &= 0xffffff;
      Data[0] |= uint8_t(V >> 16);
      Data[1] |= uint8_t(V >> 8);
      Data[2] |= uint8_t(V);
      return;
    default:
      llvm_unreachable("unknown MCS251 fixup");
    }
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // Literal relocations write no bytes: the only one this target names is
    // R_MCS251_ISR_REF, whose info width is frozen at 0.
    if (isISRRefFixup(Kind))
      return {"R_MCS251_ISR_REF", 0, 0, 0};
    static const MCFixupKindInfo Infos[MCS251::NumTargetFixupKinds -
                                       FirstTargetFixupKind] = {
        {"fixup_mcs251_16", 0, 16, 0},
        {"fixup_mcs251_24", 0, 24, 0},
        {"fixup_mcs251_lo8", 0, 8, 0},
        {"fixup_mcs251_mid8", 0, 8, 0},
        {"fixup_mcs251_hi8", 0, 8, 0},
    };
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    assert(unsigned(Kind - FirstTargetFixupKind) < std::size(Infos) &&
           "invalid MCS251 fixup kind");
    return Infos[Kind - FirstTargetFixupKind];
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    OS.write_zeros(Count);
    return true;
  }
};
} // namespace

MCAsmBackend *llvm::createMCS251MCAsmBackend(
    const Target &, const MCSubtargetInfo &, const MCRegisterInfo &,
    const MCTargetOptions &) {
  return createMCS251MCAsmBackend(MCS251::getObjectFormat());
}

MCAsmBackend *llvm::createMCS251MCAsmBackend(MCS251::ObjectFormat Format) {
  return new MCS251AsmBackend(Format == MCS251::ObjectFormat::ELF);
}
