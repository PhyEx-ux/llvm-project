//===-- MCS251MCAsmInfo.cpp - MCS-251 assembly properties ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MC assembly dialect for the ASxxxx assembler shipped with SDCC (sdas251),
// as locked during the Phase 12 dialect study against the sdas251/sdld
// sources and QEMU runs (/tmp/mcs251-p12).  The dialect deltas configured
// here are the ones the generic AsmPrinter/MC machinery controls through
// MCAsmInfo:
//
//   - comments are ';' only (no '@'/'#' line comments);
//   - no .type/.size (SDCC-style modules have neither);
//   - no .file/.ident/.weak ELF identity directives (the ASxxxx
//     counterparts .module/.source are emitted by
//     MCS251AsmPrinter::emitStartOfAsmFile; plain MCAsmInfo already
//     defaults HasIdentDirective to false);
//   - no ELF section switching directives ever reach the output: deriving
//     from plain MCAsmInfo keeps printSwitchToSection a no-op (see the
//     header for the full story) and getStackSection at nullptr;
//   - function alignment directives are never emitted: MCS-251 instructions
//     are variable length with no alignment requirement, and sdas251 would
//     reject the GAS spellings .p2align/.align (ASxxxx only knows
//     .even/.odd/.bndry).  Declaring function alignment "unsupported" here
//     beats trying to map MCAsmStreamer::emitAlignmentDirective onto
//     .bndry, because the backend simply never needs alignment.
//   - 16-bit data is .word and zero fill is .ds (ASxxxx spellings;
//     sdas251 rejects .short/.zero); DSEG/XINIT global emission uses both.
//
//===----------------------------------------------------------------------===//

#include "MCS251MCAsmInfo.h"
#include "MCS251MCTargetDesc.h"

using namespace llvm;

MCS251MCAsmInfo::MCS251MCAsmInfo(const Triple &TT,
                                 const MCTargetOptions &Options)
    : MCAsmInfo(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 1;
  CommentString = ";";
  SeparatorString = "\n";
  HasDotTypeDotSizeDirective =
      MCS251::getObjectFormat() == MCS251::ObjectFormat::ELF;
  HasSingleParameterDotFile = false;
  SupportsDebugInformation = false;
  ExceptionsType = ExceptionHandling::None;
  UseIntegratedAssembler = false;

  // Local labels keep the ELF-ish ".L" prefix used throughout the backend's
  // tests and the Phase 12 specimen (.LBB0_1).  Plain MCAsmInfo would
  // default to MachO-style "L"; MCAsmInfoELF (which we no longer derive
  // from) was what used to set ".L".
  InternalSymbolPrefix = ".L";

  // Suppress .p2align/.align in function headers (see file comment).
  HasFunctionAlignment = false;

  // Match the big-endian data layout and sdas251's MSB-first .word.
  // ASxxxx has no .long/.quad: let MCAsmStreamer split wider constants
  // into supported directives, most-significant piece first. This also
  // applies to function prefix/prologue data, not just future globals.
  IsLittleEndian = false;
  Data16bitsDirective = "\t.word\t";
  Data32bitsDirective = nullptr;
  Data64bitsDirective = nullptr;
  ZeroDirective = "\t.ds\t";
}
