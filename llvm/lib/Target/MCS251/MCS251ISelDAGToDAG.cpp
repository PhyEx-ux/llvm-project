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
  if (N->isMachineOpcode() || N->getOpcode() == ISD::TargetFrameIndex) {
    N->setNodeId(-1);
    return;
  }

  // Memory references carry TargetFrameIndex operands; only ordinary FI
  // values materialise. Keeping the two node kinds distinct permits a stack
  // object to be both dereferenced and passed as a pointer in the same DAG.
  if (N->getOpcode() == ISD::FrameIndex) {
    SDLoc DL(N);
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i16);
    SDValue Lo(CurDAG->getMachineNode(MCS251::FIADDR, DL, MVT::i16, TFI), 0);
    SDValue Hi(CurDAG->getMachineNode(MCS251::MOV16ri, DL, MVT::i16,
                                    CurDAG->getTargetConstant(0, DL, MVT::i16)), 0);
    SDNode *DR = CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL, MVT::i32,
        {CurDAG->getTargetConstant(MCS251::GPR32RegClassID, DL, MVT::i32),
         Lo, CurDAG->getTargetConstant(MCS251::sub_lo16, DL, MVT::i32),
         Hi, CurDAG->getTargetConstant(MCS251::sub_hi16, DL, MVT::i32)});
    ReplaceNode(N, DR);
    return;
  }

  SelectCode(N);
}
