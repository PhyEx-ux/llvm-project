// REQUIRES: mcs251-registered-target
// RUN: not %clang_cc1 -triple mcs251-unknown-none -ffreestanding -Wno-varargs -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s

// G2 B-S2 (G2-VARIADIC-DESIGN-draft.md R3 §4.6): `va_arg(ap, __bit)` stays
// on the frozen M2 boundary -- the B1 continuation-slot ABI defines no bit
// encoding.  Sema only warns today (-Wvarargs; the M2 gap registered in
// MCS251.h), so the CodeGen backstop stops loudly instead of the
// historical silently-wrong i8-slot compile (which then crashed llc with a
// generic "Cannot select" on the old va_list).  If this ever moves to a
// proper Sema diagnostic, update this test to match.

#include <stdarg.h>

int f(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int v = __builtin_va_arg(ap, __bit);
  va_end(ap);
  return v;
}

// CHECK: fatal error: error in backend: MCS251: va_arg of __bit is not supported (frozen M2 boundary, G2 §4.6)
