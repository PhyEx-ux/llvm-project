//===-- MCS251CallingConv.td - Calling conventions for MCS-251 -*- tablegen -*-===//
//
// Return-value ABI measured on hardware (SDCC 4.6.0 mcs251, sdas251
// V05.50.4, QEMU stc32g144k246), do not change without new measurements:
//
//   i8  return value: dpl                    (mov dpl, ... ; eret)
//   i16 return value: dpl:dph (dpl = low byte, dph = high byte)
//
// Argument ABI, also measured on the same toolchain/hardware:
//
//   i8  first argument: dpl
//   i16 first argument: dpl:dph (dpl = low byte, dph = high byte), i.e. dptr
//
// SDCC passes the second and later arguments in static OSEG overlay slots
// (_FUNCNAME_PARM_n), not on the stack and not in registers. That scheme is
// not implemented by this minimal backend, so CC_MCS251 deliberately
// describes only the single first-argument slot: any further argument (or a
// wider type) falls through to the default stack assignment, which
// LowerFormalArguments detects and rejects with a clear error instead of
// silently miscompiling.

def RetCC_MCS251 : CallingConv<[
  CCIfType<[i8],  CCAssignToReg<[DPL]>>,
  CCIfType<[i16], CCAssignToReg<[DPTR]>>
]>;

def CC_MCS251 : CallingConv<[
  CCIfType<[i8],  CCAssignToReg<[DPL]>>,
  CCIfType<[i16], CCAssignToReg<[DPTR]>>
]>;
