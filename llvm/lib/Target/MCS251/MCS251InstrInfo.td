//===-- MCS251InstrInfo.td - MCS-251 instructions ---------*- tablegen -*-===//
//
// Phase 2: text assembly only, no binary encoding. AsmStrings never contain
// the '#' immediate prefix; it is emitted by MCS251InstPrinter::printOperand
// (SDCC style: `mov r5, #0x5a`).

// Return node: carries the chain, the return-value register operands
// (dpl / dptr) and an optional glue input, hence variadic with opt-in glue.
def MCS251eret : SDNode<"MCS251ISD::ERET", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Independent interrupt return (ISR campaign T04). Unlike ERET this node is
// built chain-only (no glue, no value operands: an ISR returns nothing), and
// selecting it emits RETI instead of ERET -- RETI additionally restores the
// status register saved in the hardware interrupt frame and pops the
// interrupt controller in-service state. LowerReturn (T05) creates it only
// for CallingConv::MCS251_INTR.
def MCS251reti : SDNode<"MCS251ISD::RETI", SDTNone, [SDNPHasChain]>;

// Call node (Phase 7), same shape as upstream MSP430's MSP430call: variadic,
// with operand 0 the callee (a TargetGlobalAddress / TargetExternalSymbol
// after LowerCall wrapped it) and the remaining operands the physical
// argument registers (DAG.getRegister, so the selector emits them as implicit
// uses keeping dpl/dptr live into the call), followed by the register mask
// (the caller-preserved set from getCallPreservedMask) and the glue that
// ties the argument CopyToReg chain to the call and the call to the result
// CopyFromReg chain. SDNPOptInGlue (not SDNPInGlue) because a zero-argument
// call carries no input glue.
def SDT_MCS251Call : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
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
def mcs251_imm32 : Operand<i32>;

// Constant displacement of an @wr+dis16 address. NOT an immediate operand of
// its own: it is part of the address syntax (`mov r10, @wr6+0x1234`), so it
// prints without the SDCC `#` immediate prefix.
def mcs251_dis16 : Operand<i16> { let PrintMethod = "printDis16"; }

// Direct address (dir8). Also NOT an immediate: `mov r10, 0x30` addresses the
// 8-bit direct space. Address-space semantics (measurement-verified, see the
// MOV8di/MOV8id comment below): direct 0x00-0x7f is page-zero edata and
// direct 0x80-0xff is the SFR space.
def mcs251_dir8  : Operand<i8>  { let PrintMethod = "printDir8"; }

// Bit address (bit8) of a bit-addressed operand. An 8-bit byte, but a
// DIFFERENT address space from dir8: the bit-address map is
//   0x00-0x7f  internal RAM bit space (backing byte 0x20 + (B >> 3),
//              bit index B & 7; bit address 0 is the first bit of 0x20),
//   0x80-0xff  bit-addressable SFR space (backing byte B & 0xf8,
//              bit index B & 7).
// The byte printed is the bit address itself (sdas251 `setb 0x00` gold),
// never the backing byte.
def mcs251_bitaddr : Operand<i8> { let PrintMethod = "printBitAddr"; }

// Branch target: the operand is a MachineBasicBlock which MCS251MCInstLower
// turns into an MCSymbolRefExpr and MCS251InstPrinter prints as a label
// (same shape as upstream MSP430's jmptarget).
def brtarget : Operand<OtherVT>;

// Call target: a TargetGlobalAddress / TargetExternalSymbol that
// MCS251MCInstLower turns into an MCSymbolRefExpr. The symbol is resolved to
// a 24-bit address by the sdas251/linker (the 9A-prefix ecall encoding is
// 4 bytes, addr24), so the compiler never truncates or splits it.
def calltarget : Operand<i32>;

//===----------------------------------------------------------------------===//
//  Move instructions
//===----------------------------------------------------------------------===//

// Register-to-register moves.
def MOV8rr  : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$src),
                         "mov $dst, $src", []>;
def MOV16rr : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$src),
                         "mov $dst, $src", []>;
def MOV32rr : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$src),
                         "mov $dst, $src", []>;

// DR immediate forms. MOV writes the low 16 bits and clears the high 16;
// MOVH writes only the high 16 bits. MOV32ri is a pseudo expanded into these
// two instructions by the custom inserter.
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
  def MOVDRri : MCS251Inst<(outs GPR32:$dst), (ins mcs251_imm16:$imm),
                           "mov $dst, $imm", []>;
def MOVHDRi : MCS251Inst<(outs GPR32:$dst),
                          (ins GPR32:$old, mcs251_imm16:$imm),
                          "movh $dst, $imm", []> {
  let Constraints = "$old = $dst";
}
let usesCustomInserter = 1 in
  def MOV32ri : MCS251Inst<(outs GPR32:$dst), (ins mcs251_imm32:$imm), "",
                           [(set GPR32:$dst, imm:$imm)]>;

