// Test that the independent MCS-251 `bit` type and an old-style `sbit` fixed
// reference survive an AST/PCH round-trip with the same canonical identity
// (BT08). The bit type is a predefined type with its own serialized ID, not a
// re-created _Bool, and the fixed reference keeps its MCS251BitAddress
// attribute.

// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-pch -o %t/pch.pch %t/header.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -include-pch %t/pch.pch -fsyntax-only -verify %t/use.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -include-pch %t/pch.pch -ast-dump %t/use.c | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-pch -o %t/pch-gen.pch %t/header-gen.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -include-pch %t/pch-gen.pch -emit-llvm -o - %t/use-codegen.c | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -include-pch %t/pch-gen.pch -emit-llvm -o - %t/use-codegen.c | opt -passes=verify -S - | %python %S/../CodeGen/Inputs/mcs251-bit-structure-check.py --mode=pch

//--- header.c
typedef __bit BOOL;
__bit bit_global;
sbit FIXED = 0x88 ^ 3;

//--- use.c
// expected-no-diagnostics
// CHECK: FunctionDecl {{.*}} use_bit
// CHECK: ParmVarDecl {{.*}} p 'BOOL':'__bit'
void use_bit(BOOL p) {
  // CHECK: VarDecl {{.*}} local '__bit'
  __bit local = p;
  bit_global = local;
}

// The fixed reference's identity survives the round-trip: references to it are
// still typed `volatile __bit` (the PCH decl itself is not re-dumped in the
// use TU).
// CHECK: DeclRefExpr {{.*}} 'volatile __bit' lvalue Var {{.*}} 'FIXED' 'volatile __bit'
void use_fixed(void) {
  FIXED = 1;
}

//--- header-gen.c
// CodeGen-side PCH payload: only declarations that have a CodeGen lowering
// (the fixed bit reference). The ordinary bit global of header.c has no
// lowering yet and is intentionally not part of this round-trip.
sbit FIXED = 0x88 ^ 3;

//--- use-codegen.c
// CodeGen after the PCH round-trip is the fixed-bit intrinsic lowering: the
// serialized MCS251BitAddress attribute still resolves to 0x88 ^ 3 = 0x8B
// (139), so writes through the PCH-loaded reference are target bit writes,
// not byte stores. Exactly one set and one clear in the function, with the
// whole bit-call ban restated after every positive match (M2-4 discipline,
// M2-6 hardening: the fence between the set and the clear is the generic
// bit-call ban, so an interleaved read or toggle through the PCH-loaded
// reference is rejected too, not just a second set).
//
// Reachability pinning (M2-17 hardening, Alice r6/r7; M2-19 closure, r8):
// textual order alone cannot prove the writes execute. The function's
// `entry:` label is matched positively right after the define, and the
// control-flow fence (no branch, return, switch/invoke/unreachable, block
// label, or further function definition) is restated after entry and after
// the set: the writes must live in the entry block itself, so an entry that
// returns immediately with both writes parked behind a dead label --
// including a dead block nested behind another dead block -- is rejected,
// and a clear write similarly cannot move out from between the set and the
// end of the entry block. The extra RUN line backs this structurally:
// Inputs/mcs251-bit-structure-check.py --mode=pch (piped behind
// `opt -passes=verify -S`, so its input is verifier-clean illegal-IR-free)
// verifies every block containing an llvm.mcs251.bit.* call is reachable
// from the entry block and that the payload TU defines exactly one
// function, quoted function names included -- a spliced-in carrier
// function, however spelled, is rejected too, and any instruction form the
// closed-world checker does not recognize fails the check (fail-closed).
// M2-24..28: memory widths never default to zero; only the four exact bit
// intrinsic names are exempt from unknown-call effects. Phi incoming values,
// half-open byte intervals and balanced switch/call continuations share the
// checker regressions run by CodeGen/mcs251-bit-fixed-ref.c. va_arg is parsed
// as an instruction and conservatively models its in-memory list update.
// IR-LABEL: define {{.*}}@use_fixed_codegen(
// IR: entry:
// IR-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// IR-NOT: call {{.*}}@llvm.mcs251.bit.
// IR: call {{.*}}void @llvm.mcs251.bit.set(i32 139)
// IR-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// IR-NOT: call {{.*}}@llvm.mcs251.bit.
// IR: call {{.*}}void @llvm.mcs251.bit.clear(i32 139)
// IR-NOT: call {{.*}}@llvm.mcs251.bit.
void use_fixed_codegen(void) {
  FIXED = 1;
  FIXED = 0;
}
