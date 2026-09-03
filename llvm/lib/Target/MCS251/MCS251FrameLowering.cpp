//===-- MCS251FrameLowering.cpp - MCS-251 frame lowering -----------------===//

#include "MCS251FrameLowering.h"

using namespace llvm;

MCS251FrameLowering::MCS251FrameLowering()
    : TargetFrameLowering(StackGrowsDown, Align(1), 0, Align(1)) {}

void MCS251FrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {}

void MCS251FrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {}