// 32-bit right shifts.  There is no native dword shift (QEMU
// mcs251_native_shift_execute rejects the dword mode, matching the manual),
// so these pseudos expand in EmitInstrWithCustomInserter to a CY-chained
// RRC-A byte-lane sequence, unrolled $cnt times.  Reaching the MC emitter
// unexpanded is a compiler bug (rejectPseudo).
let usesCustomInserter = 1 in {
  def SRL32ri : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$src, i32imm:$cnt),
                           "", []>;
  def SRA32ri : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$src, i32imm:$cnt),
                           "", []>;
}

// Variable shifts expand to a guarded unit-shift loop before RA. Kind is
// 0=left, 1=logical right, 2=arithmetic right; only the low count byte is
// needed for defined IR inputs (count < width <= 32).
let usesCustomInserter = 1, isPseudo = 1 in {
  def VSHIFT8 : MCS251Inst<(outs GPR8:$dst),
      (ins GPR8:$src, GPR8:$count, i8imm:$kind), "", []>;
  def VSHIFT16 : MCS251Inst<(outs GPR16:$dst),
      (ins GPR16:$src, GPR8:$count, i8imm:$kind), "", []>;
  def VSHIFT32 : MCS251Inst<(outs GPR32:$dst),
      (ins GPR32:$src, GPR8:$count, i8imm:$kind), "", []>;
}

// Atomic one-bit DR right shift. Expand after RA into the measured five
// instructions so spills cannot break the carry/A dependencies. The full
// tied DR operand exposes every lane to RA; A and flags are scratch, and
// the final flags are NOT the N/Z of the 32-bit result.
let isPseudo = 1, Constraints = "$src = $dst", Defs = [A, PSW], Size = 9 in {
  def SRL32one : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$src), "", []>;
  def SRA32one : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$src), "", []>;
}

// Atomic symbolic address materialisation. Both words are emitted together;
// byte-of-24 relocations perform full symbol+addend arithmetic before slicing.
let Size = 8, isReMaterializable = 1 in
  def MOVADDR32 : MCS251Inst<(outs GPR32:$dst), (ins calltarget:$addr), "", []>;

// General pointers use DR and the signed dis16 indexed form. Reuse the
// printer (not the restricted SPBASE class) of resolved stack addresses.
def mcs251_addr : Operand<i32> {
  let MIOperandInfo = (ops GPR32, i16imm);
  let PrintMethod = "printStackAddr";
}
let mayLoad = 1 in
  def MOV8rmP : MCS251Inst<(outs GPR8:$dst), (ins mcs251_addr:$addr),
                         "mov $dst, $addr", []>;
let mayStore = 1 in
  def MOV8mrP : MCS251Inst<(outs), (ins mcs251_addr:$addr, GPR8:$src),
                         "mov $addr, $src", []>;

// DR-lane instructions operate on the complete 32-bit register.
let Constraints = "$lhs = $dst", Defs = [PSW] in {
  def ADD32rr : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$lhs, GPR32:$rhs),
                           "add $dst, $rhs",
                           [(set GPR32:$dst, (add GPR32:$lhs, GPR32:$rhs))]>;
  def SUB32rr : MCS251Inst<(outs GPR32:$dst), (ins GPR32:$lhs, GPR32:$rhs),
                           "sub $dst, $rhs",
                           [(set GPR32:$dst, (sub GPR32:$lhs, GPR32:$rhs))]>;
}

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

// DPXL (SFR 0x84) is the MOVX @DPTR region register (X2-1). Same shape as
// MOV8dpl/MOV8dph: a pinned direct SFR write ("mov 0x84, rN"; sdas251
// V05.50.4 gold 7A 21 84) whose only difference from the generic MOV8id
// direct store is the Defs=[DPXL] pin -- the scheduler must treat every
// DPXL re-point as a dependence of the MOVX ops that follow it (their
// Uses=[DPXL] below), so two independent AS3 chains in one block can never
// interleave a bank write across another chain's movx.  hasSideEffects is
// explicit: the write hits architectural state no SSA value tracks, so
// MachineCSE must not merge two re-points even for the same bank -- a merged
// pair stretches one region write over two movx's, so the interrupt window
// between them becomes visible to any DPXL change that is not restored
// (handwritten ISR bodies, user SFR writes; the frames the backend generates
// for ISRs DO restore DPXL, see ISR_PUSH/POP_DPX above and X2-4). The
// conservative pin is pinned by xdata-dead-value.ll::twice.
let Defs = [DPXL], hasSideEffects = 1 in
def MOV8dpxl : MCS251Inst<(outs), (ins GPR8:$src), "mov 0x84, $src", []>;

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

