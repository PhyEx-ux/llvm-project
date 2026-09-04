//===-- MCS251RELObjectWriter.h - ASxxxx REL writer --------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251RELOBJECTWRITER_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251RELOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"

namespace llvm {
class raw_pwrite_stream;

std::unique_ptr<MCObjectWriter>
createMCS251RELObjectWriter(raw_pwrite_stream &OS);

} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251RELOBJECTWRITER_H
