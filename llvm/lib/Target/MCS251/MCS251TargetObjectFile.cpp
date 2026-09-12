//===-- MCS251TargetObjectFile.cpp - MCS-251 object sections --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions: See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCS251TargetObjectFile.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"

using namespace llvm;

void MCS251TargetObjectFile::Initialize(MCContext &Ctx,
                                        const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  DSEGSection = Ctx.getELFSection(".mcs251.dseg", ELF::SHT_NOBITS,
                                  ELF::SHF_ALLOC | ELF::SHF_WRITE);
  XINITSection = Ctx.getELFSection(".mcs251.xinit", ELF::SHT_PROGBITS,
                                   ELF::SHF_ALLOC);
  // X3 placement classes. AS3 (__xdata) objects are XDATA NOBITS whose ROM
  // load image is the sparse `.mcs251.xdata_init` record table (the linker
  // places both; the record carries the full 24-bit destination).
  XSEGSection = Ctx.getELFSection(".mcs251.XSEG", ELF::SHT_NOBITS,
                                  ELF::SHF_ALLOC | ELF::SHF_WRITE);
  XDATAInitSection = Ctx.getELFSection(".mcs251.xdata_init", ELF::SHT_PROGBITS,
                                       ELF::SHF_ALLOC);
}

MCSection *MCS251TargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (const auto *GV = dyn_cast<GlobalVariable>(GO)) {
    // X3: the address space selects the storage class. AS3 objects live in
    // XDATA (NOBITS, writable); AS4 objects are CODE-space read-only images.
    // The AsmPrinter's custom emitters place the actual bytes (one
    // `.mcs251.XSEG.<sym>` NOBITS section per AS3 object plus its
    // `.mcs251.xdata_init` record; AS4 images in CSEG); these answers keep
    // generic consumers (declaration walks, constant pools) consistent with
    // that placement. `const __xdata` stays in XSEG: const is a write
    // discipline, the storage space is decided by the address space alone.
    if (GV->getAddressSpace() == 3)
      return XSEGSection;
    if (GV->getAddressSpace() == 4)
      return ReadOnlySection;
    if (!GV->isConstant())
      return DSEGSection;
  }
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}
