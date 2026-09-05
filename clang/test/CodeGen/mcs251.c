// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -emit-obj -o %t %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -S -o - %s | FileCheck %s --check-prefix=ASM

// CHECK: target datalayout = "E-m:e-p:32:8-i8:8-i16:8-i32:8-n8:16:32-S8"
// CHECK: target triple = "mcs251-unknown-none"

typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

_Static_assert(sizeof(int) == 2, "provisional 16-bit int model");
_Static_assert(sizeof(long) == 4, "32-bit long");
_Static_assert(sizeof(void *) == 4, "32-bit flat pointers");
_Static_assert(_Alignof(int) == 1, "byte-aligned int");
_Static_assert(_Alignof(long) == 1, "byte-aligned long");
_Static_assert(_Alignof(void *) == 1, "byte-aligned pointers");
_Static_assert(sizeof(__SIZE_TYPE__) == 4, "size_t spans the pointer domain");
_Static_assert(sizeof(__PTRDIFF_TYPE__) == 4, "ptrdiff_t spans pointer offsets");
_Static_assert(sizeof(__CHAR32_TYPE__) == 4, "char32_t is not unsigned int");
_Static_assert(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__, "big endian");

// The established LLVM IR symbol ABI is unmangled (DataLayout m:e).
// Explicit asm labels bridge ordinary C names to SDCC's leading underscore.
extern volatile u16 external_word __asm__("_external_word");
u16 read_word(void) __asm__("_read_word");
// CHECK-LABEL: define {{.*}}i16 @_read_word(
// CHECK: load volatile i16, ptr @_external_word, align 1
// ASM: .globl _read_word
// ASM: _read_word:
u16 read_word(void) { return external_word; }

// CHECK-LABEL: define {{.*}}zeroext i8 @increment(i8 noundef zeroext
// CHECK: zext i8 {{.*}} to i16
// CHECK: add nsw i16
// CHECK: trunc i16 {{.*}} to i8
// ASM: .globl increment
u8 increment(u8 value) { return value + 3; }

// CHECK-LABEL: define {{.*}}i32 @read_pointer(ptr noundef
// CHECK: load i32, ptr {{.*}}, align 1
u32 read_pointer(const u32 *p) { return *p; }
