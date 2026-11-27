// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -Wno-varargs -emit-llvm -o - %s | FileCheck %s

// Under the stack-based reentrant variadic ABI, `va_arg(ap, __bit)` is
// supported: a `__bit` argument undergoes default argument promotion to `int`
// at the call site, and `EmitVAArg` reads the promoted integer slot from the
// downward-stepping stack cursor and normalizes it to 0/1 (`icmp ne 0` + `zext`).

#include <stdarg.h>

int f(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int v = __builtin_va_arg(ap, __bit);
  va_end(ap);
  return v;
}

// CHECK-LABEL: define dso_local i32 @f(i32 noundef %n, ...)
// CHECK: %va.off.next = add i32 %va.off, 4
// CHECK: %va.negoff = sub i32 0, %va.off.next
// CHECK: %va.addr = getelementptr i8, ptr %va.base, i32 %va.negoff
// CHECK: %[[RAW:[0-9]+]] = load i32, ptr %va.addr, align 1
// CHECK: %va.bit.nz = icmp ne i32 %[[RAW]], 0
// CHECK: %va.bit = zext i1 %va.bit.nz to i8
// CHECK-NOT: va_arg
