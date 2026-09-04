//===-- MCS251MCTargetDesc.h - MCS-251 MC descriptions --------*- C++ -*-===//
#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCTARGETDESC_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251MCTARGETDESC_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCStreamer;
class MCTargetOptions;
class Target;
class raw_pwrite_stream;

MCCodeEmitter *createMCS251MCCodeEmitter(const MCInstrInfo &MCII,
                                         MCContext &Ctx);
MCAsmBackend *createMCS251MCAsmBackend(const Target &T,
                                       const MCSubtargetInfo &STI,
                                       const MCRegisterInfo &MRI,
                                       const MCTargetOptions &Options);
std::unique_ptr<MCObjectWriter> createMCS251ObjectWriter(
    raw_pwrite_stream &OS);
MCStreamer *createMCS251RELStreamer(
    const Triple &T, MCContext &Context, std::unique_ptr<MCAsmBackend> &&TAB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&Emitter);

} // namespace llvm

#define GET_REGINFO_ENUM
#include "MCS251GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "MCS251GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "MCS251GenSubtargetInfo.inc"

#endif
