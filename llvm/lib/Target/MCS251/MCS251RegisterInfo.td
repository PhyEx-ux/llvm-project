//===-- MCS251RegisterInfo.td - MCS-251 registers ---------*- tablegen -*-===//
//
// Register byte order ruling (measured on hardware, do not change):
//
//   On the MCS-251 the most significant byte of a multi-byte register lives
//   in the lower-numbered byte slot (evidence: SDCC mcs251 port ABI.md and
//   assembler behaviour of the wr0-wr30 / dr0-dr28 names):
//
//     WRk = Rk : R{k+1}            (Rk is the MSB, R{k+1} is the LSB)
//     DRk = Rk : R{k+1} : R{k+2} : R{k+3}   (Rk is the most significant byte)
//     DRk = WRk : WR{k+2}          (WRk is the MSW, WR{k+2} is the LSW)
//
//   LLVM's SubRegs list is positional with respect to SubRegIndices, and
//   sub_lo* always denotes the least significant lane. Therefore every
//   SubRegs list below is ordered least-significant-first (same shape as
//   upstream AVR's [R24, R25] pairs):
//
//     WRk.SubRegs = [R{k+1}, Rk]     matched with [sub_lo8,  sub_hi8]
//     DRk.SubRegs = [WR{k+2}, WRk]   matched with [sub_lo16, sub_hi16]
//
//   SubRegIndex<Size, Offset> offsets count from bit 0 (the LSB) of the
//   super-register and have nothing to do with byte slot numbers.

class MCS251Reg<string Name, bits<6> Encoding> : Register<Name> {
  let Namespace = "MCS251";
  let HWEncoding{5-0} = Encoding;
  let DwarfNumbers = [Encoding];
}

class MCS251RegWithSubRegs<string Name, bits<6> Encoding,
                           list<Register> SubRegs,
                           list<string> altNames = []>
    : RegisterWithSubRegs<Name, SubRegs> {
  let Namespace = "MCS251";
  let HWEncoding{5-0} = Encoding;
  let DwarfNumbers = [Encoding];
  let AltNames = altNames;
}

let Namespace = "MCS251" in {
  def sub_lo8 : SubRegIndex<8, 0>;
  def sub_hi8 : SubRegIndex<8, 8>;
  def sub_lo16 : SubRegIndex<16, 0>;
  def sub_hi16 : SubRegIndex<16, 16>;

  // Alternate-name set used for registers with a special-function alias
  // (dr56 == dpx, dr60 == spx).
  def RegNameAlt : RegAltNameIndex;
}

//===----------------------------------------------------------------------===//
//  8-bit byte registers r0-r63
//===----------------------------------------------------------------------===//
//
// The MCS-251 register file physically has 64 byte slots. Only r0-r9 and
// r12-r15 are allocatable (see GPR8 below): r10/r11 alias the B/ACC SFRs
// which SDCC never allocates, and r16-r63 are not allocated in this phase.
// All 64 slots are still defined so that the wr/dr super-registers below
// have a complete sub-register structure.
foreach I = 0-63 in
  def R#I : MCS251Reg<"r"#I, I>;