// ACC/B fixed SFR byte moves (A=0xe0, B=0xf0). These are deliberately
// separate from R11/R10, whose byte-slot aliases are not allocatable.
let Defs = [A] in {
def MOV8a : MCS251Inst<(outs), (ins GPR8:$src), "mov a, $src", []>;
def MOVAI : MCS251Inst<(outs), (ins mcs251_imm8:$imm), "mov a, $imm", []>;
}
let Uses = [A], Defs = [PSW], Constraints = "$src = $dst" in
  def OR8a : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$src),
                        "orl $dst, a", []>;
let Defs = [B] in
def MOV8b : MCS251Inst<(outs), (ins GPR8:$src), "mov b, $src", []>;
let Uses = [A] in
def MOV8ra : MCS251Inst<(outs GPR8:$dst), (ins), "mov $dst, a", []>;
let Uses = [B] in
def MOV8rb : MCS251Inst<(outs GPR8:$dst), (ins), "mov $dst, b", []>;

// CY/A byte rotates for the 32-bit right-shift expansion (Phase 14 shifts).
// The classic A-rotate encodings are single byte and need no A5 source-mode
// escape (low nibble < 6; sdas251 V05.50.4 gold: rrc a = 13, rlc a = 33,
// clr c = C3).  rrc/rlc read and write CY; clr c writes CY.
let Defs = [PSW] in
def CLRC : MCS251Inst<(outs), (ins), "clr c", []>;
let Defs = [PSW, A], Uses = [PSW, A] in {
def RLCA : MCS251Inst<(outs), (ins), "rlc a", []>;
def RRCA : MCS251Inst<(outs), (ins), "rrc a", []>;
}

//===----------------------------------------------------------------------===//
//  Bit-addressed instructions (8051 classic semantics)
//===----------------------------------------------------------------------===//
//
// The bit-address operand is a constant 8-bit bit address (see
// mcs251_bitaddr). Encodings are 8051 Classic (Area II, low nibble 2 so
// putOpcode adds no A5 source-mode escape) and were re-verified byte-for-byte
// against sdas251 V05.50.4 source mode (gold):
//
//   setb bit  -> D2 <bit>      clr bit -> C2 <bit>   cpl bit -> B2 <bit>
//   mov c,bit -> A2 <bit>      mov bit,c -> 92 <bit>
//   jb  bit,rel -> 20 <bit> rel8   jnb bit,rel -> 30 <bit> rel8
//   jbc bit,rel -> 10 <bit> rel8
//   setb c -> D3   clr c -> C3   cpl c -> B3
//
// Memory effect model: every one of these reaches memory (the backing byte of
// the bit), so none may be folded away; a bit-address op can alias an ordinary
// RAM byte (the 0x20-0x2f page-zero bit space overlaps ordinary edata), so the
// static per-instruction flags are deliberately conservative:
//
//   * setb/clr/cpl, mov bit,c and jbc WRITE the backing byte -> mayStore.
//     setb/clr/cpl and jbc also READ it (they are read-modify-write), and
//     mov bit,c reads it too (the bit-addressed operand is loaded into the
//     addressed bit) -> mayLoad for all four, including mov bit,c.
//   * mov c,bit and jb/jnb only READ the backing byte -> mayLoad.
//   * hasSideEffects is left 0: the memory flags already prevent deletion, and
//     leaving it clear keeps the (rare) real instruction visible to DCE when
//     its result is unused the same way MOV8di is.
//
// CY/PSW modelling: the explicit C forms carry the flag dependency. setb c /
// clr c only WRITE CY (Defs=[PSW]); cpl c READS and writes it (Uses/Defs
// [PSW]); mov c,bit writes CY (Defs=[PSW]) while mov bit,c READS CY
// (Uses=[PSW]). The bit-address forms are otherwise flag-neutral AS WRITTEN,
// but a bit address of 0xd7 names PSW.CY -- the lowering routes that constant
// to the C form so the virtual PSW stays accurate; it never emits a bare
// bit-address op with 0xd7 and then ignores the flag.
let mayLoad = 1, mayStore = 1, Size = 2 in {
  def SETBBIT : MCS251Inst<(outs), (ins mcs251_bitaddr:$bit),
                           "setb $bit", []>;
  def CLRBIT  : MCS251Inst<(outs), (ins mcs251_bitaddr:$bit),
                           "clr $bit", []>;
  def CPLBIT  : MCS251Inst<(outs), (ins mcs251_bitaddr:$bit),
                           "cpl $bit", []>;
  def MOVBITC : MCS251Inst<(outs), (ins mcs251_bitaddr:$bit),
                           "mov $bit, c", []> {
    // Writes the addressed bit FROM the current carry, so it reads CY (PSW).
    let Uses = [PSW];
  }
}
// Bit branches. OPERAND 0 IS THE BRANCH TARGET (a brtarget/MBB), operand 1 is
// the bit address. This is the target-wide invariant the post-layout
// MCS251BranchRelaxation pass and MCS251MCInstLower rely on
// (MI.getOperand(0).isMBB() / .getMBB()); putting the bit address first would
// silently break branch relaxation. The AsmString still prints the source
// shape `jb bit, rel`.
//
// JBC is a read-modify-write branch (clears the bit when set), so it carries
// mayStore as well as mayLoad. None of the three touches PSW.
let isBranch = 1, isTerminator = 1, mayLoad = 1, Size = 3 in {
  def JB : MCS251Inst<(outs),
                      (ins brtarget:$dst, mcs251_bitaddr:$bit),
                      "jb $bit, $dst", []>;
  def JNB : MCS251Inst<(outs),
                       (ins brtarget:$dst, mcs251_bitaddr:$bit),
                       "jnb $bit, $dst", []>;
}
let isBranch = 1, isTerminator = 1, mayLoad = 1, mayStore = 1, Size = 3 in
def JBC : MCS251Inst<(outs),
                     (ins brtarget:$dst, mcs251_bitaddr:$bit),
                     "jbc $bit, $dst", []>;
