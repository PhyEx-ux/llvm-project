// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,DEFAULT,INT32
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,COMPAT,INT32
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,1,1 -DMCS251_NEAR=1 -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,TINY,INT32
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,8,1 -DMCS251_NEAR=1 -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,TINY,INT32
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -target-feature +int16 -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,COMPAT,INT16
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -O2 -emit-obj -o %t %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -target-feature +int16 -ffreestanding -O2 -emit-obj -o %t %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -target-feature +int16 -ffreestanding -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -emit-llvm -o %t.compat.ll %s
// WP4 C1: an effective cc1-side debug request is rejected (the object
// writers emit no source-level debug info). The fixture above no longer
// uses -debug-info-kind=limited; the rejection is pinned here instead.
// RUN: not %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -debug-info-kind=limited -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MCS251-DEBUG-REJECT
// RUN: not %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,32,8,1 -fdynamic-debugging -emit-llvm -o - -x ir %t.compat.ll 2>&1 | FileCheck %s --check-prefix=DYNDBG-CONFLICT
// RUN: %clang_cc1 -triple mcs251-unknown-none -round-trip-args -Rround-trip-cc1-args -ffreestanding -fsyntax-only -x c /dev/null 2>&1 | FileCheck %s --check-prefix=MEMORY-CC1-DEFAULT --implicit-check-not=error:
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,8,1 -round-trip-args -Rround-trip-cc1-args -ffreestanding -fsyntax-only -x c /dev/null 2>&1 | FileCheck %s --check-prefix=MEMORY-ROUNDTRIP-ON --implicit-check-not=error:
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,8,1 -no-round-trip-args -Rround-trip-cc1-args -ffreestanding -fsyntax-only -x c /dev/null
// RUN: not %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,3,32,8,1 -ffreestanding -fsyntax-only -x c /dev/null 2>&1 | FileCheck %s --check-prefix=MEMORY-INVALID
// RUN: not %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,3,1 -ffreestanding -fsyntax-only -x c /dev/null 2>&1 | FileCheck %s --check-prefix=MEMORY-INVALID-NEAR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -round-trip-args -Rround-trip-cc1-args -E -o /dev/null -x c /dev/null 2>&1 | FileCheck %s --check-prefix=NON-MCS251 --implicit-check-not=mcs251-memory-contract --implicit-check-not=error:
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -mcs251-memory-contract=1,2,32,8,1 -fsyntax-only -x c /dev/null 2>&1 | FileCheck %s --check-prefix=NON-MCS251-EXPLICIT

// DEFAULT: target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
// TINY: target datalayout = "E-m:s-p:16:8:8:16-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
// COMPAT: target datalayout = "E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8"
// CHECK: target triple = "mcs251-unknown-none"
// MCS251-DEBUG-REJECT: error: debug information is not supported for target 'mcs251'
// DYNDBG-CONFLICT: error: backend data layout '{{.*}}-P4-A0-G0' does not match expected target description 'E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8'
// DYNDBG-CONFLICT-NOT: error: backend data layout
// DYNDBG-CONFLICT-NOT: Assertion
// DYNDBG-CONFLICT-NOT: Expected emitAssembly to fill UnoptBuf
// DYNDBG-CONFLICT-NOT: @llvm.embedded.object
// DYNDBG-CONFLICT-NOT: target datalayout
// MEMORY-CC1-DEFAULT-COUNT-2: "-mcs251-memory-contract=1,2,32,8,1"
// MEMORY-ROUNDTRIP-ON-COUNT-2: "-mcs251-memory-contract=1,2,16,8,1"
// MEMORY-INVALID: error: invalid value '1,3,32,8,1' in '-mcs251-memory-contract='
// MEMORY-INVALID-NEAR: error: invalid value '1,2,16,3,1' in '-mcs251-memory-contract='
// NON-MCS251: remark: generated arguments #1 in round-trip:
// NON-MCS251-EXPLICIT: error: unsupported option '-mcs251-memory-contract=' for target 'x86_64-unknown-linux-gnu'

#ifdef __MCS251_INT16__
#define MCS251_INT16 1
#else
#define MCS251_INT16 0
#endif
#ifndef MCS251_NEAR
#define MCS251_NEAR 0
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

