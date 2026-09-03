//===-- MCS251ISelDAGToDAG.cpp - MCS-251 DAG instruction selector -------===//

#include "MCS251.h"
#include "MCS251TargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/Debug.h"

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
  SelectCode(N);
}
