//===-- MCS251RegisterInfo.td - MCS-251 registers ---------*- tablegen -*-===//

class MCS251Reg<string Name, bits<6> Encoding> : Register<Name> {
  let Namespace = "MCS251";
  let HWEncoding{5-0} = Encoding;
  let DwarfNumbers = [Encoding];
}

class MCS251RegWithSubRegs<string Name, bits<6> Encoding,
                           list<Register> SubRegs>
    : RegisterWithSubRegs<Name, SubRegs> {
  let Namespace = "MCS251";
  let HWEncoding{5-0} = Encoding;
  let DwarfNumbers = [Encoding];
}

def sub_lo8 : SubRegIndex<8, 0> { let Namespace = "MCS251"; }
def sub_hi8 : SubRegIndex<8, 8> { let Namespace = "MCS251"; }

def R0 : MCS251Reg<"r0", 0>;
def R1 : MCS251Reg<"r1", 1>;

let SubRegIndices = [sub_lo8, sub_hi8], CoveredBySubRegs = 1 in
def WR0 : MCS251RegWithSubRegs<"wr0", 0, [R0, R1]>;

// Extended stack pointer. It is deliberately not allocatable in this bring-up
// backend.
def SPX : MCS251Reg<"spx", 60>;

def GPR8 : RegisterClass<"MCS251", [i8], 8, (add R0, R1)>;
def GPR16 : RegisterClass<"MCS251", [i16], 16, (add WR0)>;
def GPR32 : RegisterClass<"MCS251", [i32], 32, (add SPX)>;
