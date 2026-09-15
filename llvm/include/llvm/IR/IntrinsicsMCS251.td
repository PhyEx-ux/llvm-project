//===- IntrinsicsMCS251.td - Defines MCS251 intrinsics -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the MCS251-specific intrinsics.
//
// Controlled bit access (BIT task breakdown BT03). These are the ONLY routes
// to the bit-address space: ordinary address-space-5 pointers/loads/stores
// stay fail-closed (see MCS251ISelLowering::checkDataAddressSpace). The
// address operand is a constant i32 bit address in [0, 255]:
//
//   0x00-0x7f  internal RAM bit space (backing byte 0x20 + (B >> 3))
//   0x80-0xff  bit-addressable SFR space (backing byte B & 0xf8)
//
// Memory effect model (BT03 / design §2.3): a bit access aliases ordinary
// RAM (the page-zero bit space overlaps edata) and reaches real memory, so
// NONE of these may be marked IntrNoMem, IntrReadMem/WriteMem, IntrArgMemOnly
// (there is no pointer argument), IntrInaccessibleMemOnly or
// IntrSpeculatable. Every access is implicitly volatile: the fixed bit
// location is a hardware/ISR-shared object, so dead-access elimination and
// reordering across other memory must not happen. Leaving the properties
// unset means the worst case is assumed (may read and write any memory and
// may have other side effects), which is exactly the required conservatism.
//
// The user-facing contract keeps `read` returning i1 and the writers
// returning void. read lowers to a single MOV C,bit sample; the carry/PSW
// effect of that instruction is modelled with a glue dependency in the
// lowering (see MCS251ISelLowering).
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "mcs251" in { // All intrinsics start with "llvm.mcs251.".

  // Sample one bit into a 0/1 value. i1 result; the lowering materialises the
  // carried bit into a byte vreg (this target has no i1 register class).
  // ImmArg<0>: the bit address must be an IR constant; a dynamic address is
  // rejected by the IR verifier before any backend code runs.
  def int_mcs251_bit_read : Intrinsic<[llvm_i1_ty], [llvm_i32_ty],
                                      [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  // Read-modify-write updates of a single bit: one target instruction each.
  // toggle is a single atomic CPL (not read-then-store): it must not be split
  // into separate read and write ops.
  def int_mcs251_bit_set : Intrinsic<[], [llvm_i32_ty],
                                     [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_mcs251_bit_clear : Intrinsic<[], [llvm_i32_ty],
                                       [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_mcs251_bit_toggle : Intrinsic<[], [llvm_i32_ty],
                                        [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  // Symbolic persistent bit-object access (P09-BIT-CODEGEN-DESIGN section 1).
  // Keil `typedef bit BOOL` compatibility: a persistent/static `bit` object
  // reaches the backend as an i8 GlobalVariable handle carrying the structural
  // attribute "mcs251-bit-object" (see MCS251BitObject.h); these four are the
  // ONLY consumers of that handle. The single parameter is the handle itself
  // (llvm_ptr_ty = fixed AS0 opaque pointer, NOT llvm_anyptr_ty): object
  // identity, never a byte address. The bit number is resolved by the linker
  // from R_MCS251_BITADDR8, so the handle is a legal constant operand but not
  // an integer immediate -- deliberately no ImmArg.
  //
  // Not overloaded, not vararg, exactly one parameter. Signature contract:
  //   read -> i1 (ptr)    set/clear/toggle -> void (ptr)
  //
  // Effect model (section 1.2): identical conservatism to the fixed family
  // above -- [IntrHasSideEffects] and nothing else. read may also write any
  // memory, the writers are not writeonly, and the pointer parameter is an
  // identity tag rather than a backing address, so argmemonly/readonly/
  // noalias/dereferenceable narrowing is never expressible or permitted.
  // Symbolic objects only ever name RAM bits 0..127; SFR/fixed-ABS accesses
  // keep using the fixed i32 ImmArg family above. set/clear do not read the
  // logical old value first; toggle is one CPL and returns nothing.
  def int_mcs251_bit_obj_read : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty],
                                          [IntrHasSideEffects]>;
  def int_mcs251_bit_obj_set : Intrinsic<[], [llvm_ptr_ty],
                                         [IntrHasSideEffects]>;
  def int_mcs251_bit_obj_clear : Intrinsic<[], [llvm_ptr_ty],
                                           [IntrHasSideEffects]>;
  def int_mcs251_bit_obj_toggle : Intrinsic<[], [llvm_ptr_ty],
                                            [IntrHasSideEffects]>;

  // G2 B1 (G2-VARIADIC-DESIGN-draft.md R3 §4.3.6): the deterministic va_arg
  // out-of-range halt.  Lowers to the VARARG_HALT machine instruction, which
  // encodes as the two bytes 80 FE -- `sjmp .`, a branch to itself: two
  // bytes, no call frame, no runtime symbol dependency.  Deliberately NOT
  // llvm.trap, whose generic expansion on this target is an `_abort` libcall
  // the runtime has no provider for (R3 probe V3).  Emitted only by clang
  // CodeGen's MCS251ABIInfo::EmitVAArg out-of-range arm; control never
  // returns.  Worst-case effect model like the bit family above, so nothing
  // may reorder or dead-store it.
  def int_mcs251_vararg_halt : Intrinsic<[], [], [IntrHasSideEffects]>;
}
