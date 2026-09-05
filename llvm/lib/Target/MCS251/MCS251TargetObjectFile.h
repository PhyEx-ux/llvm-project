//===-- MCS251TargetObjectFile.h - MCS-251 object sections -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_MCS251_MCS251TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

class MCS251TargetObjectFile final : public TargetLoweringObjectFileELF {
  MCSection *DSEGSection = nullptr;
  MCSection *XINITSection = nullptr;

public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  MCSection *getDSEGSection() const { return DSEGSection; }
  MCSection *getXINITSection() const { return XINITSection; }
};

} // end namespace llvm

#endif
