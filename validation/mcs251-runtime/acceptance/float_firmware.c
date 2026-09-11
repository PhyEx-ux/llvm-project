/*===-- float_firmware.c --------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 QEMU 语义验收固件。
 *
 * 测试路径：固件以 uint32_t 位模式调用软浮点 helper（_addsf3 等），
 * helper 由 mcs251-runtime 对象供给，结果经定宽十六进制序列化写 SBUF(0x99)。
 *
 * 行格式（与宿主 host_expect.c 逐字节一致）：
 *   ADD a=XXXXXXXX b=XXXXXXXX r=XXXXXXXX
 *   SUB / MUL / DIV / EQ / LT / SIN / COS / ... 同构
 *
 * 红线：不使用 variadic printf；全部输出经 uart_hex32；
 * 操作数经 volatile 拷贝进入运算，禁止常数折叠；
 * 每个发射函数 noinline 控制函数体量（rel8 限制）。
 */

#include <stdint.h>
#include "float_vectors.h"

/* ---- 软浮点 helper 外部声明（uint32_t ABI） ---- */
extern uint32_t _addsf3(uint32_t a, uint32_t b);
extern uint32_t _subsf3(uint32_t a, uint32_t b);
extern uint32_t _mulsf3(uint32_t a, uint32_t b);
extern uint32_t _divsf3(uint32_t a, uint32_t b);
extern uint32_t _negsf2(uint32_t a);
extern uint32_t _floatsisf(int32_t a);
extern uint32_t _fixsfsi(uint32_t a);
extern uint32_t _eqsf2(uint32_t a, uint32_t b);
extern uint32_t _ltsf2(uint32_t a, uint32_t b);
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

/* ---- 定宽串口输出 ---- */
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

/* ---- volatile 操作数槽 ---- */
static volatile uint32_t v_a, v_b;
static volatile uint32_t g_fail;

/* ---- 加法测试 ---- */
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

/* ---- 比较测试 ---- */
__attribute__((noinline)) static void em_cmp(uint32_t i)
{
    volatile uint32_t a = vec_cmp_a[i];
    volatile uint32_t b = vec_cmp_b[i];
    v_a = a; v_b = b;
    uint32_t eq = _eqsf2(v_a, v_b);
    uint32_t lt = _ltsf2(v_a, v_b);
    uart_puts("CMP a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
    uart_puts(" eq="); uart_hex32(eq); uart_puts(" lt="); uart_hex32(lt);
    uart_putc('\n');
}

/* ---- 数学函数测试（单值，直接调用避免函数指针 ABI 限制） ---- */
__attribute__((noinline)) static void em_sin(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _sinf(v_a);
    uart_puts("SIN x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_cos(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _cosf(v_a);
    uart_puts("COS x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_tan(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _tanf(v_a);
    uart_puts("TAN x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_sqrt(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _sqrtf(v_a);
    uart_puts("SQRT x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_fabs(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _fabsf(v_a);
    uart_puts("FABS x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_floor(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _floorf(v_a);
    uart_puts("FLOOR x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_ceil(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _ceilf(v_a);
    uart_puts("CEIL x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_exp(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _expf(v_a);
    uart_puts("EXP x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

__attribute__((noinline)) static void em_log(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _logf(v_a);
    uart_puts("LOG x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

/* ---- 转换测试 ---- */
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

/* ---- neg 测试 ---- */
__attribute__((noinline)) static void em_neg(uint32_t i)
{
    volatile uint32_t x = vec_math[i];
    v_a = x;
    uint32_t r = _negsf2(v_a);
    uart_puts("NEG x="); uart_hex32(x);
    uart_puts(" r="); uart_hex32(r); uart_putc('\n');
}

int main(void)
{
    uint32_t i;
    /* 无 RAM 清零启动（crt 不做 BSS 清扫）：所有全局先写后读 */
    g_fail = 0u;
    /* add/sub 测试 */
    for (i = 0u; i < NVEC_ADDSUB; i++) { em_add(i); em_sub(i); }
    /* mul 测试 */
    for (i = 0u; i < NVEC_MUL; i++) { em_mul(i); }
    /* div 测试 */
    for (i = 0u; i < NVEC_DIV; i++) { em_div(i); }
    /* 比较测试 */
    for (i = 0u; i < NVEC_CMP; i++) { em_cmp(i); }
    /* 数学函数 */
    for (i = 0u; i < NVEC_MATH; i++) {
        em_sin(i);
        em_cos(i);
        em_tan(i);
        em_sqrt(i);
        em_fabs(i);
        em_floor(i);
        em_ceil(i);
        em_exp(i);
        em_log(i);
        em_neg(i);
    }
    /* powf: x^2 */
    for (i = 0u; i < NVEC_MATH; i++) {
        volatile uint32_t x = vec_math[i];
        volatile uint32_t y = F32_POS_TWO;
        v_a = x; v_b = y;
        uint32_t r = _powf(v_a, v_b);
        uart_puts("POW x="); uart_hex32(x); uart_puts(" y="); uart_hex32(y);
        uart_puts(" r="); uart_hex32(r); uart_putc('\n');
    }
    /* 转换 */
    for (i = 0u; i < NVEC_I2F; i++) { em_i2f(i); }
    for (i = 0u; i < NVEC_F2I; i++) { em_f2i(i); }

    if (g_fail == 0u) {
        uart_puts("FLOAT-PASS\n");
    } else {
        uart_puts("FLOAT-FAIL\n");
    }
    for (;;) { }
}
