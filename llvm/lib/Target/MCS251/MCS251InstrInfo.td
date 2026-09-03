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

// Moves out of the fixed SFR argument locations (dpl = first i8 argument,
// dpl:dph = first i16 argument; ABI measured on SDCC/QEMU hardware). The
// mirror images of MOV8dpl/MOV8dph: the source is pinned by the instruction
// itself and only declared via Uses. Emitted by copyPhysReg when expanding
// copies from DPL/DPH after LowerFormalArguments' CopyFromReg. The Rn,dir8
// encoding (e.g. `mov r5, dpl`, prefix 7E + direct address 0x82 for dpl)
// was verified against sdas251 V05.50.4.
let Uses = [DPL] in
def MOV8rdpl : MCS251Inst<(outs GPR8:$dst), (ins), "mov $dst, dpl", []>;
let Uses = [DPH] in
def MOV8rdph : MCS251Inst<(outs GPR8:$dst), (ins), "mov $dst, dph", []>;

//===----------------------------------------------------------------------===//
//  ALU instructions (two-address: dst <- dst op src)
//===----------------------------------------------------------------------===//
//
// The five basic binary operations at both byte (r, GPR8) and word (wr,
// GPR16) granularity. Every form is dst <- dst op src, expressed with a
// tied "$lhs = $dst" operand; TwoAddressInstructionPass inserts the required
// COPYs, so no custom lowering is needed.
//
// Facts locked in by measurement (sdas251 V05.50.4 + QEMU stc32g144k246 +
// Intel 8XC251Sx manual), do not change without new measurements:
//
//   * The bitwise mnemonics are anl/orl/xrl (lowercase, NOT and/or/xor).
//   * The same mnemonics cover the 8-bit r,r / r,#imm and 16-bit wr,wr /
//     wr,#imm16 forms; the 16-bit immediate is big-endian in the encoding.
//     anl/orl/xrl wr,#imm16 and the rr forms were QEMU-run-verified, the
//     remaining imm forms are sdas251-assembler-verified.
//   * The 16-bit add/sub are single instructions operating on the whole wr
//     register (no byte-wise carry chain).
//
// The ALU flags register (psw) is clobbered by all of these but is not
// modelled yet: nothing in this phase consumes or reasons about flags, so
// (as with the moves above) no Defs are declared. This must be revisited
// before compares / conditional branches are added.

let Constraints = "$lhs = $dst", isCommutable = 1 in {
  def ADD8rr : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, GPR8:$rhs),
                          "add $dst, $rhs",
                          [(set GPR8:$dst, (add GPR8:$lhs, GPR8:$rhs))]>;
  def AND8rr : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, GPR8:$rhs),
                          "anl $dst, $rhs",
                          [(set GPR8:$dst, (and GPR8:$lhs, GPR8:$rhs))]>;
  def OR8rr  : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, GPR8:$rhs),
                          "orl $dst, $rhs",
                          [(set GPR8:$dst, (or GPR8:$lhs, GPR8:$rhs))]>;
  def XOR8rr : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, GPR8:$rhs),
                          "xrl $dst, $rhs",
                          [(set GPR8:$dst, (xor GPR8:$lhs, GPR8:$rhs))]>;

  def ADD16rr : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$lhs, GPR16:$rhs),
                           "add $dst, $rhs",
                           [(set GPR16:$dst, (add GPR16:$lhs, GPR16:$rhs))]>;
  def AND16rr : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$lhs, GPR16:$rhs),
                           "anl $dst, $rhs",
                           [(set GPR16:$dst, (and GPR16:$lhs, GPR16:$rhs))]>;
  def OR16rr  : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$lhs, GPR16:$rhs),
                           "orl $dst, $rhs",
                           [(set GPR16:$dst, (or GPR16:$lhs, GPR16:$rhs))]>;
  def XOR16rr : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$lhs, GPR16:$rhs),
                           "xrl $dst, $rhs",
                           [(set GPR16:$dst, (xor GPR16:$lhs, GPR16:$rhs))]>;
}

// sub is not commutable (dst <- dst - src, operand order is significant).
let Constraints = "$lhs = $dst" in {
  def SUB8rr : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, GPR8:$rhs),
                          "sub $dst, $rhs",
                          [(set GPR8:$dst, (sub GPR8:$lhs, GPR8:$rhs))]>;
  def SUB16rr : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$lhs, GPR16:$rhs),
                           "sub $dst, $rhs",
                           [(set GPR16:$dst, (sub GPR16:$lhs, GPR16:$rhs))]>;
}

// Immediate forms. The AsmString carries no '#'; the immediate PrintMethods
// (printImm8/printImm16) emit the SDCC-style `#0x..` prefix, so the same
// operand is used by MOV8ri/MOV16ri above. The imm patterns cover the full
// i8/i16 value range, so no extra predicates are needed.
let Constraints = "$lhs = $dst" in {
  def ADD8ri : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, mcs251_imm8:$imm),
                          "add $dst, $imm",
                          [(set GPR8:$dst, (add GPR8:$lhs, imm:$imm))]>;
  def SUB8ri : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, mcs251_imm8:$imm),
                          "sub $dst, $imm",
                          [(set GPR8:$dst, (sub GPR8:$lhs, imm:$imm))]>;
  def AND8ri : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, mcs251_imm8:$imm),
                          "anl $dst, $imm",
                          [(set GPR8:$dst, (and GPR8:$lhs, imm:$imm))]>;
  def OR8ri  : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, mcs251_imm8:$imm),
                          "orl $dst, $imm",
                          [(set GPR8:$dst, (or GPR8:$lhs, imm:$imm))]>;
  def XOR8ri : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, mcs251_imm8:$imm),
                          "xrl $dst, $imm",
                          [(set GPR8:$dst, (xor GPR8:$lhs, imm:$imm))]>;

  def ADD16ri : MCS251Inst<(outs GPR16:$dst),
                           (ins GPR16:$lhs, mcs251_imm16:$imm),
                           "add $dst, $imm",
                           [(set GPR16:$dst, (add GPR16:$lhs, imm:$imm))]>;
  def SUB16ri : MCS251Inst<(outs GPR16:$dst),
                           (ins GPR16:$lhs, mcs251_imm16:$imm),
                           "sub $dst, $imm",
                           [(set GPR16:$dst, (sub GPR16:$lhs, imm:$imm))]>;
  def AND16ri : MCS251Inst<(outs GPR16:$dst),
                           (ins GPR16:$lhs, mcs251_imm16:$imm),
                           "anl $dst, $imm",
                           [(set GPR16:$dst, (and GPR16:$lhs, imm:$imm))]>;
  def OR16ri  : MCS251Inst<(outs GPR16:$dst),
                           (ins GPR16:$lhs, mcs251_imm16:$imm),
                           "orl $dst, $imm",
                           [(set GPR16:$dst, (or GPR16:$lhs, imm:$imm))]>;
  def XOR16ri : MCS251Inst<(outs GPR16:$dst),
                           (ins GPR16:$lhs, mcs251_imm16:$imm),
                           "xrl $dst, $imm",
                           [(set GPR16:$dst, (xor GPR16:$lhs, imm:$imm))]>;
}

//===----------------------------------------------------------------------===//
//  Return instruction
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def ERET : MCS251Inst<(outs), (ins), "eret", [(MCS251eret)]>;