let mayLoad = 1, Defs = [PSW], Size = 2 in
def MOVCBIT : MCS251Inst<(outs), (ins mcs251_bitaddr:$bit),
                         "mov c, $bit", []>;

// CY forms. clr c is the pre-existing CLRC; setb c / cpl c write CY. Both are
// single-byte classic encodes (opcode+1 of the bit form). cpl c is a
// read-modify-write of CY, so it Uses as well as Defs PSW.
let Defs = [PSW], Size = 1 in {
  def SETBC : MCS251Inst<(outs), (ins), "setb c", []>;
  def CPLC  : MCS251Inst<(outs), (ins), "cpl c", []> {
    let Uses = [PSW];
  }
}

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
// kind (constant K <= 0xff -> direct; everything else -> DR addressing).
// The WR forms below remain legacy MC encodings; Phase 11 IR memory objects
// use MOV8rmP/mrP and ordered big-endian bytes.
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

//===----------------------------------------------------------------------===//
//  XDATA access (AS3, the `__xdata` space) -- MOVX @DPTR channel (X2-1)
//===----------------------------------------------------------------------===//
//
// The internal extended XRAM is reached with the classic MOVX @DPTR pair
// (STC32G manual L70765-70786: "在汇编语言中，内部扩展 RAM 通过 MOVX 指令访
// 问").  Byte-for-byte sdas251 V05.50.4 oracle (source mode):
//
//   movx a, @dptr   -> E0      (single byte, low nibble 0: no A5 escape)
//   movx @dptr, a   -> F0
//
// The @Ri 8-bit forms (E2/E3/F2/F3) need the MXAX/P2 page maintenance and are
// deliberately NOT implemented in this phase; per the STC32G manual
// L71066-71069 MXAX only affects the @Ri forms, so it cannot extend the @DPTR
// form either (Intel 251 manual 3.3.2.2: the region reached by MOVX is the
// DPXL SFR, reset value 01h).
//
// X2-1 full 24-bit ruling (supersedes the phase-1 16-bit window): the frozen
// AS3 contract (DESIGN.md B.2, 32/8 `__xdata`) requires the complete 24-bit
// effective address. Every AS3 access byte therefore re-points DPXL from
// address bits [23:16] with MOV8dpxl immediately before the MOVX (self-
// healing: no DPXL value is ever assumed to survive any other access, ISR,
// call or user SFR write -- the retention protocol is documented in
// validation/mcs251-models/proposals/XDATA-CODE-DESIGN-SUPPLEMENT.md).
// Never-assumes is one half of X2-4 only: the frames the backend itself
// generates for async entries PRESERVE DPXL (ISR_PUSH/POP_DPX read/write
// it), so an interrupt cannot leak its re-point into the interrupted
// context.
// Uses=[DPXL] on both MOVX ops plus Defs=[DPXL] on MOV8dpxl declare the
// region switch as a physical-register dependence -- but only at the MIR
// level (pre/post-RA machine schedulers, CSE, RA liveness). The
// SelectionDAG schedulers do not see MCInstrDesc implicit operands at all,
// and at -O1+ the DAG combiner parallelises disjoint non-volatile memory
// ops, so the lowering welds each MOVX byte sequence (lane writes + MOVX)
// with Glue into one scheduling unit (see buildMOVXByteLoad/Store in
// MCS251ISelLowering.cpp); that glue chain is also what makes InstrEmitter
// emit the lane writes' implicit defs live (not dead) in the first place.
//
// DPTR (dpl/dph) is the fixed reserved address register and A the fixed
// transfer register: MOVXALD writes A, MOVXAST reads it, both address through
// DPL/DPH -- hence the explicit Uses/Defs pins that order these against the
// MOV8dpl/MOV8dph address-lane writes and the MOV8a/MOV8ra value moves.  No
// Patterns: LowerLoad/LowerStore build the byte sequences as machine nodes
// (same approach as the MOV8rm family above).
//
// MOVXALD's i8 result is deliberately unused by the lowering (the byte
// travels through A into MOV8ra); the lowering pins the node into the
// selected stream with a Glue edge into MOV8ra -- the same device the
// bit-read sequence uses for its A-valued tail -- so no dead-load stripping
// can remove the access from the chain.
let mayLoad = 1, Defs = [A], Uses = [DPL, DPH, DPXL], Size = 1 in
  def MOVXALD : MCS251Inst<(outs), (ins), "movx a, @dptr", []>;
