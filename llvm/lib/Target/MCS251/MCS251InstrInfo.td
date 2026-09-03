//===-- MCS251InstrInfo.td - MCS-251 instructions ---------*- tablegen -*-===//

def MCS251eret : SDNode<"MCS251ISD::ERET", SDTNone, [SDNPHasChain]>;

class MCS251Inst<dag Outs, dag Ins, string Asm, list<dag> Pat>
    : Instruction {
  let Namespace = "MCS251";
  let OutOperandList = Outs;
  let InOperandList = Ins;
  let AsmString = Asm;
  let Pattern = Pat;
  let Size = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let hasNoSchedulingInfo = 1;
}

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def ERET : MCS251Inst<(outs), (ins), "eret", [(MCS251eret)]>;
