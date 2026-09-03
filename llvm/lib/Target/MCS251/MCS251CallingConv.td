//===-- MCS251CallingConv.td - Calling conventions for MCS-251 -*- tablegen -*-===//
//
// Return-value ABI measured on hardware (SDCC 4.6.0 mcs251, sdas251
// V05.50.4, QEMU stc32g144k246), do not change without new measurements:
//
//   i8  return value: dpl                    (mov dpl, ... ; eret)
//   i16 return value: dpl:dph (dpl = low byte, dph = high byte)
//
// Argument registers (first argument enters in dpl) are handled in a later
// phase and deliberately not described here yet.

def RetCC_MCS251 : CallingConv<[
  CCIfType<[i8],  CCAssignToReg<[DPL]>>,
  CCIfType<[i16], CCAssignToReg<[DPTR]>>
]>;
