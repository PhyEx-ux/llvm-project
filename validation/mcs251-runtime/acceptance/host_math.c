/*===-- host_math.c -------------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时验收 —— 宿主机 Oracle（数学函数部分）。
 * 输出格式与 math_firmware.c 逐字节一致。
 *
 * 两种模式（2026-09-09 Kazimi 重构）：
 *   默认        —— 链接运行时参考实现（mcs251_float_math.c 宿主编译），
 *                  输出与 DUT 逐字节对拍（同一算法双端执行一致性）。
 *   --glibc     —— 用 glibc libm 计算同名行，用于度量运行时的绝对
 *                  精度（ULP 距离），不用于逐字节对拍。
 *
 * 编译（默认）：cc -std=c11 -O2 -I<runtime-src> -I<acceptance>
 *               host_math.c <runtime-src>/mcs251_float_math.c
 *               <runtime-src>/mcs251_float_arith.c
 *               <runtime-src>/mcs251_float_cmp.c
 *               <runtime-src>/mcs251_bitutil.c -lm -o host_math
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "float_vectors.h"

/* 运行时参考实现（uint32 位模式 ABI） */
uint32_t _sinf(uint32_t x);
uint32_t _cosf(uint32_t x);
uint32_t _tanf(uint32_t x);
uint32_t _sqrtf(uint32_t x);
uint32_t _fabsf(uint32_t x);
uint32_t _floorf(uint32_t x);
uint32_t _ceilf(uint32_t x);
uint32_t _expf(uint32_t x);
uint32_t _logf(uint32_t x);
uint32_t _powf(uint32_t x, uint32_t y);

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void hex32(uint32_t v) { printf("%08X", v); }

static int use_glibc = 0;

static uint32_t m_sin(uint32_t x)  { return use_glibc ? f2u(sinf(u2f(x)))   : _sinf(x); }
static uint32_t m_cos(uint32_t x)  { return use_glibc ? f2u(cosf(u2f(x)))   : _cosf(x); }
static uint32_t m_tan(uint32_t x)  { return use_glibc ? f2u(tanf(u2f(x)))   : _tanf(x); }
static uint32_t m_sqrt(uint32_t x) { return use_glibc ? f2u(sqrtf(u2f(x)))  : _sqrtf(x); }
static uint32_t m_fabs(uint32_t x) { return use_glibc ? f2u(fabsf(u2f(x)))  : _fabsf(x); }
static uint32_t m_floor(uint32_t x){ return use_glibc ? f2u(floorf(u2f(x))) : _floorf(x); }
static uint32_t m_ceil(uint32_t x) { return use_glibc ? f2u(ceilf(u2f(x)))  : _ceilf(x); }
static uint32_t m_exp(uint32_t x)  { return use_glibc ? f2u(expf(u2f(x)))   : _expf(x); }
static uint32_t m_log(uint32_t x)  { return use_glibc ? f2u(logf(u2f(x)))   : _logf(x); }
static uint32_t m_pow(uint32_t x, uint32_t y) {
    return use_glibc ? f2u(powf(u2f(x), u2f(y))) : _powf(x, y);
}

int main(int argc, char **argv)
{
    uint32_t i;
    if (argc > 1 && strcmp(argv[1], "--glibc") == 0) use_glibc = 1;

    for (i = 0; i < NVEC_MATH; i++) {
        uint32_t x = vec_math[i];
        uint32_t r;
        r = m_sin(x);
        printf("SIN x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_cos(x);
        printf("COS x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_tan(x);
        printf("TAN x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_sqrt(x);
        printf("SQRT x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_fabs(x);
        printf("FABS x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_floor(x);
        printf("FLOOR x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_ceil(x);
        printf("CEIL x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_exp(x);
        printf("EXP x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_log(x);
        printf("LOG x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = m_pow(x, F32_POS_TWO);
        printf("POW x="); hex32(x); printf(" y="); hex32(F32_POS_TWO);
        printf(" r="); hex32(r); printf("\n");
    }
    printf("MATH-PASS\n");
    return 0;
}