let mayStore = 1, Uses = [A, DPL, DPH, DPXL], Size = 1 in
  def MOVXAST : MCS251Inst<(outs), (ins), "movx @dptr, a", []>;

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
//  Multiply/divide
//===----------------------------------------------------------------------===//

// QEMU frozen 6b9edfd0 probe: MUL WR12,WR8 reads the two unsigned words,
// writes the full DR12 product and flags, and preserves WR8, A/B and DPTR.
// Pin the destination away from DR8 (whose R10/R11 lanes alias B/A).
let Uses = [WR12, WR8], Defs = [DR12, PSW], Size = 2 in
  def MULW : MCS251Inst<(outs), (ins), "mul wr12, wr8", []>;
let Uses = [A, B], Defs = [A, B, PSW] in
  def MULAB : MCS251Inst<(outs), (ins), "mul ab", []>;

// Expand before RA: all fixed-register copies and clobbers are visible to
// both FastRA and Greedy. No hidden scratch registers or static helper slots.
let usesCustomInserter = 1, isPseudo = 1 in {
  def MUL8 : MCS251Inst<(outs GPR8:$dst), (ins GPR8:$lhs, GPR8:$rhs), "",
                       [(set GPR8:$dst, (mul GPR8:$lhs, GPR8:$rhs))]>;
  def MUL16 : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$lhs, GPR16:$rhs), "",
                        [(set GPR16:$dst, (mul GPR16:$lhs, GPR16:$rhs))]>;
  def UMUL16WIDE : MCS251Inst<(outs GPR32:$dst),
                              (ins GPR16:$lhs, GPR16:$rhs), "", []>;
}

//===----------------------------------------------------------------------===//
//  Native 1-bit shifts (Phase 14)
//===----------------------------------------------------------------------===//
//
// sll/srl/sra exist for byte (Rn) and word (WRn) registers only; there is no
// dword form (QEMU mcs251_native_shift_execute rejects the dword mode,
// matching the Intel manual).  Each shifts by exactly one bit: CY takes the
// shifted-out bit, N/Z track the result (QEMU helper.c measurement).
// Encodings (sdas251 V05.50.4 gold, source mode, opcode bare):
//
//   sll r3   -> 3E 30      sll wr2 -> 3E 14     (specifier = code<<4|mode,
//   srl r7   -> 1E 70      srl wr6 -> 1E 34      byte mode 0, word mode 4)
//   sra r1   -> 0E 10      sra wr30 -> 0E F4
//
// No Patterns: these are only ever emitted by LowerShift via MachineNode
// unrolling (one node per shifted bit), the same shape as the i32 SHL
// ADD32rr unroll.  Variable shift counts remain a loud error there.
let Constraints = "$src = $dst", Defs = [PSW] in {
  def SLL8  : MCS251Inst<(outs GPR8:$dst),  (ins GPR8:$src),  "sll $dst", []>;
  def SRL8  : MCS251Inst<(outs GPR8:$dst),  (ins GPR8:$src),  "srl $dst", []>;
  def SRA8  : MCS251Inst<(outs GPR8:$dst),  (ins GPR8:$src),  "sra $dst", []>;
  def SLL16 : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$src), "sll $dst", []>;
  def SRL16 : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$src), "srl $dst", []>;
  def SRA16 : MCS251Inst<(outs GPR16:$dst), (ins GPR16:$src), "sra $dst", []>;
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
  // CMP32 flags verified by Inputs/phase11-qemu.py, including signed overflow.
  def CMP32rr : MCS251Inst<(outs), (ins GPR32:$lhs, GPR32:$rhs),
                           "cmp $lhs, $rhs", []>;
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
// after SelectionDAGISel when the full machine CFG already exists).  This
// keeps every conditional branch compact as long as the skip block stays
// layout-adjacent (SDCC emits the same shape).  It is an optimization, not a
// correctness guarantee: the post-layout MCS251BranchRelaxation pass
// (addPreEmitPass, after MachineBlockPlacement) rewrites any rel8 branch the
// final layout pushed out of range into an ejmp-based trampoline form,
// parsing the terminator shapes directly (the generic analyzeBranch hooks
// are deliberately not implemented).  The assembler still rejects a
// never-relaxed overflow as a backend bug.

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

