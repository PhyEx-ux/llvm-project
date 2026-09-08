//===-- MCS251CallingConv.td - Calling conventions for MCS-251 -*- tablegen -*-===//
//
// Return-value ABI measured on hardware (SDCC 4.6.0 mcs251, sdas251
// V05.50.4, QEMU stc32g144k246), do not change without new measurements:
//
//   i8  return value: dpl                    (mov dpl, ... ; eret)
//   i16 return value: dpl:dph (dpl = low byte, dph = high byte)
//   i32 return value: dpl:dph:b:a (least-significant byte first)
//   f32 return value: same binary32 payload lanes as i32
//
// Argument ABI, also measured on the same toolchain/hardware:
//
//   i8  first argument: dpl
//   i16 first argument: dpl:dph (dpl = low byte, dph = high byte), i.e. dptr
//   i32 first argument: dpl:dph:b:a (least-significant byte first)
//   f32 first argument: same binary32 payload lanes as i32
//
// SDCC passes the second and later arguments in static OSEG overlay slots
// (_FUNCNAME_PARM_n), not on the stack and not in registers. That scheme is
// not implemented by this minimal backend, so CC_MCS251 deliberately
// describes only the single first-argument slot: any further argument (or a
// wider type) falls through to the default stack assignment, which
// LowerFormalArguments detects and rejects with a clear error instead of
// silently miscompiling.

// The i32 DPL assignment is only a CCState gatekeeper: CCAssignToReg assigns
// one value to one candidate register, not one i32 value across four byte
// registers. The real i32 ABI layout (DPL/DPH/B/A, least-significant byte
// first) is implemented explicitly by splitI32ToBytes/combineI32FromBytes and
// their I32ABIRegs table in MCS251ISelLowering.cpp.
def RetCC_MCS251 : CallingConv<[
  CCIfType<[i8],  CCAssignToReg<[DPL]>>,
  CCIfType<[i16], CCAssignToReg<[DPTR]>>,
  CCIfType<[i32, f32], CCAssignToReg<[DPL]>>
]>;

def CC_MCS251 : CallingConv<[
  CCIfType<[i8],  CCAssignToReg<[DPL]>>,
  CCIfType<[i16], CCAssignToReg<[DPTR]>>,
  CCIfType<[i32, f32], CCAssignToReg<[DPL]>>
]>;
