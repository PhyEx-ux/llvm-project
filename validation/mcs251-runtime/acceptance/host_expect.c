/*===-- host_expect.c -----------------------------------------------------===*/
/*
 * MCS251 软浮点运行时验收 —— 宿主机 Oracle-A（x86 IEEE-754 参考实现）。
 *
 * 逐字镜像目标固件 float_firmware.c 的输出格式：
 *   ADD a=XXXXXXXX b=XXXXXXXX r=XXXXXXXX
 *   SUB / MUL / DIV / CMP / SIN / COS / ... 同构
 *
 * 宿主机用 x86 float（32 位 IEEE-754）计算参考值，
 * 结果以 uint32_t 位模式十六进制输出，与 DUT 串口逐字节对拍。
 *
 * 编译：cc -std=c11 -O2 host_expect.c -o host_expect -lm
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* float <-> uint32 bit cast */
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static void hex32(uint32_t v) { printf("%08X", v); }

#include "float_vectors.h"

/* 运行时参考实现的数学函数（mcs251_float_math.c 宿主编译供给；
 * 见下方 math 段注释） */
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

int main(void)
{
    uint32_t i;

    /* add/sub */
    for (i = 0; i < NVEC_ADDSUB; i++) {
        uint32_t a = vec_add_a[i], b = vec_add_b[i];
        uint32_t r = f2u(u2f(a) + u2f(b));
        printf("ADD a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
        r = f2u(u2f(a) - u2f(b));
        printf("SUB a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
    }

    /* mul */
    for (i = 0; i < NVEC_MUL; i++) {
        uint32_t a = vec_mul_a[i], b = vec_mul_b[i];
        uint32_t r = f2u(u2f(a) * u2f(b));
        printf("MUL a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
    }

    /* div */
    for (i = 0; i < NVEC_DIV; i++) {
        uint32_t a = vec_div_a[i], b = vec_div_b[i];
        uint32_t r = f2u(u2f(a) / u2f(b));
        printf("DIV a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
    }

    /* compare */
    for (i = 0; i < NVEC_CMP; i++) {
        uint32_t a = vec_cmp_a[i], b = vec_cmp_b[i];
        float fa = u2f(a), fb = u2f(b);
        /* eq: 0 if equal, 1 otherwise (NaN always 1) */
        uint32_t eq = (fa == fb) ? 0u : 1u;
        /* lt 三态（mcs251_float.h __ltsf2 契约，2026-09-09 对齐）：
         *   a<b -> -1(0xFFFFFFFF)，a==b -> 0，a>b -> +1，NaN -> +1 */
        uint32_t lt;
        if (fa != fa || fb != fb) lt = 1u;
        else if (fa < fb) lt = 0xFFFFFFFFu;
        else if (fa == fb) lt = 0u;
        else lt = 1u;
        printf("CMP a="); hex32(a); printf(" b="); hex32(b);
        printf(" eq="); hex32(eq); printf(" lt="); hex32(lt); printf("\n");
    }

    /* math functions。
     * 2026-09-09 Kazimi：数学函数的期望值不用 glibc（glibc 与运行时的
     * 多项式逼近不可能逐位一致），改为链接运行时参考实现本身
     * （mcs251_float_math.c 宿主编译），验收语义 = "目标执行 == 同一
     * 算法在宿主的执行"。与 glibc 的精度差另行由 host_math --glibc
     * 模式度量，不进逐字节对拍。 */
    for (i = 0; i < NVEC_MATH; i++) {
        uint32_t x = vec_math[i];
        uint32_t r;
        r = _sinf(x);
        printf("SIN x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _cosf(x);
        printf("COS x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _tanf(x);
        printf("TAN x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _sqrtf(x);
        printf("SQRT x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _fabsf(x);
        printf("FABS x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _floorf(x);
        printf("FLOOR x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _ceilf(x);
        printf("CEIL x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _expf(x);
        printf("EXP x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = _logf(x);
        printf("LOG x="); hex32(x); printf(" r="); hex32(r); printf("\n");
        r = f2u(-u2f(x));  /* IEEE 取负=符号位翻转，与 _negsf2 等价（含 ±0/NaN） */
        printf("NEG x="); hex32(x); printf(" r="); hex32(r); printf("\n");
    }

    /* powf: x^2 （运行时参考实现） */
    for (i = 0; i < NVEC_MATH; i++) {
        uint32_t x = vec_math[i], y = F32_POS_TWO;
        uint32_t r = _powf(x, y);
        printf("POW x="); hex32(x); printf(" y="); hex32(y);
        printf(" r="); hex32(r); printf("\n");
    }

    /* int->float */
    for (i = 0; i < NVEC_I2F; i++) {
        int32_t v = vec_i2f[i];
        uint32_t r = f2u((float)v);
        printf("I2F v="); hex32((uint32_t)v);
        printf(" r="); hex32(r); printf("\n");
    }

    /* float->int */
    for (i = 0; i < NVEC_F2I; i++) {
        uint32_t v = vec_f2i[i];
        uint32_t r = (uint32_t)(int32_t)u2f(v);
        printf("F2I v="); hex32(v);
        printf(" r="); hex32(r); printf("\n");
    }

    printf("FLOAT-PASS\n");
    return 0;
}
