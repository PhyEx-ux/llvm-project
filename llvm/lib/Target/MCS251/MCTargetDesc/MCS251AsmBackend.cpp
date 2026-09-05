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
//    compiler's three-part long-branch expansion guarantees in-range skips,
//    so a failure here is a backend bug, not user input to accommodate.
//
//  * createObjectTargetWriter is only reached through the *assembly* text
//    path, where the throw-away MCAsmBackend handed to MCAsmStreamer never
//    materializes an object file; the ELF stub writer satisfies that API.
//    The object path bypasses it entirely: MCS251TargetMachine::
//    createMCStreamer supplies the ASxxxx REL writer explicitly.
//
//===----------------------------------------------------------------------===//

#include "MCS251FixupKinds.h"
#include "MCS251MCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCELFObjectWriter.h"
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
public:
  MCS251AsmBackend() : MCAsmBackend(llvm::endianness::big) {}
  ~MCS251AsmBackend() override = default;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    // The assembly streamer constructs a throw-away MCAssembler whose writer
    // is only needed for the AsmPrinter's text path. Object output bypasses
    // this method and supplies the ASxxxx writer explicitly.
    return std::make_unique<MCS251ELFStubWriter>();
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    if (!IsResolved) {
      uint64_t V = Value;
      if (Fixup.getKind() == MCS251::fixup_mcs251_16) {
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
  return new MCS251AsmBackend();
}
