// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -Wno-varargs -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -target-feature +int16 -ffreestanding -Wno-varargs -emit-llvm -o - %s | FileCheck %s --check-prefix=INT16
// RUN: not %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,1,1 -ffreestanding -Wno-varargs -emit-llvm -o - %s 2>&1 | FileCheck %s --check-prefix=TINY

// G2 B-S2 (G2-VARIADIC-DESIGN-draft.md R3 §4.3.3-§4.3.5): __builtin_va_list
// is the 8-byte {__base, __off} pair (single-element array wrapper, the
// SystemZ form); va_start/va_end/va_copy keep their intrinsics (the backend
// LowerVASTART/VAEND/VACOPY hooks own them); va_arg is lowered HERE by
// MCS251ABIInfo::EmitVAArg into a guard branch + halt arm + 4-byte-slot
// load + one-slot advance -- never an llvm.va_arg intrinsic.  The halt arm
// carries the identity of the deterministic `sjmp .` stop as the
// llvm.mcs251.vararg.halt intrinsic.
//
// Review-fix coverage (Alice blockers 1-2):
//  * the direct narrow-branch assertions below use `va_arg(ap, char/short/
//    float)` -- the promoted-slot read (load i32 + trunc) comes from
//    EmitVAArg itself and is checked step by step, NOT masked by a result
//    cast as the `(char)va_arg(ap, int)` forms above it;
//  * INT16 pins the offset-width fix: under the +int16 C model the pair is
//    STILL the frozen {4-byte base, 4-byte i32 offset} = 8 bytes (the
//    offset field is pinned to the architecturally-32-bit unsigned long at
//    construction; the old `unsigned int` spelling silently built a 6-byte
//    {ptr, i16} object whose fixed byte-4 4-byte accesses ran past its
//    end);
//  * TINY pins the use-point fail-closed: with a 16-bit data pointer the
//    base field degenerates, no frozen pair exists, and the first va_arg
//    stops with the frozen backend error instead of silently reading past
//    the object end.

#include <stdarg.h>

// CHECK: %struct.__va_list_tag = type { ptr, i32 }
// INT16: %struct.__va_list_tag = type { ptr, i32 }

int sizeof_va_list = sizeof(va_list);
// The pair is exactly 8 bytes (target data layout is fully 1-byte
// aligned: __base@0, __off@4, no padding).
// CHECK: @sizeof_va_list = global i32 8, align 1
// INT16: @sizeof_va_list = global i16 8, align 1
// TINY: fatal error: error in backend: MCS251: va_list must be the frozen 8-byte {4-byte base, 4-byte offset} pair with __off at byte 4

int read_i(va_list ap) {
  return va_arg(ap, int);
}
// Guard: 6 slots x 4B, the sixth (byte offset 20) is the last legal one.
// CHECK: define dso_local i32 @read_i
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i32 4
// CHECK: load i32, ptr %{{.*}}, align 1
// CHECK: icmp ule i32 %{{.*}}, 20
// CHECK: br i1 %{{.*}}, label
// Halt arm: intrinsic + unreachable; the arm never rejoins.
// CHECK: call addrspace(4) void @llvm.mcs251.vararg.halt()
// CHECK: unreachable
// Load arm: base from the pair, base+off addressing, 4B slot read,
// advance by exactly one slot.
// CHECK: load ptr, ptr %{{.*}}, align 1
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i32 %{{.*}}
// CHECK: load i32, ptr %{{.*}}, align 1
// CHECK: add i32 %{{.*}}, 4
// CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 1
// No va_arg intrinsic anywhere in this TU:
// CHECK-NOT: va_arg

char read_c(va_list ap) { return (char)va_arg(ap, int); }
// Narrow read: the slot holds the promoted i32; truncating back is the
// inverse of the default argument promotion.
// CHECK: define dso_local signext i8 @read_c
// CHECK: load i32, ptr %{{.*}}, align 1
// CHECK: trunc i32 %{{.*}} to i8
// CHECK-NOT: va_arg

short read_s(va_list ap) { return (short)va_arg(ap, int); }
// CHECK: define dso_local signext i16 @read_s
// CHECK: load i32, ptr %{{.*}}, align 1
// CHECK: trunc i32 %{{.*}} to i16

