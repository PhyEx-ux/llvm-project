//===-- MCS251MCAsmInfo.cpp - MCS-251 assembly properties ----------------===//

#include "MCS251MCAsmInfo.h"

using namespace llvm;

MCS251MCAsmInfo::MCS251MCAsmInfo(const Triple &TT,
                                 const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 1;
  CommentString = ";";
  SeparatorString = "\n";
  HasDotTypeDotSizeDirective = false;
  HasSingleParameterDotFile = false;
  SupportsDebugInformation = false;
  ExceptionsType = ExceptionHandling::None;
  UseIntegratedAssembler = false;
}
