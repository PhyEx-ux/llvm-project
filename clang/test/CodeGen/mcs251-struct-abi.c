// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O1 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 \
// RUN:   -mllvm -verify-machineinstrs -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O0 \
// RUN:   -mllvm -verify-machineinstrs -mllvm -mcs251-object-format=elf \
// RUN:   -emit-obj %s -o %t.o0.o
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 \
// RUN:   -mllvm -verify-machineinstrs -mllvm -mcs251-object-format=elf \
// RUN:   -emit-obj %s -o %t.o2.o

#include <stdarg.h>
#include <stdint.h>

struct S1 { uint8_t a; };
struct S2 { uint8_t a; uint8_t b; };
struct S3 { uint8_t a; uint8_t b; uint8_t c; };
struct S4 { uint16_t a; uint16_t b; };
struct S12 { uint32_t x; uint32_t y; uint32_t z; };
struct S24 { uint8_t data[24]; };
union U4 { uint32_t u; uint8_t b[4]; };

// IR-LABEL: define dso_local i8 @ret_s1(ptr {{.*}}byval(%struct.S1) align 1
// ASM-LABEL: _ret_s1:
// ASM: mov r0, @dr60-0x0003
// ASM: mov dpl, r0
// ASM: eret
struct S1 ret_s1(struct S1 s) {
  s.a += 1;
  return s;
}

// IR-LABEL: define dso_local i16 @ret_s2(ptr {{.*}}byval(%struct.S2) align 1
// ASM-LABEL: _ret_s2:
// ASM: mov dpl,
// ASM: mov dph,
// ASM: eret
struct S2 ret_s2(struct S2 s) {
  s.b += s.a;
  return s;
}

// 3-byte struct uses sret hidden pointer in Arg 0 (DPL:DPH:B).
// IR-LABEL: define dso_local void @ret_s3(ptr {{.*}}sret(%struct.S3) align 1 {{.*}}, ptr {{.*}}byval(%struct.S3) align 1
// ASM-LABEL: _ret_s3:
// ASM: mov r1, b
// ASM: mov r2, dph
// ASM: mov r3, dpl
// ASM: eret
struct S3 ret_s3(struct S3 s) {
  s.c += s.a + s.b;
  return s;
}

// 4-byte struct and union return directly in DPL:DPH:B:A.
// IR-LABEL: define dso_local i32 @ret_s4(ptr {{.*}}byval(%struct.S4) align 1
// ASM-LABEL: _ret_s4:
// ASM: mov dpl,
// ASM: mov dph,
// ASM: mov b,
// ASM: mov a,
// ASM: eret
struct S4 ret_s4(struct S4 s) {
  s.b += s.a;
  return s;
}

// IR-LABEL: define dso_local i32 @ret_u4(ptr {{.*}}byval(%union.U4) align 1
union U4 ret_u4(union U4 u) {
  u.b[0] ^= 0x55;
  return u;
}

// 12-byte struct uses sret + byval; <= 16B copy unrolls inline without _memcpy.
// IR-LABEL: define dso_local void @ret_s12(ptr {{.*}}sret(%struct.S12) align 1 {{.*}}, ptr {{.*}}byval(%struct.S12) align 1
// ASM-LABEL: _ret_s12:
// ASM-NOT: ecall _memcpy
// ASM: eret
struct S12 ret_s12(struct S12 s) {
  s.z = s.x + s.y;
  return s;
}

// 24-byte struct uses sret + byval; > 16B copy emits _memcpy libcall.
// IR-LABEL: define dso_local void @ret_s24(ptr {{.*}}sret(%struct.S24) align 1 {{.*}}, ptr {{.*}}byval(%struct.S24) align 1
// ASM-LABEL: _ret_s24:
// ASM: ecall _memcpy
// ASM: eret
struct S24 ret_s24(struct S24 s) {
  s.data[23] = s.data[0] + s.data[22];
  return s;
}

// Calling an external function with >16B byval emits inline BYVAL_COPY32 loop.
extern uint8_t consume_s24(struct S24 s);
// ASM-LABEL: _pass_s24:
// ASM: inc spx, #0x4
// ASM: mov {{r[0-9]+}}, 0x81
// ASM: mov {{r[0-9]+}}, 0x85
// ASM: mov @{{wr[0-9]+}}, {{r[0-9]+}}
// ASM: sub {{wr[0-9]+}}, #0x0001
// ASM: jne
// ASM: ecall _consume_s24
// ASM: dec spx, #0x4
// ASM: eret
uint8_t pass_s24(const struct S24 *p) {
  return consume_s24(*p);
}

// Variadic struct passing and va_arg(ap, struct S).
// IR-LABEL: define dso_local i32 @vararg_structs(i32 {{.*}}, ...)
// IR: call addrspace(4) void @llvm.va_start
// IR: getelementptr i8, ptr
// ASM-LABEL: _vararg_structs:
// ASM-NOT: _PARM_
// ASM: eret
int vararg_structs(int tag, ...) {
  va_list ap;
  va_start(ap, tag);
  struct S3 s3 = va_arg(ap, struct S3);
  struct S12 s12 = va_arg(ap, struct S12);
  va_end(ap);
  return tag + s3.a + s3.b + s3.c + (int)s12.z;
}

extern int sink_vararg_structs(int tag, ...);
// ASM-LABEL: _call_vararg_structs:
// ASM: ecall _sink_vararg_structs
// ASM: eret
int call_vararg_structs(const struct S3 *p3, const struct S12 *p12) {
  return sink_vararg_structs(7, *p3, *p12);
}