_Static_assert(sizeof(int) == (MCS251_INT16 ? 2 : 4), "C int model");
_Static_assert(sizeof(short) == 2, "16-bit short in both models");
_Static_assert(sizeof(long) == 4, "32-bit long in both models");
_Static_assert(sizeof(void *) == (MCS251_NEAR ? 2 : 4),
               "contract-selected default pointer width");
_Static_assert(sizeof(__INTMAX_TYPE__) == 8, "64-bit intmax_t");
_Static_assert(sizeof(__SIZE_TYPE__) == 4, "32-bit size_t");
_Static_assert(sizeof(__PTRDIFF_TYPE__) == 4, "32-bit ptrdiff_t");
_Static_assert(sizeof(__CHAR32_TYPE__) == 4, "32-bit char32_t");
_Static_assert(_Alignof(short) == 1 && _Alignof(int) == 1 &&
                   _Alignof(long) == 1 && _Alignof(long long) == 1 &&
                   _Alignof(float) == 1 && _Alignof(double) == 1 &&
                   _Alignof(long double) == 1 && _Alignof(void *) == 1,
               "byte-aligned C scalar model");
_Static_assert(sizeof((u16)0 + (u16)0) == sizeof(int), "integer promotion");
_Static_assert(((u16)65535 + (u16)1 == 0) == MCS251_INT16,
               "traditional unsigned-16 promotion wraps; ILP32 does not");
_Static_assert(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__, "big endian");

// Ordinary C names stay undecorated in IR; the LLVM mangler adds the physical
// underscore for both ASxxxx assembly and REL output.
extern volatile u16 external_word;
// CHECK-LABEL: define {{.*}}i16 @read_word(
// CHECK: load volatile i16, ptr @external_word, align 1
// ASM: .globl _read_word
// ASM: _read_word:
u16 read_word(void) { return external_word; }

// CHECK-LABEL: define {{.*}}zeroext i8 @increment(i8 noundef zeroext
// INT32: zext i8 {{.*}} to i32
// INT32: add nsw i32
// INT32: trunc i32 {{.*}} to i8
// INT16: zext i8 {{.*}} to i16
// INT16: add nsw i16
// INT16: trunc i16 {{.*}} to i8
// ASM: .globl _increment
u8 increment(u8 value) { return value + 3; }

// CHECK-LABEL: define {{.*}}i32 @read_pointer(ptr noundef
// CHECK: load i32, ptr {{.*}}, align 1
u32 read_pointer(const u32 *p) { return *p; }

// The v2 Tiny layout uses a 16-bit pointer index independently of the 32-bit
// size_t/ptrdiff_t language types.
// TINY-LABEL: define {{.*}}ptr @bump_pointer(
// TINY: getelementptr inbounds i8, ptr {{.*}}, i16 1
void *bump_pointer(void *p) { return (u8 *)p + 1; }

// TINY-LABEL: define {{.*}}zeroext i8 @local_roundtrip(
// TINY: alloca i8, align 1
// TINY: store volatile i8
// TINY: load volatile i8
u8 local_roundtrip(u8 value) {
  volatile u8 local = value;
  return local;
}

#if MCS251_NEAR
// ptrdiff_t stays 32 bits even when AS0 pointers are 16 bits. Form the complete
// mathematical address difference in i32; never subtract modulo 2^16 first.
// TINY-LABEL: define {{.*}}i32 @pointer_delta(
// TINY: ptrtoint ptr {{.*}} to i32
// TINY: ptrtoint ptr {{.*}} to i32
// TINY: sub i32
// TINY-NOT: sub i16
long pointer_delta(const u8 *lhs, const u8 *rhs) { return lhs - rhs; }
#endif

// A result cast cannot hide the different intermediate integer promotions.
// CHECK-LABEL: define {{.*}}zeroext i8 @traditional_wrap(
// INT32: zext i16 {{.*}} to i32
// INT32: add nsw i32
// INT32: icmp eq i32
// INT16: add i16
// INT16: icmp eq i16
u8 traditional_wrap(u16 value) { return value + (u16)1 == 0; }
