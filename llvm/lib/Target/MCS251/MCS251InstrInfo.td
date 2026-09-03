//===-- MCS251InstrInfo.td - MCS-251 instructions ---------*- tablegen -*-===//
//
// Phase 2: text assembly only, no binary encoding. AsmStrings never contain
// the '#' immediate prefix; it is emitted by MCS251InstPrinter::printOperand
// (SDCC style: `mov r5, #0x5a`).

// Return node: carries the chain, the return-value register operands
// (dpl / dptr) and an optional glue input, hence variadic with opt-in glue.
def MCS251eret : SDNode<"MCS251ISD::ERET", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

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

//===----------------------------------------------------------------------===//
//  Operands
//===----------------------------------------------------------------------===//
//
// Custom PrintMethods render immediates SDCC-style (`#0x2a`, `#0x1234`) and
// mask the value to the operand width, so negative constants (which reach
// the printer sign-extended to 64 bits) come out as their two's-complement
// byte/word pattern.

def mcs251_imm8  : Operand<i8>  { let PrintMethod = "printImm8"; }
def mcs251_imm16 : Operand<i16> { let PrintMethod = "printImm16"; }

//===----------------------------------------------------------------------===//
//  Move instructions
//===----------------------------------------------------------------------===//

// Register-to-register moves.
def MOV8rr  : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$src),
                         "mov $dst, $src", []>;
def MOV16rr : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$src),
                         "mov $dst, $src", []>;

// Immediate materialisation.
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
  def MOV8ri  : MCS251Inst<(outs GPR8:$dst), (ins mcs251_imm8:$src),
                           "mov $dst, $src",
                           [(set GPR8:$dst, imm:$src)]>;
  def MOV16ri : MCS251Inst<(outs GPR16:$dst), (ins mcs251_imm16:$src),
                           "mov $dst, $src",
                           [(set GPR16:$dst, imm:$src)]>;
}

// Moves into the fixed SFR return-value locations. dpl/dph are not members
// of any register class, so the destination is pinned by the instruction
// itself and only declared via Defs (same shape as AVR's SPWRITE). These are
// emitted by copyPhysReg when expanding copies to DPL/DPH.
let Defs = [DPL] in
def MOV8dpl : MCS251Inst<(outs), (ins GPR8:$src), "mov dpl, $src", []>;
let Defs = [DPH] in
def MOV8dph : MCS251Inst<(outs), (ins GPR8:$src), "mov dph, $src", []>;

//===----------------------------------------------------------------------===//
//  Return instruction
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def ERET : MCS251Inst<(outs), (ins), "eret", [(MCS251eret)]>;
