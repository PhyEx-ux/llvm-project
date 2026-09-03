//===-- MCS251InstrInfo.td - MCS-251 instructions ---------*- tablegen -*-===//
//
// Phase 2: text assembly only, no binary encoding. AsmStrings never contain
// the '#' immediate prefix; it is emitted by MCS251InstPrinter::printOperand
// (SDCC style: `mov r5, #0x5a`).

// Return node: carries the chain, the return-value register operands
// (dpl / dptr) and an optional glue input, hence variadic with opt-in glue.
def MCS251eret : SDNode<"MCS251ISD::ERET", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Call node (Phase 7), same shape as upstream MSP430's MSP430call: variadic,
// with operand 0 the callee (a TargetGlobalAddress / TargetExternalSymbol
// after LowerCall wrapped it) and the remaining operands the physical
// argument registers (DAG.getRegister, so the selector emits them as implicit
// uses keeping dpl/dptr live into the call), followed by the register mask
// (the caller-preserved set from getCallPreservedMask) and the glue that
// ties the argument CopyToReg chain to the call and the call to the result
// CopyFromReg chain. SDNPOptInGlue (not SDNPInGlue) because a zero-argument
// call carries no input glue.
def SDT_MCS251Call : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def MCS251call : SDNode<"MCS251ISD::CALL", SDT_MCS251Call,
                        [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                         SDNPVariadic]>;

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

// Branch target: the operand is a MachineBasicBlock which MCS251MCInstLower
// turns into an MCSymbolRefExpr and MCS251InstPrinter prints as a label
// (same shape as upstream MSP430's jmptarget).
def brtarget : Operand<OtherVT>;

// Call target: a TargetGlobalAddress / TargetExternalSymbol that
// MCS251MCInstLower turns into an MCSymbolRefExpr. The symbol is resolved to
// a 24-bit address by the sdas251/linker (the 9A-prefix ecall encoding is
// 4 bytes, addr24), so the compiler never truncates or splits it.
def calltarget : Operand<iPTR>;

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
// All of these write the PSW flags (manual Table 5-10: add/sub/anl/orl/xrl/
// cmp/inc/dec all write CY/OV/AC/N/Z; mov and the jumps/eret do not). Since
// Phase 6 the flags are modelled as the virtual PSW register, so every ALU
// form declares Defs = [PSW]; this keeps any scheduler or folder from
// inserting a flag-writing instruction between a cmp and its jcc. mov stays
// flag-neutral.

