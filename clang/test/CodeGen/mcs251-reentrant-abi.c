// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c23 -fmcs251-keil -ffreestanding \
// RUN:   -Wno-varargs -O2 -mllvm -verify-machineinstrs -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=ASM32
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c23 -fmcs251-keil -ffreestanding \
// RUN:   -Wno-varargs -O2 -mllvm -verify-machineinstrs \
// RUN:   -mllvm -mcs251-object-format=elf -emit-obj %s -o %t.32.o
// RUN: %clang_cc1 -triple mcs251-unknown-none -target-feature +int16 \
// RUN:   -std=c23 -fmcs251-keil -ffreestanding -Wno-varargs -O2 \
// RUN:   -mllvm -verify-machineinstrs -mllvm -mcs251-object-format=elf \
// RUN:   -emit-obj %s -o %t.int16.o
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,2,16,1,1 \
// RUN:   -std=c23 -fmcs251-keil -ffreestanding -Wno-varargs -O2 \
// RUN:   -mllvm -verify-machineinstrs -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=ASM16

#include <stdarg.h>
#include <stdint.h>

// No static parameter slots or OSEG areas emitted anywhere.
// ASM32-NOT: _PARM_
// ASM32-NOT: OSEG
// ASM16-NOT: _PARM_
// ASM16-NOT: OSEG

// __reentrant and Keil bare `reentrant` suffixes accepted; multi-arg recursion
// uses stack slots per frame.
// ASM32-LABEL: _reentrant_sum:
// ASM32: ecall _reentrant_sum
// ASM32: eret
int reentrant_sum(int n, int acc, int step) __reentrant {
  if (n <= 1)
    return acc + step;
  return reentrant_sum(n - 1, acc + step, step) +
         reentrant_sum(n - 2, acc, step + 1);
}

// ASM32-LABEL: _keil_reent_fn:
// ASM32: mov {{r[0-9]+}}, @dr60-0x0006
// ASM32: eret
int keil_reent_fn(int a, int b) reentrant {
  return a + b;
}

// Multi-argument indirect calls (scalars and __bit).
// ASM32-LABEL: _call_indirect_multi:
// ASM32: inc spx,
// ASM32: ecall @dr
// ASM32: dec spx,
// ASM32: ecall @dr
// ASM32: eret
int call_indirect_multi(int (*fp)(int, int, int),
                        __bit (*bfp)(__bit, __bit, int),
                        __bit x, __bit y) {
  int r = fp(1, 2, 3);
  return r + (bfp(x, y, r) ? 1 : 0);
}

// Variadic function with > 6 arguments (10 variadic ints) and indirect variadic call.
// ASM32-LABEL: _sum_many:
// ASM32: mov {{r[0-9]+}}, 0x81
// ASM32: mov {{r[0-9]+}}, 0x85
// ASM32: eret
// ASM16-LABEL: _sum_many:
// ASM16: mov {{r[0-9]+}}, 0x81
// ASM16: mov {{r[0-9]+}}, 0x85
// ASM16: eret
int sum_many(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int total = 0;
  for (int i = 0; i < n; ++i)
    total += va_arg(ap, int);
  va_end(ap);
  return total;
}

// ASM32-LABEL: _call_ten_and_indirect_vararg:
// ASM32: ecall _sum_many
// ASM32: ecall @dr
// ASM32: eret
int call_ten_and_indirect_vararg(int (*vfp)(int, ...)) {
  int a = sum_many(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
  int b = vfp(8, 10, 20, 30, 40, 50, 60, 70, 80);
  return a + b;
}

// C23 zero-fixed-parameter variadic function (F = 0) and va_arg(ap, __bit).
// ASM32-LABEL: _zero_fixed_vararg:
// ASM32: mov {{r[0-9]+}}, 0x81
// ASM32: mov {{r[0-9]+}}, 0x85
// ASM32: eret
// ASM16-LABEL: _zero_fixed_vararg:
// ASM16: mov {{r[0-9]+}}, 0x81
// ASM16: mov {{r[0-9]+}}, 0x85
// ASM16: eret
int zero_fixed_vararg(...) {
  va_list ap;
  va_start(ap);
  int a = va_arg(ap, int);
  __bit flag = va_arg(ap, __bit);
  va_end(ap);
  return flag ? a : -a;
}

// ASM32-LABEL: _call_zero_fixed:
// ASM32: inc spx, #0x4
// ASM32: inc spx, #0x4
// ASM32: ecall _zero_fixed_vararg
// ASM32: dec spx, #0x4
// ASM32: dec spx, #0x4
// ASM32: eret
int call_zero_fixed(void) {
  return zero_fixed_vararg(42, 3);
}
