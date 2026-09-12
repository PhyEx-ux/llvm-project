//===-- MCS251ELFStreamer.cpp - MCS251 ELF object streamer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This streamer emits the v1 object identity: e_flags = EF_MCS251_ABI_V1
// with the 40-byte `.note.mcs251.abi` SHT_NOTE carrier, byte-for-byte
// unchanged (DESIGN.md N.8).
//
// X3-R1: no v2 identity carrier is emitted.  DESIGN.md N.9 keeps the
// complete v2 identity value domain open (call ABI, register variant, the
// init/placement/stack/function subprotocols, capability words, ABI
// options, code profile), so publishing a candidate value set as a
// production object identity is forbidden; the AsmPrinter's module
// classification hard-rejects any module outside the v1 identity before a
// byte reaches this streamer.  The `.mcs251.attributes` byte format
// (DESIGN.md N.3-N.6) remains specified and is validated as a STRUCTURE
// codec by its own unit tests (llvm/unittests/BinaryFormat/
// MCS251AttributesTest.cpp); it deliberately has no production caller and
// no MCContext wiring -- MCS-251 state must not leak into the generic
// MCContext (P1-2).  The gate may only be reopened once the identity
// fields and subprotocols are approved.
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

    // The v1 identity carrier (N.8), emitted at exactly this historical
    // point and byte-for-byte unchanged.  This is the only identity this
    // streamer produces (see the file header note).
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
