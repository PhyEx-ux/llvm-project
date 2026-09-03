//===-- MCS251InstrInfo.cpp - MCS-251 instruction information ------------===//

#include "MCS251InstrInfo.h"
#include "MCS251Subtarget.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "MCS251GenInstrInfo.inc"

MCS251InstrInfo::MCS251InstrInfo(const MCS251Subtarget &STI)
    : MCS251GenInstrInfo(STI, RI), RI() {}
