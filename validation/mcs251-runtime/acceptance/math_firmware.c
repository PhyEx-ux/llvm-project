/*===-- math_firmware.c ---------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时验收固件 #2 —— 数学函数。
 *
 * 链接 mcs251_float_math.c.o + mcs251_float_arith.c.o（powf 依赖 mulsf3/logf/expf）。
 * 输出格式与 host_expect.c 逐字节一致。
 */

#include <stdint.h>
#include "float_vectors.h"

extern uint32_t _sinf(uint32_t x);
extern uint32_t _cosf(uint32_t x);
extern uint32_t _tanf(uint32_t x);
extern uint32_t _sqrtf(uint32_t x);
extern uint32_t _fabsf(uint32_t x);
extern uint32_t _floorf(uint32_t x);
extern uint32_t _ceilf(uint32_t x);
extern uint32_t _expf(uint32_t x);
extern uint32_t _logf(uint32_t x);
extern uint32_t _powf(uint32_t x, uint32_t y);

#define SBUF (*(volatile uint8_t *)0x99)

__attribute__((noinline)) static void uart_putc(char c) { SBUF = (uint8_t)c; }
__attribute__((noinline)) static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

__attribute__((noinline)) static void uart_hex8(uint8_t v)
{
    uint8_t hi = (uint8_t)(v >> 4) & 0x0Fu;
    uint8_t lo = v & 0x0Fu;
    uart_putc((char)(hi < 10 ? '0' + hi : 'A' + hi - 10));
    uart_putc((char)(lo < 10 ? '0' + lo : 'A' + lo - 10));
}

__attribute__((noinline)) static void uart_hex16(uint16_t v)
{
    uart_hex8((uint8_t)(v >> 8));
    uart_hex8((uint8_t)v);
}

__attribute__((noinline)) static void uart_hex32(uint32_t v)
{
    uart_hex16((uint16_t)(v >> 16));
    uart_hex16((uint16_t)v);
}

static volatile uint32_t v_a, v_b;

#define EM_MATH(TAG, FN) \
__attribute__((noinline)) static void em_##TAG(uint32_t i) \
{ \
    volatile uint32_t x = vec_math[i]; \
    v_a = x; \
    uint32_t r = FN(v_a); \
    uart_puts(#TAG " x="); uart_hex32(x); \
    uart_puts(" r="); uart_hex32(r); uart_putc('\n'); \
}

EM_MATH(SIN, _sinf)
EM_MATH(COS, _cosf)
EM_MATH(TAN, _tanf)
EM_MATH(SQRT, _sqrtf)
EM_MATH(FABS, _fabsf)
EM_MATH(FLOOR, _floorf)
EM_MATH(CEIL, _ceilf)
EM_MATH(EXP, _expf)
EM_MATH(LOG, _logf)

__attribute__((noinline)) static void em_pow(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    volatile uint32_t y = F32_POS_TWO;
    v_a = x; v_b = y;
    uint32_t r = _powf(v_a, v_b);
    uart_puts("POW x="); uart_hex32(x); uart_puts(" y="); uart_hex32(y);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

int main(void)
{
    uint32_t i;
    for (i = 0u; i < NVEC_MATH; i++) {
        em_SIN(i);
        em_COS(i);
        em_TAN(i);
        em_SQRT(i);
        em_FABS(i);
        em_FLOOR(i);
        em_CEIL(i);
        em_EXP(i);
        em_LOG(i);
        em_pow(i);
    }
    uart_puts("MATH-PASS\n");
    for (;;) { }
}