// Flag-consuming selects expand into a long-branch diamond with a PHI.
let Uses = [PSW], usesCustomInserter = 1 in {
  def SELECT8 : MCS251Inst<(outs GPR8:$dst),
      (ins GPR8:$true, GPR8:$false, i8imm:$cc), "", []>;
  def SELECT16 : MCS251Inst<(outs GPR16:$dst),
      (ins GPR16:$true, GPR16:$false, i8imm:$cc), "", []>;
  def SELECT32 : MCS251Inst<(outs GPR32:$dst),
      (ins GPR32:$true, GPR32:$false, i8imm:$cc), "", []>;
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

// Although this ABI has no stack arguments, call sequences protect static
// parameter slots from interleaving with another call's setup. The scheduler
// recognizes these opcodes through TargetInstrInfo's call-frame descriptors.
def MCS251callseq_start : SDNode<"ISD::CALLSEQ_START",
    SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>,
    [SDNPHasChain, SDNPOutGlue]>;
def MCS251callseq_end : SDNode<"ISD::CALLSEQ_END",
    SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>,
    [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
let isPseudo = 1, hasSideEffects = 1 in {
  def ADJCALLSTACKDOWN : MCS251Inst<(outs), (ins i32imm:$amt, i32imm:$callee),
      "", [(MCS251callseq_start timm:$amt, timm:$callee)]>;
  def ADJCALLSTACKUP : MCS251Inst<(outs), (ins i32imm:$amt, i32imm:$callee),
      "", [(MCS251callseq_end timm:$amt, timm:$callee)]>;
}
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
let isCall = 1, hasSideEffects = 1, Defs = [PSW], Size = 2 in
  def ECALLr : MCS251Inst<(outs), (ins GPR32:$addr), "ecall @$addr", []>;
def : Pat<(MCS251call GPR32:$addr), (ECALLr GPR32:$addr)>;

// Direct calls retain full addr24 symbol relocations; registers use ECALLr.
def : Pat<(MCS251call (i32 tglobaladdr:$dst)), (ECALL tglobaladdr:$dst)>;
def : Pat<(MCS251call (i32 texternalsym:$dst)), (ECALL texternalsym:$dst)>;

//===----------------------------------------------------------------------===//
//  Return instruction
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def ERET : MCS251Inst<(outs), (ins), "eret", [(MCS251eret)]>;

//===----------------------------------------------------------------------===//
//  ISR fixed save/restore and interrupt return (ISR campaign T04)
//===----------------------------------------------------------------------===//
//
// Fixed, operand-less opcodes for the per-interrupt-level 37-byte save and
// inverse restore (A6 freeze): PSW + DR0/4/8/12/16/20/24/28 + DR56/DPX. The
// save set is frozen -- nothing is omitted, nothing is elided on demand (no
// shrink-wrapping, no callee-save re-save, no merged-save pass), and no
// temporary register is involved. SPX itself is never pushed or popped (A6
// ban) and no DR32..DR52 opcode exists.

// Stack model (see the Stack frame instructions section below): the stack
// grows UP and SPX points at the top-most used byte, so push pre-increments
// SPX and then stores, pop loads and then decrements SPX. Hence:
//
//   * every push: READS the register it saves -- the asynchronous
//     interrupted-context value, a pure dataflow input: a push never defines
//     the register it saves and the input must not be marked undef -- plus
//     reads SPX, writes SPX (Defs [DR60]) and writes the stack (mayStore);
//   * every pop: reads the stack (mayLoad), reads/writes SPX (Uses/Defs
//     [DR60]) and defines the restored register.
//
// Alias side effects are modelled explicitly on the fixed registers the
// saved/restored slots pass through -- "reserved, non-allocatable" does not
// mean "effect-free":
//
//   * DR8 lanes r10/r11 are the B/A SFRs: push dr8 reads DR8 plus the A/B
//     aliases, pop dr8 defines DR8 and A/B;
//   * DPTR (dpl:dph) aliases the low word of DR56/DPX: push dpx reads DR56
//     plus the DPTR overlay, pop dpx defines DR56 and DPL/DPH/DPTR;
//   * DPXL (SFR 0x84) is the MOVX region byte of DPX: push dpx reads it,
//     pop dpx defines it. The ISR frame therefore SAVES AND RESTORES DPXL
//     (X2-4 interrupt-retention protocol: an async preemption must not
//     leak its re-point into the interrupted context; the guarantee must
//     not be dropped -- design supplement §3);
//   * pop psw defines the virtual PSW flags register (the pushed one).
//
// Each push/pop encodes as two bytes (opcode byte + direct address or
// specifier), hence Size = 2; RETI below stays Size = 1.
//
// Ordering of the whole sequence (PSW first, PSW + RETI last) is T05's
// emission contract (FrameSetup/FrameDestroy, strict inverse order); these
// definitions only pin the per-instruction register effects.

// Fixed pushes. ISR_PUSH_DR16 coexists with PUSHFP above on purpose: PUSHFP
// is the ordinary-function frame-anchor pair member (documented anchor
// semantics), ISR_PUSH_* are the ISR fixed-frame opcodes.
let hasSideEffects = 1, mayStore = 1, Size = 2, Defs = [DR60] in {
  def ISR_PUSH_PSW  : MCS251Inst<(outs), (ins), "push psw", []> {
    let Uses = [PSW, DR60];
  }
  def ISR_PUSH_DR0  : MCS251Inst<(outs), (ins), "push dr0", []> {
    let Uses = [DR0, DR60];
  }
  def ISR_PUSH_DR4  : MCS251Inst<(outs), (ins), "push dr4", []> {
    let Uses = [DR4, DR60];
  }
  def ISR_PUSH_DR8  : MCS251Inst<(outs), (ins), "push dr8", []> {
    let Uses = [DR8, A, B, DR60];
  }
  def ISR_PUSH_DR12 : MCS251Inst<(outs), (ins), "push dr12", []> {
    let Uses = [DR12, DR60];
  }
  def ISR_PUSH_DR16 : MCS251Inst<(outs), (ins), "push dr16", []> {
    let Uses = [DR16, DR60];
  }
  def ISR_PUSH_DR20 : MCS251Inst<(outs), (ins), "push dr20", []> {
    let Uses = [DR20, DR60];
  }
  def ISR_PUSH_DR24 : MCS251Inst<(outs), (ins), "push dr24", []> {
    let Uses = [DR24, DR60];
  }
  def ISR_PUSH_DR28 : MCS251Inst<(outs), (ins), "push dr28", []> {
    let Uses = [DR28, DR60];
  }
  def ISR_PUSH_DPX  : MCS251Inst<(outs), (ins), "push dpx", []> {
    let Uses = [DR56, DPL, DPH, DPTR, DPXL, DR60];
  }
}

// Fixed pops: read the stack, rewind SPX, define the restored register
// (with the same alias sets as the matching push).
let hasSideEffects = 1, mayLoad = 1, Size = 2, Uses = [DR60] in {
  def ISR_POP_PSW  : MCS251Inst<(outs), (ins), "pop psw", []> {
    let Defs = [PSW, DR60];
  }
  def ISR_POP_DR0  : MCS251Inst<(outs), (ins), "pop dr0", []> {
    let Defs = [DR0, DR60];
  }
  def ISR_POP_DR4  : MCS251Inst<(outs), (ins), "pop dr4", []> {
    let Defs = [DR4, DR60];
  }
  def ISR_POP_DR8  : MCS251Inst<(outs), (ins), "pop dr8", []> {
    let Defs = [DR8, A, B, DR60];
  }
  def ISR_POP_DR12 : MCS251Inst<(outs), (ins), "pop dr12", []> {
    let Defs = [DR12, DR60];
  }
  def ISR_POP_DR16 : MCS251Inst<(outs), (ins), "pop dr16", []> {
    let Defs = [DR16, DR60];
  }
  def ISR_POP_DR20 : MCS251Inst<(outs), (ins), "pop dr20", []> {
    let Defs = [DR20, DR60];
  }
  def ISR_POP_DR24 : MCS251Inst<(outs), (ins), "pop dr24", []> {
    let Defs = [DR24, DR60];
  }
  def ISR_POP_DR28 : MCS251Inst<(outs), (ins), "pop dr28", []> {
    let Defs = [DR28, DR60];
  }
  def ISR_POP_DPX  : MCS251Inst<(outs), (ins), "pop dpx", []> {
    let Defs = [DR56, DPL, DPH, DPTR, DPXL, DR60];
  }
}

// Interrupt return (RETI, opcode 0x32; ISR campaign T04, A6). Pops the 4-byte
// hardware interrupt frame (profile 1, A6: PSW1, PC[23:16], PC[7:0],
// PC[15:8]) through SPX -- hence mayLoad, Uses [DR60] and Defs [DR60] for the
// rewind. Defs [PSW] models the restored status register on the single
// virtual flags register this backend keeps. Two further side effects are
// deliberately UNMODELLED registers and are captured only by hasSideEffects
// and this comment (do not remove):
//   * the status register restored by the hardware is PSW1 (SFR 0xD1, which
//     carries N/Z) -- folded here into the virtual PSW def;
//   * RETI pops the interrupt controller in-service state for this priority
//     level, re-enabling same/lower priority interrupts.
// RETI is NOT ERET: ordinary functions keep ERET; only ISR exits may emit
// RETI (T05 LowerReturn/epilogue, T06 final-machine boundary enforce this).
// The AsmStrings are distinct ("reti" / "eret") so neither prints as the
// other, and the encodings differ (0x32 vs 0xAA).
let isReturn = 1, isTerminator = 1, isBarrier = 1, hasSideEffects = 1,
    mayLoad = 1, Uses = [DR60], Defs = [DR60, PSW] in
def RETI : MCS251Inst<(outs), (ins), "reti", [(MCS251reti)]>;

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
// functions set hasFP() and anchor the frame top in reserved dr16:
//
//   prologue:  push dr16 / inc spx,#N / mov dr16,dr60
//   epilogue:  mov dr60,dr16 / dec spx,#N / pop dr16
//
// The supported SDCC port does not allocate R16-R31. LLVM reserves DR16
// too, and functions using it as an anchor save/restore it, so nested
// LLVM/SDCC calls preserve this private anchor convention. push/pop dr16
// move 4 bytes (the QEMU DR-slot width), and the anchor is taken AFTER the
// push, so the O - N displacement formula above stays valid relative to the
// anchor as well (both the concept addresses and the anchor shift by the same
// 4 bytes).
//
// Encodings (sdas251 V05.50.4 + QEMU stc32g144k246 run-verified, see
// /tmp/mcs251-p9 forms.asm): inc/dec dr60,#1/2/4 (the ONLY legal immediates
// -- a frame adjustment is emitted as a sequence of these steps, the SDCC
// prologue template), mov dr16,dr60 / mov dr60,dr16 (DR<->DR moves, the
// same encoding family as the measured mov dr0,dr60), push/pop dr16 (same
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
let hasSideEffects = 1, Uses = [DR60], Defs = [DR16] in
def SETFP : MCS251Inst<(outs), (ins), "mov dr16, dr60", []>;
let hasSideEffects = 1, Uses = [DR16], Defs = [DR60] in
def RESTORESP : MCS251Inst<(outs), (ins), "mov dr60, dr16", []>;

// Anchor save/restore. push dr16 advances SPX by 4 (a full DR slot).
let hasSideEffects = 1, Uses = [DR16], Defs = [DR60] in
def PUSHFP : MCS251Inst<(outs), (ins), "push dr16", []>;
let hasSideEffects = 1, Uses = [DR60], Defs = [DR16, DR60] in
def POPFP : MCS251Inst<(outs), (ins), "pop dr16", []>;

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
// dr16 when the function has a frame anchor); displacement is resolved by
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

// Unresolved frame references have a distinct, verifier-correct descriptor.
// PEI changes these into the two-operand-address S forms above.
def mcs251_frame : Operand<i32> {
  let MIOperandInfo = (ops SPBASE, i32imm, i16imm);
}
let mayLoad = 1, isPseudo = 1 in {
  def MOV8rmF : MCS251Inst<(outs GPR8:$dst), (ins mcs251_frame:$addr), "", []>;
  def MOV32rmF : MCS251Inst<(outs GPR32:$dst), (ins mcs251_frame:$addr), "", []>;
  def MOV16rmF : MCS251Inst<(outs GPR16:$dst), (ins mcs251_frame:$addr), "", []>;
}
let mayStore = 1, isPseudo = 1 in {
  def MOV8mrF : MCS251Inst<(outs), (ins mcs251_frame:$addr, GPR8:$src), "", []>;
  def MOV32mrF : MCS251Inst<(outs), (ins mcs251_frame:$addr, GPR32:$src), "", []>;
  def MOV16mrF : MCS251Inst<(outs), (ins mcs251_frame:$addr, GPR16:$src), "", []>;
}

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
// sequence runs. Reading the DR16 anchor into an escaping address is not
// implemented yet, so that mixed case is rejected explicitly.
let usesCustomInserter = 1, hasSideEffects = 0 in
def FIADDR : MCS251Inst<(outs GPR16:$dst), (ins i16imm:$src), "", []>;

def mcs251_fioffset : Operand<i16> {
  let MIOperandInfo = (ops i16imm, i16imm);
}

// add wr, <frame displacement>: operand 1 is the unresolved frame index
// (FI, offset) pair; eliminateFrameIndex replaces it with the folded
// displacement and retargets the opcode to ADD16ri. add wr,#imm16 wraps
// mod 2^16, which is the correct region-00 semantics for a negative
// displacement (Phase 8 ruling).
let Constraints = "$lhs = $dst", Defs = [PSW] in
def ADD16fi : MCS251Inst<(outs GPR16:$dst),
                         (ins GPR16:$lhs, mcs251_fioffset:$addr),
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
