//===-- MCS251Subtarget.cpp - MCS-251 subtarget information --------------===//

#include "MCS251Subtarget.h"

using namespace llvm;

#define DEBUG_TYPE "mcs251-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "MCS251GenSubtargetInfo.inc"

MCS251Subtarget &
MCS251Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  ParseSubtargetFeatures(CPU, CPU, FS);
  return *this;
}

MCS251Subtarget::MCS251Subtarget(const Triple &TT, const std::string &CPU,
                                 const std::string &FS,
                                 const TargetMachine &TM)
    : MCS251GenSubtargetInfo(TT, CPU, CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering() {}