float read_f(va_list ap) { return (float)va_arg(ap, double); }
// f32 reads the slot's bit pattern directly (double == f32 on MCS251).
// CHECK: define dso_local float @read_f
// CHECK: load float, ptr %{{.*}}, align 1

char *read_p(va_list ap) { return va_arg(ap, char *); }
// A pointer actual sits in the same 4B slot (the caller canonicalised the
// A byte on the store side).
// CHECK: define dso_local ptr @read_p
// CHECK: load ptr, ptr %{{.*}}, align 1

// ---------------------------------------------------------------------------
// Direct narrow-branch assertions (review blocker 2): `va_arg(ap, char)` /
// `va_arg(ap, short)` / `va_arg(ap, float)` exercise EmitVAArg's own
// promoted-slot path.  The trunc below consumes the SLOT's i32 load and
// the advance is still exactly 4 bytes -- the subsequent return promotion
// (sext) is a separate, later instruction, so a result cast can no longer
// masquerade as the narrow read.
// ---------------------------------------------------------------------------
int read_char_direct(va_list ap) { return va_arg(ap, char); }
// CHECK: define dso_local i32 @read_char_direct
// The SLOT address computation, then the promoted 4-byte read...
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i32 %{{.*}}
// CHECK-NEXT: load i32, ptr %{{.*}}, align 1
// ...truncated by EmitVAArg, with the same one-slot advance...
// CHECK-NEXT: trunc i32 %{{.*}} to i8
// CHECK-NEXT: add i32 %{{.*}}, 4
// CHECK-NEXT: store i32 %{{.*}}, ptr %{{.*}}, align 1
// ...and only then the temp + return promotion (sext), so the trunc is
// provably the narrow read, not a result cast.
// CHECK-NEXT: store i8 %{{.*}}, ptr %{{.*}}, align 1
// CHECK: sext i8 %{{.*}} to i32
// CHECK-NOT: va_arg

int read_short_direct(va_list ap) { return va_arg(ap, short); }
// CHECK: define dso_local i32 @read_short_direct
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i32 %{{.*}}
// CHECK-NEXT: load i32, ptr %{{.*}}, align 1
// CHECK-NEXT: trunc i32 %{{.*}} to i16
// CHECK-NEXT: add i32 %{{.*}}, 4
// CHECK-NEXT: store i32 %{{.*}}, ptr %{{.*}}, align 1
// CHECK-NEXT: store i16 %{{.*}}, ptr %{{.*}}, align 1
// CHECK: sext i16 %{{.*}} to i32
// CHECK-NOT: va_arg

float read_float_direct(va_list ap) { return va_arg(ap, float); }
// f32 is not promoted on this target (double == f32), so the direct form
// is the ordinary slot read: float load, then the same 4-byte advance.
// CHECK: define dso_local float @read_float_direct
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i32 %{{.*}}
// CHECK-NEXT: load float, ptr %{{.*}}, align 1
// CHECK-NEXT: add i32 %{{.*}}, 4
// CHECK-NEXT: store i32 %{{.*}}, ptr %{{.*}}, align 1
// CHECK-NEXT: store float %{{.*}}, ptr %{{.*}}, align 1
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
// va_list objects are 8-byte pair arrays; a forwarded va_list reaches the
// helper as a plain pointer to the owner's pair.
// CHECK: define dso_local i32 @sum3(i32 noundef %{{.*}}, ...)
// CHECK: alloca [1 x %struct.__va_list_tag], align 1
// CHECK: alloca [1 x %struct.__va_list_tag], align 1
// CHECK: call addrspace(4) void @llvm.va_start.p0(ptr %{{.*}})
// CHECK: call addrspace(4) void @llvm.va_copy.p0(ptr %{{.*}}, ptr %{{.*}})
// CHECK: call addrspace(4) i32 @read_i(ptr noundef %{{.*}})
// CHECK: call addrspace(4) void @llvm.va_end.p0(ptr %{{.*}})
// CHECK: call addrspace(4) void @llvm.va_end.p0(ptr %{{.*}})
