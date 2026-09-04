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

// Constant displacement of an @wr+dis16 address. NOT an immediate operand of
// its own: it is part of the address syntax (`mov r10, @wr6+0x1234`), so it
// prints without the SDCC `#` immediate prefix.
def mcs251_dis16 : Operand<i16> { let PrintMethod = "printDis16"; }

// Direct address (dir8). Also NOT an immediate: `mov r10, 0x30` addresses the
// 8-bit direct space. Address-space semantics (measurement-verified, see the
// MOV8di/MOV8id comment below): direct 0x00-0x7f is page-zero edata and
// direct 0x80-0xff is the SFR space.
def mcs251_dir8  : Operand<i8>  { let PrintMethod = "printDir8"; }

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
//  Memory instructions (load/store, Phase 8)
//===----------------------------------------------------------------------===//
//
// The MCS-251 data addressing forms this backend emits (encodings verified
// against sdas251 V05.50.4 / TSV gold, source-mode):
//
//   mov r3, @wr6        indirect byte read   (region-00 edata, 16-bit)
//   mov @wr6, r3        indirect byte write
//   mov r10, @wr6+0x1234  displaced read (dis16)
//   mov @wr6+0x1234, r10  displaced write
//   mov r10, 0x30       direct read  (dir8)
//   mov 0x30, r10       direct write
//
// ADDRESS-SPACE TRAP (measurement-verified, do not change): a direct address
// in 0x80-0xff addresses the SFR space, while the SAME numeric address
// reached through @wr indirect or @wr+dis16 addresses region-00 edata. The
// SFRs are therefore ONLY reachable via the direct forms MOV8di/MOV8id, and
// MCS251ISelLowering::parseAddress decides direct-vs-indirect per address
// kind (constant K <= 0xff -> direct; everything else -> @wr/+dis16). There
// is deliberately no hardware 16-bit load/store instruction: an i16 memory
// object is always decomposed into two byte accesses with big-endian lane
// mapping (see LD16/ST16 below).
//
// No Patterns: the selection is a semantic decision (address space + width
// decomposition), done by LowerLoad/LowerStore building these as machine
// nodes directly (same approach as CMP8rr: no value-producing generic node
// exists to match).

let mayLoad = 1 in {
  // mov r3, @wr6
  def MOV8rm  : MCS251Inst<(outs GPR8:$dst), (ins GPR16:$base),
                           "mov $dst, @$base", []>;
  // mov r10, @wr6+0x1234
  def MOV8rmD : MCS251Inst<(outs GPR8:$dst),
                           (ins GPR16:$base, mcs251_dis16:$disp),
                           "mov $dst, @$base+$disp", []>;
  // mov r10, 0x30 (direct; 0x80-0xff reaches the SFRs, see the trap above)
  def MOV8di  : MCS251Inst<(outs GPR8:$dst), (ins mcs251_dir8:$addr),
                           "mov $dst, $addr", []>;
}

let mayStore = 1 in {
  // mov @wr6, r3
  def MOV8mr  : MCS251Inst<(outs), (ins GPR16:$base, GPR8:$src),
                           "mov @$base, $src", []>;
  // mov @wr6+0x1234, r10
  def MOV8mrD : MCS251Inst<(outs),
                           (ins GPR16:$base, mcs251_dis16:$disp, GPR8:$src),
                           "mov @$base+$disp, $src", []>;
  // mov 0x30, r10 (direct; 0x80-0xff reaches the SFRs, see the trap above)
  def MOV8id  : MCS251Inst<(outs), (ins mcs251_dir8:$addr, GPR8:$src),
                           "mov $addr, $src", []>;
}

// Pseudo for an i16 load, built by LowerLoad and expanded by
// EmitInstrWithCustomInserter (REG_SEQUENCE is MIR-only, the two byte loads
// cannot be expressed as one DAG node). Endianness ruling (measurement-
// verified): 16-bit objects in edata memory are BIG-endian -- mem[base] is
// the high byte -- matching the WR register file where the MSB lives in the
// lower-numbered byte slot (sub_hi8). The expansion is therefore
//
//   %hi:gpr8  = MOV8rmD %base, disp      ; mem[base+disp]    -> hi lane
//   %lo:gpr8  = MOV8rmD %base, disp+1    ; mem[base+disp+1]  -> lo lane
//   %dst:gpr16 = REG_SEQUENCE %hi:sub_hi8, %lo:sub_lo8
//
// using dis16 displacements instead of incrementing the base register.
let mayLoad = 1, usesCustomInserter = 1 in
def LD16 : MCS251Inst<(outs GPR16:$dst),
                      (ins GPR16:$base, mcs251_dis16:$disp), "", []>;

