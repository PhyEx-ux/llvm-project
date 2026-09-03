//===-- MCS251MCAsmInfo.h - MCS-251 assembly properties -------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCASMINFO_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class MCS251MCAsmInfo final : public MCAsmInfoELF {
public:
  MCS251MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};
} // namespace llvm

#endif
