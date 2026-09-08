/*===-- arith_firmware.c --------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时验收固件 #1 —— 运算 + 比较 + 转换。
 *
 * 链接 mcs251_float_arith.c.o + mcs251_float_cmp.c.o（不含数学函数）。
 * 输出格式与 host_expect.c 逐字节一致。
 */

#include <stdint.h>
#include "float_vectors.h"

extern uint32_t _addsf3(uint32_t a, uint32_t b);
extern uint32_t _subsf3(uint32_t a, uint32_t b);
extern uint32_t _mulsf3(uint32_t a, uint32_t b);
extern uint32_t _divsf3(uint32_t a, uint32_t b);
extern uint32_t _negsf2(uint32_t a);
extern uint32_t _floatsisf(int32_t a);
extern uint32_t _fixsfsi(uint32_t a);
extern uint32_t _eqsf2(uint32_t a, uint32_t b);
extern uint32_t _nesf2(uint32_t a, uint32_t b);
extern uint32_t _ltsf2(uint32_t a, uint32_t b);
extern uint32_t _gesf2(uint32_t a, uint32_t b);

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

__attribute__((noinline)) static void em_add(uint32_t i)
{
    volatile uint32_t a = vec_add_a[i];
    volatile uint32_t b = vec_add_b[i];
    v_a = a; v_b = b;
    uint32_t r = _addsf3(v_a, v_b);
    uart_puts("ADD a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_sub(uint32_t i)
{
    volatile uint32_t a = vec_add_a[i];
    volatile uint32_t b = vec_add_b[i];
    v_a = a; v_b = b;
    uint32_t r = _subsf3(v_a, v_b);
    uart_puts("SUB a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_mul(uint32_t i)
{
    volatile uint32_t a = vec_mul_a[i];
    volatile uint32_t b = vec_mul_b[i];
    v_a = a; v_b = b;
    uint32_t r = _mulsf3(v_a, v_b);
    uart_puts("MUL a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_div(uint32_t i)
{
    volatile uint32_t a = vec_div_a[i];
    volatile uint32_t b = vec_div_b[i];
    v_a = a; v_b = b;
    uint32_t r = _divsf3(v_a, v_b);
    uart_puts("DIV a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_cmp(uint32_t i)
{
    volatile uint32_t a = vec_cmp_a[i];
    volatile uint32_t b = vec_cmp_b[i];
    v_a = a; v_b = b;
    uint32_t eq = _eqsf2(v_a, v_b);
    uint32_t ne = _nesf2(v_a, v_b);
    uint32_t lt = _ltsf2(v_a, v_b);
    uint32_t ge = _gesf2(v_a, v_b);
    uart_puts("CMP a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
    uart_puts(" eq="); uart_hex32(eq); uart_puts(" ne="); uart_hex32(ne);
    uart_puts(" lt="); uart_hex32(lt); uart_puts(" ge="); uart_hex32(ge);
    uart_putc('\n');
}

__attribute__((noinline)) static void em_i2f(uint32_t i)
{
    volatile int32_t v = vec_i2f[i];
    uint32_t r = _floatsisf(v);
    uart_puts("I2F v="); uart_hex32((uint32_t)v);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_f2i(uint32_t i)
{
    volatile uint32_t v = vec_f2i[i];
    uint32_t r = _fixsfsi(v);
    uart_puts("F2I v="); uart_hex32(v);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_neg(uint32_t i)
{
    volatile uint32_t x = vec_neg_data[i];
    v_a = x;
    uint32_t r = _negsf2(v_a);
    uart_puts("NEG x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

int main(void)
{
    uint32_t i;
    for (i = 0u; i < NVEC_ADDSUB; i++) { em_add(i); em_sub(i); }
    for (i = 0u; i < NVEC_MUL; i++) { em_mul(i); }
    for (i = 0u; i < NVEC_DIV; i++) { em_div(i); }
    for (i = 0u; i < NVEC_CMP; i++) { em_cmp(i); }
    for (i = 0u; i < NVEC_I2F; i++) { em_i2f(i); }
    for (i = 0u; i < NVEC_F2I; i++) { em_f2i(i); }
    for (i = 0u; i < NVEC_NEG; i++) { em_neg(i); }
    uart_puts("ARITH-PASS\n");
    for (;;) { }
}
