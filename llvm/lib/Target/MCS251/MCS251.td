//===-- MCS251.td - Describe the MCS-251 target -----------*- tablegen -*-===//

include "llvm/Target/Target.td"

include "MCS251RegisterInfo.td"
include "MCS251CallingConv.td"
include "MCS251InstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR16>;

def MCS251InstrInfo : InstrInfo;

class Proc<string Name> : Processor<Name, NoItineraries, []>;
def : Proc<"generic">;
def : Proc<"mcs251">;

def MCS251AsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
}

def MCS251 : Target {
  let InstructionSet = MCS251InstrInfo;
}
