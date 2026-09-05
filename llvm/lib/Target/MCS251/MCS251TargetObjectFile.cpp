//===-- MCS251TargetObjectFile.cpp - MCS-251 object sections --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
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
}

MCSection *MCS251TargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (const auto *GV = dyn_cast<GlobalVariable>(GO))
    if (!GV->isConstant())
      return DSEGSection;
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}