let Constraints = "$lhs = $dst", isCommutable = 1, Defs = [PSW] in {
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
let Constraints = "$lhs = $dst", Defs = [PSW] in {
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
let Constraints = "$lhs = $dst", Defs = [PSW] in {
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
//  Compare instructions (flags only, no result register)
//===----------------------------------------------------------------------===//
//
// cmp computes lhs - rhs and only writes the PSW flags, hence the empty outs
// and Defs = [PSW]. Encodings (Intel 8XC251Sx manual A-4 + sdas251 TSV
// gold, source-mode, the two sources agree; note: BC/BD/BE, NOT 9C/9E):
//
//   cmp r,r     -> cmp r5,r3        (BC md ms)
//   cmp r,#imm8 -> cmp r5,#0x5a     (BE m,0000 imm)
//   cmp wr,wr   -> cmp wr6,wr4      (BD jd/2 js/2)
//   cmp wr,#16  -> cmp wr6,#0x1234  (BE j/2,0100 hi lo)
//
// No Patterns: there is no value-producing compare in this backend (SETCC
// is unsupported in Phase 6), so compares are only ever emitted manually by
// LowerBR_CC, glued to the BRCC pseudo that consumes the flags.
let Defs = [PSW] in {
  def CMP8rr  : MCS251Inst<(outs), (ins GPR8:$lhs, GPR8:$rhs),
                           "cmp $lhs, $rhs", []>;
  def CMP8ri  : MCS251Inst<(outs), (ins GPR8:$lhs, mcs251_imm8:$imm),
                           "cmp $lhs, $imm", []>;
  def CMP16rr : MCS251Inst<(outs), (ins GPR16:$lhs, GPR16:$rhs),
                           "cmp $lhs, $rhs", []>;
  def CMP16ri : MCS251Inst<(outs), (ins GPR16:$lhs, mcs251_imm16:$imm),
                           "cmp $lhs, $imm", []>;
}

//===----------------------------------------------------------------------===//
//  Control-flow instructions
//===----------------------------------------------------------------------===//
//
// Encodings (manual A-4 + sdas251 TSV): every conditional jump is a 2-byte
// rel8 form (+/-128 bytes); sjmp rel8 is opcode 80; ejmp addr24 is opcode
// 8A and reaches anywhere.
//
// Because a conditional jump is limited to rel8, a conditional branch is
// always expanded into the three-part long-branch sequence
//
//     jCCinv  skip      (rel8: the ejmp is 4 bytes away, always in range)
//     ejmp    target    (24-bit reach)
//   skip:
//
// via the BRCC/BRCC8S pseudos below (expanded by FinalizeISel, which runs
// after SelectionDAGISel when the full machine CFG already exists). This is
// the conservative-but-always-correct scheme for a backend with no branch
// relaxation infrastructure (SDCC emits the same shape).

// Conditional jumps. Semantics (cmp a,b computes a-b; QEMU-run-verified,
// manual truth tables agree):
//   je  Z=1 | jne Z=0 | jc CY=1 | jnc CY=0
//   jg  Z=0 and CY=0        (ugt)   | jle  Z=1 or CY=1   (ule)
//   jsl N<>OV               (slt)   | jsge N=OV          (sge)
//   jsg Z=0 and N=OV        (sgt)   | jsle Z=1 or N<>OV  (sle)
let isBranch = 1, isTerminator = 1, Uses = [PSW] in {
  def JE   : MCS251Inst<(outs), (ins brtarget:$dst), "je $dst",   []>;
  def JNE  : MCS251Inst<(outs), (ins brtarget:$dst), "jne $dst",  []>;
  def JC   : MCS251Inst<(outs), (ins brtarget:$dst), "jc $dst",   []>;
  def JNC  : MCS251Inst<(outs), (ins brtarget:$dst), "jnc $dst",  []>;
  def JG   : MCS251Inst<(outs), (ins brtarget:$dst), "jg $dst",   []>;
  def JLE  : MCS251Inst<(outs), (ins brtarget:$dst), "jle $dst",  []>;
  def JSL  : MCS251Inst<(outs), (ins brtarget:$dst), "jsl $dst",  []>;
  def JSGE : MCS251Inst<(outs), (ins brtarget:$dst), "jsge $dst", []>;
  def JSG  : MCS251Inst<(outs), (ins brtarget:$dst), "jsg $dst",  []>;
  def JSLE : MCS251Inst<(outs), (ins brtarget:$dst), "jsle $dst", []>;
}

// Unconditional jumps. The compiler uses ejmp uniformly for LowerBR (SDCC
// does the same); sjmp exists for the assembler's sake and future peephole
// use, it is never emitted by the compiler in this phase.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
  def SJMP : MCS251Inst<(outs), (ins brtarget:$dst), "sjmp $dst", []>;
  def EJMP : MCS251Inst<(outs), (ins brtarget:$dst), "ejmp $dst",
                        [(br bb:$dst)]>;
}

// BRCC pseudo (built by LowerBR_CC, expanded by FinalizeISel): the cmp has
// already been emitted by LowerBR_CC and glued to this pseudo. $cc is a raw
// ISD::CondCode.
let isBranch = 1, isTerminator = 1, Uses = [PSW], usesCustomInserter = 1 in
def BRCC : MCS251Inst<(outs), (ins i8imm:$cc, brtarget:$dst), "", []>;

// BRCC8S pseudo: signed i8 compare. The N-flag behaviour of 8-bit width
// operations has not been measured, so the comparison is conservatively
// widened to 16 bits: both operands are placed in a fresh wr as 0x00:(x^0x80)
// (offset-binary: x <s y  <=>  flip(x) <u flip(y) with
// flip(t) = t ^ 0x80 = (t + 0x80) mod 256), which gives the exact
// sign-extend-both-operands-and-compare-with-CMP16 semantics using only
// already-verified encodings (mov r,#imm / mov r,r / xrl r,#imm / cmp
// wr,wr). The whole sequence (extension + cmp + branch) is built by the
// custom inserter because the byte-into-word lane assembly is only
// expressible at MIR level. Uses = [PSW] mirrors BRCC for symmetry; it has
// no runtime effect because the inserter expands the pseudo before register
// allocation.
let isBranch = 1, isTerminator = 1, Uses = [PSW], usesCustomInserter = 1 in
def BRCC8S : MCS251Inst<(outs),
                        (ins GPR8:$lhs, GPR8:$rhs, i8imm:$cc, brtarget:$dst),
                        "", []>;

//===----------------------------------------------------------------------===//
//  Call instruction
//===----------------------------------------------------------------------===//
//
// Direct call = ecall (opcode 9A + addr24, 4 bytes). The 24-bit target
// address is filled in by the sdas251/linker from the symbol, so ecall is
// never truncated or split into an lcall (measurement-verified; symbol
// addresses above 0xFFFF resolve correctly).
//
// Caller-saved modelling: the full clobber set (r0-r9/r12-r15, acc/b,
// dpl/dph/dpx, psw) is NOT spelled out in a Defs list -- LowerCall attaches
// a register-mask operand to the MCS251ISD::CALL node (AVR-style), which the
// selector carries onto the MachineInstr. PSW is additionally listed in Defs
// (as MSP430 lists SR) so the flag clobber is already visible on the bare
// instruction before/without the mask operand. DPL/DPH/DPTR deliberately
// stay out of any preserved set: call results are consumed through
// CopyFromReg, not preserved across the call.
let isCall = 1, hasSideEffects = 1, Defs = [PSW] in
def ECALL : MCS251Inst<(outs), (ins calltarget:$addr), "ecall $addr", []>;

// LowerCall wraps every callee as one of the two target symbol forms below
// (indirect calls are rejected there), so these two Pats are the only entry
// points for selecting MCS251ISD::CALL (same shape as MSP430's CALLi Pats).
def : Pat<(MCS251call (iPTR tglobaladdr:$dst)), (ECALL tglobaladdr:$dst)>;
def : Pat<(MCS251call (iPTR texternalsym:$dst)), (ECALL texternalsym:$dst)>;

//===----------------------------------------------------------------------===//
//  Return instruction
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def ERET : MCS251Inst<(outs), (ins), "eret", [(MCS251eret)]>;
