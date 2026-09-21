// CodeGen-level fail-closed gates for the MCS-251 `bit` capability: the
// P01 explicit-i8 ABI slice for function boundaries is not implemented yet,
// so a bit return type and a bit call argument are rejected instead of
// degrading to a zeroext i1 boundary. The object/storage gates from M1
// (parameter, auto object, compound literal, alias, block capture) are
// re-asserted here at the CodeGen layer to guard against regressions, and
// the Sema rejection of a used toggle result is pinned.
//
// P-1b positive migration: static and global bit objects now emit their
// bit-object handles (see gate-static/gate-global below and the structural
// assertions in CodeGen/mcs251-bit-objects.c); the remaining gates stay
// fail-closed.
//
// Every section compiles with rc=1 and the diagnostic text is asserted.

// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=sta %t/gate-static.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=glo %t/gate-global.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=cl %t/gate-compound.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=ali %t/gate-alias.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fblocks -emit-llvm -o /dev/null -verify=blk %t/gate-block.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=tog %t/gate-used-toggle.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fcommon -emit-llvm -o /dev/null -verify=com %t/gate-common.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=wks %t/gate-weak.c

// (gate-return / gate-return-indirect removed: bit returns and single-argument
// indirect bit returns are SUPPORTED since P-3 (design 4.1); positive coverage
// lives in mcs251-bit-abi.c -- ret_bit, call sites, indirect_ret_bit, fp globals.


// (gate-call-arg / -sbit / -dyn / gate-param / gate-local removed: bit call
// arguments, sbit/dynamic bit lvalue arguments, bit formal parameters and
// auto bit locals are SUPPORTED since P-2/P-3; positive coverage lives in
// CodeGen/mcs251-bit-abi.c and mcs251-bit-local.c.)





//--- gate-static.c
// P-1b positive migration: a static bit object now emits its bit-object
// handle (structural IR assertions in CodeGen/mcs251-bit-objects.c); the
// declaration and the use compile cleanly. The automatic-local form stays
// fail-closed (gate-local below, P-2).
// sta-no-diagnostics
void f(void) {
  static __bit b;
  (void)b;
}

//--- gate-global.c
// P-1b positive migration: a global bit object compiles to its handle; the
// old M1 fail-closed gate is gone.
// glo-no-diagnostics
__bit b;

//--- gate-compound.c
// M1 gate, regression: a bit compound literal fails closed.
int f(void) {
  // cl-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  return (__bit){1};
}

//--- gate-alias.c
// M1 gate, regression: a bit alias fails closed.
unsigned char backing;
// ali-error@+1 {{cannot compile this MCS251 bit alias yet}}
extern __bit y __attribute__((alias("backing")));

//--- gate-block.c
// M1 gate, regression: a block capturing a bit object fails closed.
int f(void) {
  // blk-error@+1 {{cannot compile this MCS251 bit object yet}}
  __block __bit x = 1;
  // blk-error@+1 {{cannot compile this MCS251 bit block capture yet}}
  int (^b)(void) = ^{ return +x; };
  return b();
}

//--- gate-used-toggle.c
// Sema gate, pinned: the result of a controlled toggle cannot be used.
sbit Q = 0x80;
// tog-error@+1 {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
int f(void) { int x = (Q ^= 1); return x; }

//--- gate-common.c
// P09 section 2.2 gate: under -fcommon a plain tentative bit definition
// would actually become a COMMON symbol; that is diagnosed rather than being
// silently degraded to an external strong definition that masks the user
// option. An explicitly-initialized strong definition and an internal static
// are never COMMON and keep compiling under -fcommon (the structural form of
// their handles is asserted in CodeGen/mcs251-bit-objects.c).
// com-error@+1 {{cannot compile this MCS251 bit COMMON tentative definition yet}}
__bit t;
__bit s = 1;
static __bit i;
void use(void) { s = 0; i = 1; }

//--- gate-weak.c
// P09 section 2.1 gate: weak- and section-annotated bit objects stay outside
// the supported handle forms (a user section would also fight the dedicated
// .mcs251.bit record section). Both references live in one function: after
// the first unsupported-object error IRGen skips later top-level decl
// groups, so per-object diagnostics are only deterministic within the same
// declaration group. Codegen continues on a safely-shaped plain handle
// instead of crashing after the error.
// WP4 A8: the weak DEFINITION is rejected in Sema (the first error below);
// IRGen still re-asserts its own weak/section gate for both objects, so
// line 13 carries both diagnostics and line 14 the section one.
// wks-error@+2 {{weak variable definitions are not supported on MCS251}} wks-error@+2 {{cannot compile this MCS251 bit weak/section object yet}}
// wks-error@+2 {{cannot compile this MCS251 bit weak/section object yet}}
__attribute__((weak)) __bit w;
__bit s __attribute__((section("MYBITS")));
void use(void) { w = 1; s = 0; }