// Pseudo for an i16 store; mirror of LD16 with the lanes extracted via
// sub-register COPYs (mem[base+disp] <- hi lane, mem[base+disp+1] <- lo
// lane). $val must be a GPR16 vreg; constants are materialised by
// LowerStore before the pseudo is built.
let mayStore = 1, usesCustomInserter = 1 in
def ST16 : MCS251Inst<(outs),
                      (ins GPR16:$base, mcs251_dis16:$disp, GPR16:$val),
                      "", []>;

// Pseudo for a truncating store (i16 value -> i8 memory): stores the low
// lane only.
let mayStore = 1, usesCustomInserter = 1 in
def ST16T : MCS251Inst<(outs),
                       (ins GPR16:$base, mcs251_dis16:$disp, GPR16:$val),
                       "", []>;

// Direct-addressing variant of ST16T (constant address <= 0xff, including
// the SFR space 0x80-0xff): same low-lane extraction, dir8 encoding. The
// re-classification trap: a direct address 0x80-0xff must NOT fall back to
// @wr for the compiler's convenience -- @wr at the same numeric address is
// region-00 edata, a different memory location (see the address-space trap
// at the top of this section).
let mayStore = 1, usesCustomInserter = 1 in
def ST16TD : MCS251Inst<(outs), (ins mcs251_dir8:$addr, GPR16:$val), "", []>;

// Pseudo widening a byte to a word with a zero high lane
// (zero/any-extending load of i8, and plain zext i8->i16):
//
//   %zero:gpr8 = MOV8ri 0
//   %dst:gpr16 = REG_SEQUENCE %zero:sub_hi8, %src:sub_lo8
//
// Same single-def-vreg structure as the BRCC8S expansion (MachineCSE's
// getUniqueVRegDef assumption; see the comment there).
let usesCustomInserter = 1 in
def ZEXT8 : MCS251Inst<(outs GPR16:$dst), (ins GPR8:$src), "", []>;

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

//===----------------------------------------------------------------------===//
// Stack frame instructions (Phase 9)
//===----------------------------------------------------------------------===//
//
// THE STACK GROWS UP on the MCS-251 (measurement-verified, do not change):
// push pre-increments SP (SPX+1 then store), ecall pushes the 3-byte return
// address with SPX+3, and eret pops [SPX-2..SPX] with SPX-3. SPX therefore
// points at the TOP (highest used address) of the stack, and a frame's
// objects start at SPX_entry+1 -- the byte at SPX_entry itself holds the
// return address. This drives the whole frame model:
//
//   * prologue:  inc spx,#N          -> SPX_after = SPX_entry + N
//   * an object at PEI offset O (LocalAreaOffset = 1, so O in [1,N]) lives
//     at address SPX_entry + O, i.e. at displacement  O - N  from SPX_after
//     (a NEGATIVE displacement, the SDCC `@spx-0x....` house style).
//   * epilogue:  dec spx,#N restores SPX_entry so eret can pop the return
//     address.
//
// Variable-sized objects (dynamic allocas) move SPX at the alloca site, so
// the static frame can no longer be addressed relative to SPX. Those
// functions set hasFP() and anchor the frame top in dr56 (dpx):
//
//   prologue:  push dr56 / inc spx,#N / mov dr56,dr60
//   epilogue:  mov dr60,dr56 / dec spx,#N / pop dr56
//
// dr56 is not preserved across calls by the SDCC ABI, so a var-sized callee
// saves/restores it itself. A nested call to another function with
// variable-sized objects must not destroy the caller's anchor. push/pop dr56
// move 4 bytes (the QEMU DR-slot width), and the anchor is taken AFTER the
// push, so the O - N displacement formula above stays valid relative to the
// anchor as well (both the concept addresses and the anchor shift by the same
// 4 bytes).
//
// Encodings (sdas251 V05.50.4 + QEMU stc32g144k246 run-verified, see
// /tmp/mcs251-p9 forms.asm): inc/dec dr60,#1/2/4 (the ONLY legal immediates
// -- a frame adjustment is emitted as a sequence of these steps, the SDCC
// prologue template), mov dr56,dr60 / mov dr60,dr56 (DR<->DR moves, the
// same encoding family as the measured mov dr0,dr60), push/pop dr56 (same
// DR push/pop family as the measured push dr12).

