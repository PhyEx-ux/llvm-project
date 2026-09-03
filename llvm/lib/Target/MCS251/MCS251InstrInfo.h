//===-- MCS251InstrInfo.h - MCS-251 instruction information ----*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251INSTRINFO_H
#define LLVM_LIB_TARGET_MCS251_MCS251INSTRINFO_H

#include "MCS251RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "MCS251GenInstrInfo.inc"

namespace llvm {
class MCS251Subtarget;

class MCS251InstrInfo final : public MCS251GenInstrInfo {
  const MCS251RegisterInfo RI;

public:
  explicit MCS251InstrInfo(const MCS251Subtarget &STI);
  const MCS251RegisterInfo &getRegisterInfo() const { return RI; }
};
} // namespace llvm

#endif
