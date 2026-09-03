//===-- MCS251MCTargetDesc.h - MCS-251 MC descriptions --------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCTARGETDESC_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"

#define GET_REGINFO_ENUM
#include "MCS251GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "MCS251GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "MCS251GenSubtargetInfo.inc"

#endif
