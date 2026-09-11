// CodeGen-level fail-closed gates for the MCS-251 `bit` capability (BIT M2):
// the P01 explicit-i8 ABI slice for function boundaries is not implemented
// yet, so a bit return type and a bit call argument are rejected instead of
// degrading to a zeroext i1 boundary. The object/storage gates from M1
// (parameter, auto/static/global object, compound literal, alias, block
// capture) are re-asserted here at the CodeGen layer to guard against
// regressions, and the Sema rejection of a used toggle result is pinned.
//
// Every section compiles with rc=1 and the diagnostic text is asserted.

// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=ret %t/gate-return.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=ret %t/gate-return-indirect.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=arg %t/gate-call-arg.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=arg %t/gate-call-sbit.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=arg %t/gate-call-dyn.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=prm %t/gate-param.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=loc %t/gate-local.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=sta %t/gate-static.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=glo %t/gate-global.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=cl %t/gate-compound.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=ali %t/gate-alias.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fblocks -emit-llvm -o /dev/null -verify=blk %t/gate-block.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=tog %t/gate-used-toggle.c

//--- gate-return.c
// A bit return type must not degrade to a zeroext i1 return.
// ret-error@+1 {{cannot compile this MCS251 bit return type yet}}
__bit f(void) { return 1; }

//--- gate-return-indirect.c
// Calling a bit-returning function indirectly never creates the LLVM
// function for the callee decl, so the same gate is enforced at the call
// site: no zeroext i1 indirect call either.
__bit (*fp)(void);
// ret-error@+1 {{cannot compile this MCS251 bit return type yet}}
int h(void) { fp(); return 1; }

//--- gate-call-arg.c
// A bit actual argument must not cross the boundary as zeroext i1.
extern void g(__bit);
// arg-error@+1 {{cannot compile this MCS251 bit argument yet}}
void f(void) { g(1); }

//--- gate-call-sbit.c
// Passing a controlled fixed bit reference as a bit argument is equally
// rejected (it would have to be read and then ABI-normalized).
sbit P = 0x80;
extern void g(__bit);
// arg-error@+1 {{cannot compile this MCS251 bit argument yet}}
void f(void) { g(P); }

//--- gate-call-dyn.c
// A converted dynamic value does not open the boundary either.
extern void g(__bit);
// arg-error@+1 {{cannot compile this MCS251 bit argument yet}}
void f(int x) { g((__bit)x); }

//--- gate-param.c
// M1 gate, regression: a bit parameter definition fails closed.
// prm-error@+1 {{cannot compile this MCS251 bit parameter yet}}
void f(__bit p) {}

//--- gate-local.c
// M1 gate, regression: an automatic bit object fails closed (declaration and
// use each report).
void f(void) {
  // loc-error@+1 {{cannot compile this MCS251 bit object yet}}
  __bit b = 1;
  // loc-error@+1 {{cannot compile this MCS251 bit object yet}}
  (void)b;
}

//--- gate-static.c
// M1 gate, regression: a static bit object fails closed.
void f(void) {
  // sta-error@+1 {{cannot compile this MCS251 bit object yet}}
  static __bit b;
  // sta-error@+1 {{cannot compile this MCS251 bit object yet}}
  (void)b;
}

//--- gate-global.c
// M1 gate, regression: a global bit object fails closed.
// glo-error@+1 {{cannot compile this MCS251 bit global yet}}
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
