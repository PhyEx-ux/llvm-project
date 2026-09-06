//===-- MCS251ELFObjectWriter.cpp - MCS251 ELF writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251FixupKinds.h"
#include "MCS251MCTargetDesc.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {
class MCS251ELFObjectWriter final : public MCELFObjectTargetWriter {
public:
  MCS251ELFObjectWriter()
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, ELF::ELFOSABI_NONE,
                                ELF::EM_MCS251,
                                /*HasRelocationAddend=*/true) {}

  void sortRelocs(std::vector<ELFRelocationEntry> &Relocs) override {
    // ELF32 serialization otherwise silently truncates to uint32_t. Check
    // after the generic writer has folded any local section-symbol offset.
    for (const ELFRelocationEntry &Rel : Relocs)
      if (!isInt<32>(static_cast<int64_t>(Rel.Addend)))
        report_fatal_error("MCS251 ELF: RELA addend does not fit signed 32 bits");
    MCELFObjectTargetWriter::sortRelocs(Relocs);
  }

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &,
                        bool IsPCRel) const override {
    if (IsPCRel) {
      if (Fixup.getKind() == FK_Data_1)
        return ELF::R_MCS251_PC8;
      report_fatal_error("MCS251 ELF: unsupported PC-relative fixup");
    }
    switch (Fixup.getKind()) {
    case FK_Data_2:
    case MCS251::fixup_mcs251_16:
      return ELF::R_MCS251_16;
    case MCS251::fixup_mcs251_24:
      return ELF::R_MCS251_24;
    case MCS251::fixup_mcs251_lo8:
      return ELF::R_MCS251_LO8;
    case MCS251::fixup_mcs251_mid8:
      return ELF::R_MCS251_MID8;
    case MCS251::fixup_mcs251_hi8:
      return ELF::R_MCS251_HI8;
    default:
      report_fatal_error("MCS251 ELF: unsupported relocation fixup");
    }
  }
};
} // namespace

std::unique_ptr<MCObjectTargetWriter> llvm::createMCS251ELFObjectWriter() {
  return std::make_unique<MCS251ELFObjectWriter>();
}
