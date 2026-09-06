//===-- MCS251ELFStreamer.cpp - MCS251 ELF object streamer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
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
    getWriter().setELFHeaderEFlags(ELF::EF_MCS251_ABI_V1);
    MCSection *Previous = getCurrentSectionOnly();
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
