/*===-- host_arith.c ------------------------------------------------------===*/
/*
 * MCS251 软浮点运行时验收 —— 宿主机 Oracle-A（运算+比较+转换部分）。
 * 输出格式与 arith_firmware.c 逐字节一致。
 * 编译：cc -std=c11 -O2 -I. host_arith.c -o host_arith -lm
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void hex32(uint32_t v) { printf("%08X", v); }

#include "float_vectors.h"

int main(void)
{
    uint32_t i;
    for (i = 0; i < NVEC_ADDSUB; i++) {
        uint32_t a = vec_add_a[i], b = vec_add_b[i];
        uint32_t r = f2u(u2f(a) + u2f(b));
        printf("ADD a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
        r = f2u(u2f(a) - u2f(b));
        printf("SUB a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
    }
    for (i = 0; i < NVEC_MUL; i++) {
        uint32_t a = vec_mul_a[i], b = vec_mul_b[i];
        uint32_t r = f2u(u2f(a) * u2f(b));
        printf("MUL a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
    }
    for (i = 0; i < NVEC_DIV; i++) {
        uint32_t a = vec_div_a[i], b = vec_div_b[i];
        uint32_t r = f2u(u2f(a) / u2f(b));
        printf("DIV a="); hex32(a); printf(" b="); hex32(b);
        printf(" r="); hex32(r); printf("\n");
    }
    for (i = 0; i < NVEC_CMP; i++) {
        uint32_t a = vec_cmp_a[i], b = vec_cmp_b[i];
        float fa = u2f(a), fb = u2f(b);
        int un = (fa != fa) || (fb != fb);
        /* compiler-rt ABI（与固件/运行时一致，Alice 复审最终裁定）：
         * eq/ne：有序相等 0，不等或 NaN 1；
         * lt 三态：a<b 为 (uint32_t)-1，相等 0，a>b 为 1，NaN 为 +1；
         * ge 三态：有序同上，但 NaN 为 -1（GE 族 UNORDERED=-1）。 */
        uint32_t eq = ((fa == fb) && !un) ? 0u : 1u;
        uint32_t ne = un ? 1u : ((fa != fb) ? 1u : 0u);
        uint32_t lt, ge;
        if (un) { lt = 1u; ge = 0xFFFFFFFFu; }
        else if (fa < fb) { lt = 0xFFFFFFFFu; ge = 0xFFFFFFFFu; }
        else if (fa == fb) { lt = 0u; ge = 0u; }
        else { lt = 1u; ge = 1u; }
        printf("CMP a="); hex32(a); printf(" b="); hex32(b);
        printf(" eq="); hex32(eq); printf(" ne="); hex32(ne);
        printf(" lt="); hex32(lt); printf(" ge="); hex32(ge); printf("\n");
    }
    for (i = 0; i < NVEC_I2F; i++) {
        int32_t v = vec_i2f[i];
        uint32_t r = f2u((float)v);
        printf("I2F v="); hex32((uint32_t)v);
        printf(" r="); hex32(r); printf("\n");
    }
    for (i = 0; i < NVEC_F2I; i++) {
        uint32_t v = vec_f2i[i];
        uint32_t r = (uint32_t)(int32_t)u2f(v);
        printf("F2I v="); hex32(v);
        printf(" r="); hex32(r); printf("\n");
    }
    for (i = 0; i < NVEC_NEG; i++) {
        uint32_t x = vec_neg_data[i];
        uint32_t r = f2u(-u2f(x));
        printf("NEG x="); hex32(x); printf(" r="); hex32(r); printf("\n");
    }
    printf("ARITH-PASS\n");
    return 0;
}
