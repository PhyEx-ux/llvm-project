//===-- MCS251TargetInfo.cpp - MCS-251 target registration ---------------===//

#include "MCS251TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheMCS251Target() {
  static Target TheMCS251Target;
  return TheMCS251Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMCS251TargetInfo() {
  RegisterTarget<Triple::mcs251> X(getTheMCS251Target(), "mcs251",
                                   "Intel MCS-251 [experimental]", "MCS251");
}