// SP adjustment steps. inc/dec are ALU operations and write PSW (manual
// Table 5-10); they also read the old SPX (RMW).
let hasSideEffects = 1, Uses = [DR60], Defs = [DR60, PSW] in {
  def INCSPX4 : MCS251Inst<(outs), (ins), "inc spx, #0x4", []>;
  def INCSPX2 : MCS251Inst<(outs), (ins), "inc spx, #0x2", []>;
  def INCSPX1 : MCS251Inst<(outs), (ins), "inc spx, #0x1", []>;
  def DECSPX4 : MCS251Inst<(outs), (ins), "dec spx, #0x4", []>;
  def DECSPX2 : MCS251Inst<(outs), (ins), "dec spx, #0x2", []>;
  def DECSPX1 : MCS251Inst<(outs), (ins), "dec spx, #0x1", []>;
}

// Frame-anchor moves (only emitted when hasFP(): variable-sized objects).
let hasSideEffects = 1, Uses = [DR60], Defs = [DR56] in
def MOV56 : MCS251Inst<(outs), (ins), "mov dr56, dr60", []>;
let hasSideEffects = 1, Uses = [DR56], Defs = [DR60] in
def MOV60 : MCS251Inst<(outs), (ins), "mov dr60, dr56", []>;

// Anchor save/restore. push dr56 advances SPX by 4 (a full DR slot).
let hasSideEffects = 1, Uses = [DR56], Defs = [DR60] in
def PUSH56 : MCS251Inst<(outs), (ins), "push dr56", []>;
let hasSideEffects = 1, Uses = [DR60], Defs = [DR56, DR60] in
def POP56 : MCS251Inst<(outs), (ins), "pop dr56", []>;

// Stack-slot address operand: @dr60-0x002c style. The displacement prints
// SDCC-style: zero prints nothing (`@dr60`), positive as +0x%04x, negative
// as -0x%04x (a two's-complement 0xfffc displacement would be parsed as a
// POSITIVE displacement by sdas251 -- the 24-bit @DRk displacement is
// unsigned in the assembler's eyes -- so the sign must be explicit, mir-
// roring SDCC's own output). MIOperandInfo describes the eliminated form
// (base register + displacement); while a frame index is unresolved the
// displacement slot carries (FI, offset) instead, one extra operand, and
// eliminateFrameIndex folds them.
def mcs251_stack : Operand<i16> {
  let MIOperandInfo = (ops SPBASE, i16imm);
  let PrintMethod = "printStackAddr";
}

// Frame-relative loads/stores. Base is an SPBASE register (dr60 normally,
// dr56 when the function has a frame anchor); displacement is resolved by
// eliminateFrameIndex to O - StackSize (negative, see the model above).
// There is deliberately ONE word-granular access pair: mov wr,@dr60+dis and
// mov @dr60+dis,wr are single instructions (TSV-verified) and are the
// required form for i16 spills -- do not decompose them into byte pairs.
let mayLoad = 1 in {
  def MOV8rmS  : MCS251Inst<(outs GPR8:$dst),
                            (ins mcs251_stack:$addr),
                            "mov $dst, $addr", []>;
  def MOV16rmS : MCS251Inst<(outs GPR16:$dst),
                            (ins mcs251_stack:$addr),
                            "mov $dst, $addr", []>;
}
let mayStore = 1 in {
  def MOV8mrS  : MCS251Inst<(outs),
                            (ins mcs251_stack:$addr, GPR8:$src),
                            "mov $addr, $src", []>;
  def MOV16mrS : MCS251Inst<(outs),
                            (ins mcs251_stack:$addr, GPR16:$src),
                            "mov $addr, $src", []>;
}