//===----------------------------------------------------------------------===//
//  16-bit word registers wr0-wr30 (even byte slots)
//===----------------------------------------------------------------------===//
//
// SubRegs are least-significant-first per the byte order ruling above:
// wr{k} = [r{k+1} (low byte), r{k} (high byte)].
let SubRegIndices = [sub_lo8, sub_hi8], CoveredBySubRegs = 1 in
foreach I = 0-15 in {
  defvar K = !mul(I, 2);
  def WR#K : MCS251RegWithSubRegs<"wr"#K, K,
                                  [!cast<Register>("R"#!add(K, 1)),
                                   !cast<Register>("R"#K)]>;
}

// wr56-wr62 exist only to give dr56/dr60 a complete sub-register structure
// (X86 HAX-style: artificial registers that no instruction ever names).
// wr32-wr54 are deliberately not defined: no dr register needs them.
let isArtificial = 1, SubRegIndices = [sub_lo8, sub_hi8],
    CoveredBySubRegs = 1 in {
  def WR56 : MCS251RegWithSubRegs<"wr56", 56, [R57, R56]>;
  def WR58 : MCS251RegWithSubRegs<"wr58", 58, [R59, R58]>;
  def WR60 : MCS251RegWithSubRegs<"wr60", 60, [R61, R60]>;
  def WR62 : MCS251RegWithSubRegs<"wr62", 62, [R63, R62]>;
}

//===----------------------------------------------------------------------===//
//  32-bit double-word registers dr0-dr28 (byte slots divisible by 4)
//===----------------------------------------------------------------------===//
//
// SubRegs are least-significant-first per the byte order ruling above:
// dr{k} = [wr{k+2} (low word), wr{k} (high word)].
// dr32-dr52 have no assembler encoding and are not defined.
let SubRegIndices = [sub_lo16, sub_hi16], CoveredBySubRegs = 1 in
foreach I = 0-7 in {
  defvar K = !mul(I, 4);
  def DR#K : MCS251RegWithSubRegs<"dr"#K, K,
                                  [!cast<Register>("WR"#!add(K, 2)),
                                   !cast<Register>("WR"#K)]>;
}

// dr56 (alias dpx) is the extended data pointer, dr60 (alias spx) is the
// extended stack pointer. Both are reserved in getReservedRegs() and never
// allocated, but they need full sub-register structure for lane masks.
let SubRegIndices = [sub_lo16, sub_hi16], CoveredBySubRegs = 1,
    RegAltNameIndices = [RegNameAlt] in {
  def DR56 : MCS251RegWithSubRegs<"dr56", 56, [WR58, WR56], ["dpx"]>;
  def DR60 : MCS251RegWithSubRegs<"dr60", 60, [WR62, WR60], ["spx"]>;
}

//===----------------------------------------------------------------------===//
//  Special-function registers dpl/dph and the 16-bit dptr pair
//===----------------------------------------------------------------------===//
//
// DPL (SFR 0x82) and DPH (SFR 0x83) are the ABI return-value locations:
// i8 results are returned in dpl, i16 results in the dpl:dph pair
// (dpl = low byte, dph = high byte; QEMU-measured SDCC behaviour).
//
// DPTR models the dpl:dph overlay so the calling-convention machinery has a
// single 16-bit location to assign i16 returns to. In the hardware (and in
// the SDCC assembler) dptr is a degenerate register: it may only appear as
// the destination of a 16-bit immediate load and is never a source or
// destination of a register-to-register move. We therefore never allocate it
// and only use it as a copyPhysReg destination that is immediately split
// into its dpl/dph lanes. SubRegs are least-significant-first per the byte
// order ruling at the top of this file: dptr = [dpl (low), dph (high)].
//
// Value-semantics note: dptr aliases the low 16 bits of dr56 (dpx). The
// alias is deliberately not modelled in the register file -- both registers
// are reserved, so no allocation conflict can arise.
//
// The HWEncoding values are placeholders: dpl/dph/dptr have no r/wr/dr-style
// encoding (the assembler reaches them through the SFR direct addresses
// 0x82/0x83), and this phase has no code emitter.
def DPL : MCS251Reg<"dpl", 61>;
def DPH : MCS251Reg<"dph", 62>;

// DPXL (SFR 0x84, Intel 251 manual 3.3.2.2) selects the 64-Kbyte region the
// MOVX @DPTR channel reaches (reset value 01h). X2-1 ruling: the backend
// never relies on the region surviving an access -- every AS3 access sequence
// re-points DPXL from the canonical 24-bit address bits [23:16] immediately
// before the MOVX (self-healing, see the XDATA lowering in
// MCS251ISelLowering.cpp and the DPXL retention protocol in
// validation/mcs251-models/proposals/XDATA-CODE-DESIGN-SUPPLEMENT.md). Like
// dpl/dph it is a member of no register class (reached only through the
// dedicated MOV8dpxl direct form) and is reserved in getReservedRegs().
// never allocated (class-less) and reserved in getReservedRegs().
// X2-4 retention protocol, ISR half: the ISR fixed frame covers DPXL --
// push dpx reads it, pop dpx restores it (ISR_PUSH/POP_DPX Uses/Defs).
// HWEncoding 60 is a placeholder (the encoding field is bits<6>, so the
// dpl/dph convention numbers max out at 63; 60 is unused), following the
// dpl/dph 61/62 convention above.
def DPXL : MCS251Reg<"dpxl", 60>;

// ACC (A, SFR 0xe0) and B (SFR 0xf0) are independent fixed ABI registers.
// Their byte-slot aliases R11 (ACC) and R10 (B) are intentionally not modeled
// as register-file aliases: R10/R11 remain excluded from GPR8, while A/B are
// reserved SFR names reached only by MOV8a/MOV8b and MOV8ra/MOV8rb. Their
// HWEncoding values 58/59 are placeholders -- the actual SFR addresses are
// 0xe0/0xf0 -- following the DPL/DPH 61/62 placeholder convention above.
def A : MCS251Reg<"a", 58>;
def B : MCS251Reg<"b", 59>;

let SubRegIndices = [sub_lo8, sub_hi8], CoveredBySubRegs = 1 in
  def DPTR : MCS251RegWithSubRegs<"dptr", 63, [DPL, DPH]>;

//===----------------------------------------------------------------------===//
//  The condition-flags register PSW
//===----------------------------------------------------------------------===//
//
// Physical layout ruling (Intel 8XC251Sx manual section 5.6 + project
// ruling, do not change without new measurements):
//
//   * CY/AC/RS/OV live at the same bit positions in BOTH PSW (SFR 0xD0) and
//     PSW1 (SFR 0xD1); N and Z exist only in PSW1.
//   * Every flag consumer in the current architecture (jcc, and every
//     flag producer we emit: add/sub/anl/orl/xrl/cmp/inc/dec per
//     Table 5-10) addresses the flags as one implicit unit -- no
//     instruction reads or writes an individual bit through an addressable
//     location in this backend.
//
// We therefore model the flags as ONE virtual 8-bit register "PSW"
// (same approach as upstream MSP430's single SR / AVR's SREG):
//
//   * PSW is a member of no register class and is reserved in
//     getReservedRegs(), so the allocator can never hand it out.
//   * Flag producers declare Defs = [PSW], flag consumers declare
//     Uses = [PSW]; ordering between them is additionally pinned with DAG
//     glue where the pair is built manually (see LowerBR_CC).
//   * PSW is call-clobbered (getCalleeSavedRegs returns the empty set), so
//     a future call phase will not need any saving machinery.
//
// It is isArtificial so it never participates in sub-register lanemask
// bookkeeping.
def PSW : Register<"psw"> {
  let Namespace = "MCS251";
  let isArtificial = 1;
}

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//
//
// Member order is allocation priority order; plain ascending order for now.
// It can be tuned later to match SDCC's allocation preferences (r7..r0
// first). The alignment argument is byte alignment (8 = byte aligned, as in
// upstream AVR's 16-bit class).

def GPR8 : RegisterClass<"MCS251", [i8], 8, (add R0, R1, R2, R3, R4, R5, R6,
                                             R7, R8, R9, R12, R13, R14, R15)>;

// Keep shift-loop counters in the classic low-register bank. A real subclass
// and live-range interference prevent aliasing the shifted WR/DR value.
def GPR8Low : RegisterClass<"MCS251", [i8], 8,
                            (add R0, R1, R2, R3, R4, R5, R6, R7)>;

// wr10 is excluded because it covers r10/r11 (B/ACC); wr16+ are not
// allocatable in this phase.
def GPR16 : RegisterClass<"MCS251", [i16], 8, (add WR0, WR2, WR4, WR6, WR8,
                                               WR12, WR14)>;

// Keep the allocatable DR set closed under word/byte lane extraction and
// spilling. DR8 overlaps the fixed A/B ABI locations through R10/R11; DR16+
// have no encodable byte-register lanes. Allocating them for a PHI/spill
// bypasses the DAG subregister constraints and would silently corrupt values.
// DR16 (private frame anchor), DR56 (DPX) and DR60 (SPX) remain reserved.
def GPR32 : RegisterClass<"MCS251", [i32], 8, (add DR0, DR4, DR12)>;

// TFPU operand parking (G7 S3, G7-FLOAT-DESIGN-draft.md §2.4). The AR (DR4)
// and BR (DR0) operands must be materialised OUTSIDE the PSW[4:3] selected
// R0-R7 bank window, so that every write into the coprocessor's
// operand/result bytes happens INSIDE the TPIN measurement window: the
// window loads are emitted by the post-RA MCS251TFPUExpand pass as the first
// step of the TFPU_<OP> pseudo (mov dr4,<ar> / mov dr0,<br>), never as a
// separate copy at the operand's definition site.
//
// DR12 is the ONLY allocatable 32-bit register outside that window: GPR32 is
// {DR0, DR4, DR12}, DR0/DR4 are the two window halves themselves, DR8
// overlaps the fixed A/B ABI bytes and DR16+ have no encodable byte lanes
// (see the GPR32 comment above). The class is therefore a singleton, which
// is exactly the "park it outside the window" constraint the design states;
// register pressure around a TFPU sequence is handled by the ordinary
// spill/reload machinery, and a reload lands before the pseudo -- inside the
// window, on the parked register, not in R0-R7.
def GPR32Win : RegisterClass<"MCS251", [i32], 8, (add DR12)>;

// TFPU readback destination (G7 S3, design §2.4 step 4). The readback
// `mov <result>, DR4` MUST be a real instruction inside the TPIN window, so
// its destination has to be a register OTHER than DR4: if the allocator
// placed the result vreg in DR4 the expansion would be `mov dr4, dr4`, the
// snapshot would vanish, and the dr4 lanes would again be read at the first
// consumer (the blocker-1 readback bug).
//
// Unlike the operand parking class this one is NOT a singleton. The operand
// side genuinely needs the one register outside the window (dr12), but a
// readback value and a parked operand can be live simultaneously (a second
// command whose operand was gathered before the first command ran), and a
// second singleton live range would deadlock the allocator ("ran out of
// registers" on chained/nested TFPU expressions). DR0 is the second
// candidate: for a unary command r0-r3 are not used by the coprocessor at
// all, and for the binary four the BR half is dead once the trigger has
// fired. A live readback value in DR0 across a LATER window is protected by
// the ordinary implicit-def machinery (TFPU_LD_BR defs DR0, TFPU_<OP> defs
// r0-r7), so the allocator relocates or spills it -- correctness never rests
// on the allocator's choice, only the shape does.
//
// DR4 is excluded by construction; DR8/DR16+ are unallocatable for the same
// reasons as in GPR32 above. Order is allocation preference: dr12 first (it
// is outside every window half, so a readback there writes no operand byte),
// dr0 second for the nested case where dr12 still holds a parked operand.
def GPR32Rd : RegisterClass<"MCS251", [i32], 8, (add DR12, DR0)>;

// Non-allocatable class holding the two stack-addressing bases: dr60 (spx,
// the default frame reference) and dr16 (the frame anchor used when the
// function has variable-sized objects, see MCS251FrameLowering). The stack
// access instructions (MOV8rmS etc.) take their base from this class so
// eliminateFrameIndex can substitute either register via ChangeToRegister.
// Same isAllocatable=0 pattern as DP8/DP16 below.
let isAllocatable = 0 in
def SPBASE : RegisterClass<"MCS251", [i32], 8, (add DR60, DR16)>;

// Non-allocatable classes for the fixed SFR ABI locations (dpl/dph and their
// 16-bit overlay dptr), same pattern as upstream X86's CCR for EFLAGS. They
// exist so TargetRegisterInfo::getMinimalPhysRegClass() can answer for these
// registers: the source-level scheduler's CheckForPhysRegDependency calls it
// whenever a CopyToReg's source is a CopyFromReg of the same physical
// register -- exactly the `return g();` forwarding shape, where the call
// result in dpl feeds the caller's own return slot. Without these classes
// the lookup returns null there (an assert build catches it; a release
// build dereferences it). CopyCost = -1 marks the pair-of-lanes copy as
// expensive, so the scheduler models the dependency as a physical-register
// one and keeps the copy chain ordered; the registers stay reserved in
// getReservedRegs() and can never be allocated. PSW never needs this: it is
// only ever referenced implicitly and never through CopyFromReg/CopyToReg.
let CopyCost = -1, isAllocatable = 0 in {
  def DP8  : RegisterClass<"MCS251", [i8],  8, (add DPL, DPH)>;
  def DP16 : RegisterClass<"MCS251", [i16], 8, (add DPTR)>;
  def AB8  : RegisterClass<"MCS251", [i8],  8, (add A, B)>;
}

//===----------------------------------------------------------------------===//
//  Call-preserved registers (caller side)
//===----------------------------------------------------------------------===//
//
// The SDCC MCS-251 ABI (measurement-verified, QEMU stc32g144k246): everything
// is caller-saved across an ecall -- the callee may freely clobber r0-r9 /
// r12-r15, acc/b, dpl/dph (dptr), dpx (dr56), the dr0..dr28 file and psw.
// Hence the callee-saved list is EMPTY. The stack pointer dr60 (spx) must of
// course hold a valid sp when ecall returns; OtherPreserved marks it as
// preserved-by-the-call-machinery (the bit is set in CSR_MCS251_RegMask)
// without making it a callee-saved register that prologue/epilogue insertion
// would try to push/pop -- it is reserved and never allocated.
//
// The mask derived from this def is what LowerCall attaches to the
// MCS251ISD::CALL node (getCallPreservedMask); DPL/DPH/DPTR deliberately
// have no preserved bits: call results are read out via CopyFromReg, never
// assumed live across the call.
def CSR_MCS251 : CalleeSavedRegs<(add)> {
  let OtherPreserved = (add DR60);
}
