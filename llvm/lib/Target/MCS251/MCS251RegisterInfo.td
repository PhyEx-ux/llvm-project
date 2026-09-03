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
//  Register classes
//===----------------------------------------------------------------------===//
//
// Member order is allocation priority order; plain ascending order for now.
// It can be tuned later to match SDCC's allocation preferences (r7..r0
// first). The alignment argument is byte alignment (8 = byte aligned, as in
// upstream AVR's 16-bit class).

def GPR8 : RegisterClass<"MCS251", [i8], 8, (add R0, R1, R2, R3, R4, R5, R6,
                                             R7, R8, R9, R12, R13, R14, R15)>;

// wr10 is excluded because it covers r10/r11 (B/ACC); wr16+ are not
// allocatable in this phase.
def GPR16 : RegisterClass<"MCS251", [i16], 8, (add WR0, WR2, WR4, WR6, WR8,
                                               WR12, WR14)>;

// dr56 (dpx) and dr60 (spx) are reserved and not allocatable.
def GPR32 : RegisterClass<"MCS251", [i32], 8, (add DR0, DR4, DR8, DR12, DR16,
                                               DR20, DR24, DR28)>;
