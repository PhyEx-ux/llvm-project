//===-- MCS251TargetMachine.h - MCS-251 target machine ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251TARGETMACHINE_H
#define LLVM_LIB_TARGET_MCS251_MCS251TARGETMACHINE_H

#include "MCS251Subtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/MCS251TargetParser.h"
#include <optional>

namespace llvm {
class MCS251TargetMachine final : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  MCS251Subtarget Subtarget;
  const bool ELFObjectOutput;
  bool ObjectFileOutput = false;
  // P1-2: the numeric storage-model contract no longer lives in the generic
  // TargetOptions; it arrives through the target-feature string
  // (+mcs251-memory-contract=v1-v2-v32-v8-v1) or, for direct llc-style
  // users, the two command-line options below, and is resolved once here.
  std::optional<MCS251::MemoryContract> MemoryContract;

public:
  MCS251TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                      StringRef FS, const TargetOptions &Options,
                      std::optional<Reloc::Model> RM,
                      std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                      bool JIT);
  ~MCS251TargetMachine() override;

  const MCS251Subtarget *
  getSubtargetImpl(const Function &F) const override {
    return &Subtarget;
  }

  bool usesELFObjects() const { return ELFObjectOutput; }
  bool emitsObjectFile() const { return ObjectFileOutput; }

  const std::optional<MCS251::MemoryContract> &getMemoryContract() const {
    return MemoryContract;
  }

  void registerPassBuilderCallbacks(PassBuilder &PB) override;
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  Expected<std::unique_ptr<MCStreamer>>
  createMCStreamer(raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                   CodeGenFileType FileType, MCContext &Ctx) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};
} // namespace llvm

#endif
