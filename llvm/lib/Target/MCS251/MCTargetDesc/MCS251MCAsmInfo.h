//===-- MCS251MCAsmInfo.h - MCS-251 assembly properties -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MC assembly dialect information for the ASxxxx assembler shipped with SDCC
// (sdas251).  See MCS251MCAsmInfo.cpp for the dialect decisions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCASMINFO_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCASMINFO_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class Triple;

// Deliberately derives from the plain MCAsmInfo, *not* MCAsmInfoELF:
//
//  - MCAsmInfo::printSwitchToSection is a virtual no-op while
//    MCAsmInfoELF finalizes it with ELF syntax.  MCAsmStreamer funnels
//    every section switch it prints through this hook (".text" for the
//    first switch, ".section ..." afterwards), so the base no-op is what
//    keeps all ELF section directives out of the ASxxxx output.  The one
//    code area is opened explicitly by MCS251AsmPrinter::emitStartOfAsmFile
//    (".area CSEG (CODE)") and nothing else is ever printed.
//
//  - MCAsmInfo::getStackSection returns nullptr, which suppresses the
//    trailing ".section .note.GNU-stack" that AsmPrinter::doFinalization
//    would otherwise switch to; MCAsmInfoELF returns that section.
//
// The TargetMachine still uses a TargetLoweringObjectFileELF internally
// (the codegen machinery needs MCSection objects); only the *printed
// syntax* is ASxxxx.
class MCS251MCAsmInfo final : public MCAsmInfo {
public:
  MCS251MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};
} // namespace llvm

#endif
