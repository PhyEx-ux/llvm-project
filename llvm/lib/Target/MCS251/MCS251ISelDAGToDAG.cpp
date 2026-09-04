//===-- MCS251ISelDAGToDAG.cpp - MCS-251 DAG instruction selector -------===//

#include "MCS251.h"
#include "MCS251TargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "mcs251-isel"
#define PASS_NAME "MCS251 DAG->DAG Pattern Instruction Selection"

namespace {
class MCS251DAGToDAGISel final : public SelectionDAGISel {
public:
  MCS251DAGToDAGISel(MCS251TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  void Select(SDNode *N) override;

#include "MCS251GenDAGISel.inc"
};

class MCS251DAGToDAGISelLegacy final : public SelectionDAGISelLegacy {
public:
  static char ID;
  MCS251DAGToDAGISelLegacy(MCS251TargetMachine &TM,
                            CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<MCS251DAGToDAGISel>(TM, OptLevel)) {}
};
} // namespace

char MCS251DAGToDAGISelLegacy::ID;

INITIALIZE_PASS(MCS251DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createMCS251ISelDag(MCS251TargetMachine &TM,
                                        CodeGenOptLevel OptLevel) {
  return new MCS251DAGToDAGISelLegacy(TM, OptLevel);
}

void MCS251DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  // A frame-index VALUE (an alloca address escaping into a call argument,
  // a return value, a phi) materialises as the FIADDR pseudo (AVR-style
  // select<FrameIndex>): the custom inserter builds the two SFR-direct SP
  // reads plus ADD16fi, and PEI folds the final displacement.
  //
  // DISPATCH BY USER: getTargetFrameIndex CSEs, so the TargetFrameIndex
  // node consumed by LowerLoad/LowerStore's frame-relative accesses (as a
  // machine-node operand) can be the very same node some other user wants
  // as a value. Morphing it would destroy the memory operand; leaving it
  // unmaterialised would strand the value user. So: machine users keep the
  // node untouched (it is already in the target domain), value users get
  // FIADDR, and a node needed BOTH ways is rejected loudly -- splitting the
  // uses would need a hook-independent copy of the address. (The
  // register-arithmetic path alloca[i] never comes through here:
  // parseAddress materialises that side in the DAG lowering, see
  // materializeFrameIndex.)
  unsigned Opc = N->getOpcode();
  if (Opc == ISD::FrameIndex || Opc == ISD::TargetFrameIndex) {
    bool HasMachineUse = false, HasValueUse = false;
    for (SDNode *U : N->users()) {
      if (U->isMachineOpcode())
        HasMachineUse = true;
      else
        HasValueUse = true;
    }
    if (HasMachineUse && HasValueUse)
      report_fatal_error("MCS251: a stack object used both as a memory base "
                         "and as an escaping pointer value in one function "
                         "is not supported (the frame index would need two "
                         "lowerings)");
    if (HasValueUse) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      SDValue TFI = Opc == ISD::FrameIndex
                        ? CurDAG->getTargetFrameIndex(FI, MVT::i16)
                        : SDValue(N, 0);
      CurDAG->SelectNodeTo(N, MCS251::FIADDR, MVT::i16, TFI);
      return;
    }
    // Memory-only use (or dead): nothing to select.
    N->setNodeId(-1);
    return;
  }

  SelectCode(N);
}
