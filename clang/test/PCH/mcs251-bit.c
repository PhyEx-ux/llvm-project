// Test that the independent MCS-251 `bit` type and an old-style `sbit` fixed
// reference survive an AST/PCH round-trip with the same canonical identity
// (BT08). The bit type is a predefined type with its own serialized ID, not a
// re-created _Bool, and the fixed reference keeps its MCS251BitAddress
// attribute.

// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-pch -o %t/pch.pch %t/header.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -include-pch %t/pch.pch -fsyntax-only -verify %t/use.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -include-pch %t/pch.pch -ast-dump %t/use.c | FileCheck %s

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
