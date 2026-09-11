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