// i16 memory objects on the frame: same lane-decomposition pseudos as
// LD16/ST16/ST16T, but frame-relative. Expanded by the custom inserter
// (FinalizeISel) into two MOV8rmS/MOV8mrS whose displacements are FI+0 and
// FI+1; PEI later resolves each against the frame base. Explicit operand
// order: LD16S [dst, addr], ST16S/ST16TS [addr, val].
let mayLoad = 1, usesCustomInserter = 1 in
def LD16S : MCS251Inst<(outs GPR16:$dst),
                       (ins mcs251_stack:$addr), "", []>;
let mayStore = 1, usesCustomInserter = 1 in
def ST16S : MCS251Inst<(outs),
                       (ins mcs251_stack:$addr, GPR16:$val), "", []>;
let mayStore = 1, usesCustomInserter = 1 in
def ST16TS : MCS251Inst<(outs),
                        (ins mcs251_stack:$addr, GPR16:$val), "", []>;

// Frame-index pointer materialisation (`lea FI`). Selected by the
// ISelDAGToDAG Select hook (AVR-style select<FrameIndex>): operand $src
// carries the TargetFrameIndex until the custom inserter expands to
//
//   %lo = MOV8di 0x81        ; SFR-direct read of SP (low byte)
//   %hi = MOV8di 0x85        ; SFR-direct read of SPH (high byte, this
//                            ;  platform's SPH -- QEMU-arbitrated, NOT 0xbe)
//   %sp = REG_SEQUENCE %hi:sub_hi8, %lo:sub_lo8
//   %dst = ADD16fi %sp, <FI> ; displacement folded by PEI
//
// Reading SPX into a WR has no single instruction (mov wr,dr60 is illegal:
// width mismatch), so the two SFR-direct byte reads are the only path (both
// assembler- and run-verified). ADD16fi keeps the frame index alive until
// PEI (the displacement depends on StackSize, unknown at ISel time);
// eliminateFrameIndex rewrites it into a plain ADD16ri. In a function without
// an FP, SPX is invariant throughout the function: there are no
// ADJCALLSTACKs, and push only appears in a hasFP prologue. FIADDR can
// therefore carry no chain and be freely scheduled. It is only valid without
// a frame anchor: with variable-sized objects SPX has moved by the time the
// sequence runs, and there is no way to read dr56 into a WR -- rejected
// loudly there.
let usesCustomInserter = 1, hasSideEffects = 0 in
def FIADDR : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$src), "", []>;

// add wr, <frame displacement>: operand 1 is the unresolved frame index
// (FI, offset) pair; eliminateFrameIndex replaces it with the folded
// displacement and retargets the opcode to ADD16ri. add wr,#imm16 wraps
// mod 2^16, which is the correct region-00 semantics for a negative
// displacement (Phase 8 ruling).
let Constraints = "$lhs = $dst", Defs = [PSW] in
def ADD16fi : MCS251Inst<(outs GPR16:$dst),
                         (ins GPR16:$lhs, mcs251_stack:$addr),
                         "add $dst, $addr", []>;

// Dynamic alloca (DYNAMIC_STACKALLOC). The whole size-adjust-readback
// sequence is MIR-only (REG_SEQUENCE), so it rides a pseudo expanded by
// the custom inserter:
//
//   %lo = MOV8di 0x81 / %hi = MOV8di 0x85      ; read current SPX
//   %sp = REG_SEQUENCE %hi:sub_hi8, %lo:sub_lo8
//   %new = ADD16rr %sp, %size                  ; new stack top (wr math:
//                                               ;  `add dr60,wr` is an
//                                               ;  illegal width mix)
//   MOV8id 0x85, hi(%new) / MOV8id 0x81, lo(%new)  ; write SPX back via
//                                               ;  SFR-direct stores, hi first
//   %dst = ADD16ri %sp, #1                     ; object base = old top+1
//
// The returned pointer is the FIRST byte of the object (old SPX+1): on an
// up-growing stack whose SPX points at the top-most used byte, a new
// object occupies [oldSPX+1 .. oldSPX+size]. Alignment > 1 is rejected in
// the lowering (the stack is byte-aligned; no padding is ever emitted).
let usesCustomInserter = 1, hasSideEffects = 1, Uses = [DR60], Defs = [DR60] in
def DYNALLOCA : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$size), "", []>;
