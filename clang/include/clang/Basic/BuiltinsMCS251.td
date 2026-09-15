//===--- BuiltinsMCS251.td - MCS-251 Builtin function database -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MCS-251-specific builtin function database.
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

// L1 controlled fixed bit lvalue (BT04). The single argument is a bit address
// in the MCS-251 bit space (RAM 0x00-0x7F plus SFR 0x80-0xFF).
//
// The prototype is variadic and the builtin uses CustomTypeChecking so that no
// implicit argument conversion (and therefore no narrowing) is applied before
// Sema sees the operand: SemaMCS251::CheckMCS251BuiltinFunctionCall evaluates
// the raw integer constant expression and range-checks the full APSInt, so an
// out-of-range or over-wide constant is rejected on its true width rather than
// after a truncating conversion to the parameter type.
//
// Sema then marks the call expression as a controlled, volatile, non-
// addressable bit lvalue whose value semantics are boolean and whose storage is
// the fixed bit address carried in this operand. It never yields an ordinary
// pointer or byte object. CodeGen for the closed capability is M2; until then
// the frontend fails closed rather than emitting a byte global/store.
def __builtin_mcs251_bit_lvalue :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_bit_lvalue"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "__bit(...)";
}

//===----------------------------------------------------------------------===//
// G7 S3 (G7-FLOAT-DESIGN-draft.md §2.3 plan A / §2.4): the TFPU DMA
// coprocessor math builtins. Nine commands in one batch (PM ruling D3):
// the five unary transcendentals and the four binary arithmetic ops.
//
// The prototype is a variadic placeholder with CustomTypeChecking: no
// implicit argument conversion is applied before Sema sees the operands,
// so SemaMCS251::CheckMCS251BuiltinFunctionCall enforces the EXACT f32
// scalar type on every argument (f64, integers, pointers and anything
// else are rejected with a frozen diagnostic; on this target `double` is
// 32 bits wide but is still a distinct type from `float` and is refused
// -- routing it through would silently claim an f64 path that does not
// exist).
//
// CodeGen (CodeGenFunction::EmitMCS251BuiltinExpr) bitcasts each float
// argument to its IEEE-754 i32 bit pattern, calls the corresponding
// llvm.mcs251.tfpu.* intrinsic, and bitcasts the result back to float.
// The intrinsic is [IntrHasSideEffects], so nothing reorders the hardware
// sequence out of the demo 38 TPIN measurement window.
//===----------------------------------------------------------------------===//

def __builtin_mcs251_tfpu_sin :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_sin"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_cos :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_cos"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_tan :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_tan"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_atan :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_atan"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_sqrt :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_sqrt"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_add :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_sub :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_mul :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_mul"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
def __builtin_mcs251_tfpu_div :
    TargetBuiltin {
  let Spellings = ["__builtin_mcs251_tfpu_div"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "float(...)";
}
