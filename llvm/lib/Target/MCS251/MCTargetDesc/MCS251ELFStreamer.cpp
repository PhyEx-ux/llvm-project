//===-- MCS251ELFStreamer.cpp - MCS251 ELF object streamer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This streamer emits the object identity chosen for the compilation:
//
//  - v1 (deprecated, PM ruling 2026-09-13 #2; produced only under an
//    explicit v1 contract): e_flags = EF_MCS251_ABI_V1 with the 52-byte
//    `.note.mcs251.abi` SHT_NOTE carrier, byte-for-byte unchanged
//    (DESIGN.md N.8).
//
//  - v2 (A4, PM ruling 2026-09-13): the AsmPrinter installs
//    MCS251Attributes::EFlagsV2 (0x102) on the object writer BEFORE
//    initSections runs (codegen path: MCS251AsmPrinter::doInitialization
//    classifies the module first and only then calls the base class, whose
//    initSections follows).  A v2 object carries NO v1 note -- publishing
//    both carriers would let an old reader link a v2 object as v1 -- and
//    its `.mcs251.attributes` section is emitted by the AsmPrinter through
//    MCELFStreamer::emitSelfDescribingAttributesSection with the payload
//    assembled by MCS251Attributes::renderRegisteredIdentity (the codec's
//    production caller).
//
// The streamer deliberately keeps no MCS251 state of its own: the only v1/v2
// signal is the writer's e_flags word, which is exactly the identity bit the
// emitted header will carry, so the two can never disagree.
//
//===----------------------------------------------------------------------===//

#include "MCS251MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MCS251Attributes.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionELF.h"

using namespace llvm;

namespace {
class MCS251ELFStreamer final : public MCELFStreamer {
public:
  using MCELFStreamer::MCELFStreamer;

  void initSections(const MCSubtargetInfo &) override {
    // Generic MCELFStreamer assumes four-byte text alignment. Machine-byte
    // layout is part of the dual-format contract; MCS251 requires only one.
    switchSection(getContext().getObjectFileInfo()->getTextSection());

    // A4/W2: a v2 object announces itself through EFlagsV2 on the writer.
    // It publishes `.mcs251.attributes` instead of the v1 note, so nothing
    // else happens here; see the file header for the full contract.
    if (getWriter().getELFHeaderEFlags() == MCS251Attributes::EFlagsV2)
      return;

    // The v1 identity carrier (N.8), emitted at exactly this historical
    // point and byte-for-byte unchanged.
    MCSection *Previous = getCurrentSectionOnly();
    getWriter().setELFHeaderEFlags(ELF::EF_MCS251_ABI_V1);
    auto *Note = getContext().getELFSection(".note.mcs251.abi", ELF::SHT_NOTE, 0);
    Note->setAlignment(Align(4));
    switchSection(Note);
    emitIntValue(7, 4); // namesz, including NUL
    emitIntValue(32, 4);
    emitIntValue(ELF::NT_MCS251_ABI, 4);
    emitBytes(StringRef("MCS251\0\0", 8));
    // Object version, call ABI major/minor/variant, register mask,
    // small-model/static-parameter/sparse-XINIT flags, reserved words.
    for (uint32_t Value : {1u, 1u, 0u, 2u, 0xf3ffu, 7u, 0u, 0u})
      emitIntValue(Value, 4);
    switchSection(Previous);
  }
};
} // namespace

MCStreamer *llvm::createMCS251ELFStreamer(
    const Triple &, MCContext &Context, std::unique_ptr<MCAsmBackend> &&TAB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return new MCS251ELFStreamer(Context, std::move(TAB), std::move(OW),
                              std::move(Emitter));
}
