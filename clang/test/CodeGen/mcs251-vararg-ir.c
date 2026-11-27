// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -Wno-varargs -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -target-feature +int16 -ffreestanding -Wno-varargs -emit-llvm -o - %s | FileCheck %s --check-prefix=INT16
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,1,1 -ffreestanding -Wno-varargs -emit-llvm -o - %s | FileCheck %s --check-prefix=TINY

// Stack-based reentrant variadic ABI: `__builtin_va_list` is `{ ptr __base, i32 __off }`
// (8 bytes with 32-bit default data pointers, 6 bytes with 16-bit data pointers).
// `va_start`/`va_end`/`va_copy` keep their LLVM intrinsics; `va_arg` is lowered
// directly by `MCS251ABIInfo::EmitVAArg` into a downward-stepping stack load
// (`base - (off + step)`), with no 6-slot cap or halt branch, and never emits
// an `llvm.va_arg` intrinsic.

#include <stdarg.h>

// CHECK: %struct.__va_list_tag = type { ptr, i32 }
// INT16: %struct.__va_list_tag = type { ptr, i32 }
// TINY: %struct.__va_list_tag = type { ptr, i32 }

int sizeof_va_list = sizeof(va_list);
// CHECK: @sizeof_va_list = global i32 8, align 1
// INT16: @sizeof_va_list = global i16 8, align 1
// TINY: @sizeof_va_list = global i32 6, align 1

int read_i(va_list ap) {
  return va_arg(ap, int);
}
// Downward-stepping stack load: advance __off by slot width (4B for default
// 32-bit int), negate, and load at base - off_next. No 6-slot cap or halt.
// CHECK-LABEL: define dso_local i32 @read_i
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i32 4
// CHECK: load i32, ptr %{{.*}}, align 1
// CHECK: load ptr, ptr %{{.*}}, align 1
// CHECK: %va.off.next = add i32 %va.off, 4
// CHECK: store i32 %va.off.next, ptr %va.offp, align 1
// CHECK: %va.negoff = sub i32 0, %va.off.next
// CHECK: %va.addr = getelementptr i8, ptr %va.base, i32 %va.negoff
// CHECK: load i32, ptr %va.addr, align 1
// CHECK-NOT: mcs251.vararg.halt
// CHECK-NOT: va_arg
// TINY-LABEL: define dso_local i32 @read_i
// TINY: getelementptr inbounds i8, ptr %{{.*}}, i32 2
// TINY: %va.off.next = add i32 %va.off, 4
// TINY: %va.negoff = sub i32 0, %va.off.next
// TINY: trunc i32 %va.negoff to i16
// TINY: getelementptr i8, ptr %va.base, i16
// TINY: load i32, ptr %va.addr, align 1
// INT16-LABEL: define dso_local i16 @read_i
// INT16: getelementptr inbounds i8, ptr %{{.*}}, i32 4
// INT16: %va.off.next = add i32 %va.off, 2
// INT16: %va.negoff = sub i32 0, %va.off.next
// INT16: load i16, ptr %va.addr, align 1

char read_c(va_list ap) { return (char)va_arg(ap, int); }
// CHECK-LABEL: define dso_local signext i8 @read_c
// CHECK: load i32, ptr %va.addr, align 1
// CHECK: trunc i32 %{{.*}} to i8
// CHECK-NOT: va_arg

short read_s(va_list ap) { return (short)va_arg(ap, int); }
// CHECK-LABEL: define dso_local signext i16 @read_s
// CHECK: load i32, ptr %va.addr, align 1
// CHECK: trunc i32 %{{.*}} to i16

float read_f(va_list ap) { return (float)va_arg(ap, double); }
// CHECK-LABEL: define dso_local float @read_f
// CHECK: load float, ptr %va.addr, align 1

char *read_p(va_list ap) { return va_arg(ap, char *); }
// CHECK-LABEL: define dso_local ptr @read_p
// CHECK: load ptr, ptr %va.addr, align 1

int read_char_direct(va_list ap) { return va_arg(ap, char); }
// CHECK-LABEL: define dso_local i32 @read_char_direct
// CHECK: %va.off.next = add i32 %va.off, 4
// CHECK-NEXT: store i32 %va.off.next, ptr %va.offp, align 1
// CHECK-NEXT: %va.negoff = sub i32 0, %va.off.next
// CHECK-NEXT: %va.addr = getelementptr i8, ptr %va.base, i32 %va.negoff
// CHECK-NEXT: %{{.*}} = load i32, ptr %va.addr, align 1
// CHECK-NEXT: trunc i32 %{{.*}} to i8
// CHECK-NEXT: store i8 %{{.*}}, ptr %va.arg.tmp, align 1
// CHECK: sext i8 %{{.*}} to i32
// CHECK-NOT: va_arg

int read_short_direct(va_list ap) { return va_arg(ap, short); }
// CHECK-LABEL: define dso_local i32 @read_short_direct
// CHECK: %va.off.next = add i32 %va.off, 4
// CHECK-NEXT: store i32 %va.off.next, ptr %va.offp, align 1
// CHECK-NEXT: %va.negoff = sub i32 0, %va.off.next
// CHECK-NEXT: %va.addr = getelementptr i8, ptr %va.base, i32 %va.negoff
// CHECK-NEXT: %{{.*}} = load i32, ptr %va.addr, align 1
// CHECK-NEXT: trunc i32 %{{.*}} to i16
// CHECK-NEXT: store i16 %{{.*}}, ptr %va.arg.tmp, align 1
// CHECK: sext i16 %{{.*}} to i32
// CHECK-NOT: va_arg

float read_float_direct(va_list ap) { return va_arg(ap, float); }
// CHECK-LABEL: define dso_local float @read_float_direct
// CHECK: %va.off.next = add i32 %va.off, 4
// CHECK-NEXT: store i32 %va.off.next, ptr %va.offp, align 1
// CHECK-NEXT: %va.negoff = sub i32 0, %va.off.next
// CHECK-NEXT: %va.addr = getelementptr i8, ptr %va.base, i32 %va.negoff
// CHECK-NEXT: %{{.*}} = load float, ptr %va.addr, align 1
// CHECK-NEXT: store float %{{.*}}, ptr %va.arg.tmp, align 1
// CHECK-NOT: va_arg

int sum3(int n, ...) {
  va_list ap, ap2;
  va_start(ap, n);
  va_copy(ap2, ap);
  int a = read_i(ap2);
  int b = va_arg(ap, int);
  va_end(ap);
  va_end(ap2);
  return a + b + n;
}
// CHECK-LABEL: define dso_local i32 @sum3(i32 noundef %{{.*}}, ...)
// CHECK: alloca [1 x %struct.__va_list_tag], align 1
// CHECK: alloca [1 x %struct.__va_list_tag], align 1
// CHECK: call addrspace(4) void @llvm.va_start.p0(ptr %{{.*}})
// CHECK: call addrspace(4) void @llvm.va_copy.p0(ptr %{{.*}}, ptr %{{.*}})
// CHECK: call addrspace(4) i32 @read_i(ptr noundef %{{.*}})
// CHECK: call addrspace(4) void @llvm.va_end.p0(ptr %{{.*}})
// CHECK: call addrspace(4) void @llvm.va_end.p0(ptr %{{.*}})
