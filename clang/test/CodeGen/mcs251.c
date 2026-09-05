// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,INT32
// RUN: %clang_cc1 -triple mcs251-unknown-none -target-feature +int16 -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,INT16
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -emit-obj -o %t %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -target-feature +int16 -ffreestanding -O2 -emit-obj -o %t %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple mcs251-unknown-none -target-feature +int16 -ffreestanding -O2 -S -o - %s | FileCheck %s --check-prefix=ASM

// CHECK: target datalayout = "E-m:e-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8"
// CHECK: target triple = "mcs251-unknown-none"

#ifdef __MCS251_INT16__
#define MCS251_INT16 1
#else
#define MCS251_INT16 0
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

_Static_assert(sizeof(int) == (MCS251_INT16 ? 2 : 4), "C int model");
_Static_assert(sizeof(short) == 2, "16-bit short in both models");
_Static_assert(sizeof(long) == 4, "32-bit long in both models");
_Static_assert(sizeof(void *) == 4, "32-bit flat pointers");
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

// The established LLVM IR symbol ABI is unmangled until the separate naming
// migration. Explicit asm labels bridge to SDCC's physical leading underscore.
extern volatile u16 external_word __asm__("_external_word");
u16 read_word(void) __asm__("_read_word");
// CHECK-LABEL: define {{.*}}i16 @_read_word(
// CHECK: load volatile i16, ptr @_external_word, align 1
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
// ASM: .globl increment
u8 increment(u8 value) { return value + 3; }

// CHECK-LABEL: define {{.*}}i32 @read_pointer(ptr noundef
// CHECK: load i32, ptr {{.*}}, align 1
u32 read_pointer(const u32 *p) { return *p; }

// A result cast cannot hide the different intermediate integer promotions.
// CHECK-LABEL: define {{.*}}zeroext i8 @traditional_wrap(
// INT32: zext i16 {{.*}} to i32
// INT32: add nsw i32
// INT32: icmp eq i32
// INT16: add i16
// INT16: icmp eq i16
u8 traditional_wrap(u16 value) { return value + (u16)1 == 0; }
