/*===-- host_fuzz.c -------------------------------------------------------===*/
/*
 * MCS251 软浮点运行时 —— 宿主 fuzz 回归测试（可重复，失败返回非零）。
 *
 * 在宿主机上把运行时源码当普通 C 编译（运行时只用 uint32 逻辑，
 * 不依赖目标专属构造），与宿主 IEEE-754 单精度逐位比对：
 *   - 结构化边界：特殊值全配对、指数边界网格、非正规/半途模式
 *   - 随机：固定种子（可重复），全 32 位位模式
 *   - 覆盖：add/sub/mul/div、7 个比较、int<->float 转换
 *   - Alice 复核的 4 个 add/sub/mul 复现用例硬编码在内
 *
 * NaN 策略：输入含 NaN 时宿主逐位语义是实现定义（载荷传递），
 * 只验证结果为 NaN；无 NaN 输入而生成的 NaN（0/0、Inf-Inf 等）
 * 按本库约定 F32_NEG_QNAN=0xFFC00000 与 x86 默认 QNaN 逐位一致。
 *
 * 编译：cc -std=c11 -O2 -I../src host_fuzz.c ../src/mcs251_float_arith.c \
 *          ../src/mcs251_float_cmp.c ../src/mcs251_bitutil.c -lm
 * 退出码：0 = 全部一致；1 = 存在差异（并打印前若干条）。
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "mcs251_float.h"

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static long g_total = 0, g_bad = 0;

static void note(const char *op, uint32_t a, uint32_t b,
                 uint32_t got, uint32_t exp)
{
    if (got == exp) { g_total++; return; }
    g_bad++;
    if (g_bad <= 25) {
        printf("MISMATCH %-4s a=%08X b=%08X got=%08X exp=%08X (%g %s %g)\n",
               op, a, b, got, exp, u2f(a), op, u2f(b));
    }
    g_total++;
}

/* NaN 判定（位级，避免宿主编译器把 NaN 比较常量折叠掉） */
static int is_nan_bits(uint32_t v)
{
    return ((v & 0x7F800000u) == 0x7F800000u) && ((v & 0x007FFFFFu) != 0u);
}

/* 运算后统一校验：op 输入任一为 NaN 时只要求 got 为 NaN；
 * 宿主结果为 NaN 时要求 got == 0xFFC00000（x86 默认 QNaN，与库约定一致）；
 * 其余要求逐位相等。 */
static void check(const char *op, uint32_t a, uint32_t b,
                  uint32_t got, uint32_t exp)
{
    g_total++;
    if (is_nan_bits(a) || is_nan_bits(b)) {
        if (!is_nan_bits(got)) {
            g_bad++;
            if (g_bad <= 25)
                printf("MISMATCH %-4s a=%08X b=%08X got=%08X (NaN input, NaN expected)\n",
                       op, a, b, got);
        }
        return;
    }
    if (is_nan_bits(exp)) {
        if (got != 0xFFC00000u) {
            g_bad++;
            if (g_bad <= 25)
                printf("MISMATCH %-4s a=%08X b=%08X got=%08X exp=FFC00000 (gen-NaN)\n",
                       op, a, b, got);
        }
        return;
    }
    if (got != exp) {
        g_bad++;
        if (g_bad <= 25)
            printf("MISMATCH %-4s a=%08X b=%08X got=%08X exp=%08X (%g %s %g)\n",
                   op, a, b, got, exp, u2f(a), op, u2f(b));
    }
}

static uint32_t rnd32(void)
{
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}

/* 特殊值 + 代表性边界位模式 */
static const uint32_t specials[] = {
    0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x40000000u, 0xC0000000u,
    0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x00800000u, 0x80800000u,
    0x7F7FFFFFu, 0xFF7FFFFFu, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
    0x3F490FDBu, 0x40490FDBu, 0x3FB504F3u, 0x402DF854u,
    0x4B000000u, 0xCB000000u, 0x41200000u, 0xC1200000u,
    0x3E800000u, 0xBE800000u, 0x3F000000u, 0xBF000000u,
    0x00400000u, 0x80400000u, 0x00200000u, 0x80200000u,
    0x00080000u, 0x80080000u,
    0x00000002u, 0x00000003u, 0x3F7FFFFFu, 0x3F800001u, 0x3FC00000u,
    0x3FFFFFFFu, 0x60000000u, 0x7F000000u, 0x00000007u, 0x00000FFFu,
};
#define NSPEC ((int)(sizeof(specials) / sizeof(specials[0])))

static void arith_pair(uint32_t a, uint32_t b)
{
    float fa = u2f(a), fb = u2f(b);
    check("add", a, b, _addsf3(a, b), f2u(fa + fb));
    check("sub", a, b, _subsf3(a, b), f2u(fa - fb));
    check("mul", a, b, _mulsf3(a, b), f2u(fa * fb));
    if (!F32_IS_ZERO(b)) {
        check("div", a, b, _divsf3(a, b), f2u(fa / fb));
    } else {
        /* 0/0 与 x/0：NaN/Inf 路径也要跑 */
        check("div", a, b, _divsf3(a, b), f2u(fa / fb));
    }
}

static void cmp_pair(uint32_t a, uint32_t b)
{
    float fa = u2f(a), fb = u2f(b);
    int un = (fa != fa) || (fb != fb);
    /* compiler-rt ABI（与固件/运行时一致，Alice 复审最终裁定）：
     * eq/ne：有序相等 0，不等或 NaN 1；
     * lt/le 三态：a<b 为 (uint32_t)-1，相等 0，a>b 为 1，NaN 为 +1；
     * gt/ge 三态：有序同上，但 NaN 为 -1（GE 族 UNORDERED=-1）。 */
    uint32_t e_eq = ((fa == fb) && !un) ? 0u : 1u;
    uint32_t e_ne = un ? 1u : ((fa != fb) ? 1u : 0u);
    uint32_t e_lt, e_ge;
    if (un) { e_lt = 1u; e_ge = 0xFFFFFFFFu; }
    else if (fa < fb) { e_lt = 0xFFFFFFFFu; e_ge = 0xFFFFFFFFu; }
    else if (fa == fb) { e_lt = 0u; e_ge = 0u; }
    else { e_lt = 1u; e_ge = 1u; }
    uint32_t e_un = un ? 1u : 0u;
    note("eq", a, b, _eqsf2(a, b), e_eq);
    note("ne", a, b, _nesf2(a, b), e_ne);
    note("lt", a, b, _ltsf2(a, b), e_lt);
    note("le", a, b, _lesf2(a, b), e_lt);
    note("gt", a, b, _gtsf2(a, b), e_ge);
    note("ge", a, b, _gesf2(a, b), e_ge);
    note("un", a, b, _unordsf2(a, b), e_un);
}

int main(void)
{
    int i, j;

    /* ---- 0. Alice 复核的硬编码复现用例（回归锚点） ---- */
    {
        struct { const char *n; uint32_t a, b, exp; } t[] = {
            {"add", 0x00000001u, 0x00000001u, 0x00000002u},
            {"sub", 0x3F800000u, 0x3F7FFFFFu, 0x33800000u},
            {"add", 0x3F800000u, 0x3F800001u, 0x40000000u},
            {"mul", 0x3FC00000u, 0x3FC00000u, 0x40100000u},
        };
        for (i = 0; i < 4; i++) {
            uint32_t r;
            if (t[i].n[0] == 'm') r = _mulsf3(t[i].a, t[i].b);
            else if (t[i].n[0] == 's') r = _subsf3(t[i].a, t[i].b);
            else r = _addsf3(t[i].a, t[i].b);
            note(t[i].n, t[i].a, t[i].b, r, t[i].exp);
        }
    }

    /* ---- 1. 特殊值全配对（运算 + 比较） ---- */
    for (i = 0; i < NSPEC; i++) {
        for (j = 0; j < NSPEC; j++) {
            arith_pair(specials[i], specials[j]);
            cmp_pair(specials[i], specials[j]);
        }
    }

    /* ---- 2. 指数边界网格（含随机尾数，压对齐/进位/下溢路径） ---- */
    {
        const uint32_t exps[] = { 0, 1, 2, 3, 24, 125, 126, 127, 128, 129,
                                  200, 252, 253, 254, 255 };
        srand(7);
        for (i = 0; i < (int)(sizeof(exps) / sizeof(exps[0])); i++) {
            for (j = 0; j < (int)(sizeof(exps) / sizeof(exps[0])); j++) {
                int k;
                for (k = 0; k < 8; k++) {
                    uint32_t m1 = rnd32() & 0x007FFFFFu;
                    uint32_t m2 = rnd32() & 0x007FFFFFu;
                    uint32_t a = (exps[i] << 23) | m1;
                    uint32_t b = (exps[j] << 23) | m2;
                    if ((rnd32() & 1u) && m1) a |= 0x80000000u;
                    if ((rnd32() & 1u) && m2) b |= 0x80000000u;
                    arith_pair(a, b);
                }
            }
        }
    }

    /* ---- 3. 非正规/半途重点（压 subnormal 舍入与 halfway 偶舍） ---- */
    {
        const uint32_t low[] = { 0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u,
                                 0x00000007u, 0x00000FFFu, 0x003FFFFFu, 0x00400000u,
                                 0x007FFFFEu, 0x007FFFFFu, 0x00800000u, 0x00800001u,
                                 0x00FFFFFFu, 0x3F7FFFFFu, 0x3F800000u, 0x3F800001u,
                                 0x3FFFFFFFu, 0x40000000u };
        for (i = 0; i < (int)(sizeof(low) / sizeof(low[0])); i++) {
            for (j = 0; j < (int)(sizeof(low) / sizeof(low[0])); j++) {
                arith_pair(low[i], low[j]);
                arith_pair(low[i] | 0x80000000u, low[j]);
                arith_pair(low[i], low[j] | 0x80000000u);
            }
        }
        /* 2 的幂附近的乘除（压规范化边界） */
        {
            uint32_t pw[] = { 0x3F000000u, 0x3E800000u, 0x40000000u, 0x60000000u,
                              0x5F000000u, 0x46000000u, 0x86000000u };
            for (i = 0; i < (int)(sizeof(pw) / sizeof(pw[0])); i++) {
                for (j = 0; j < (int)(sizeof(low) / sizeof(low[0])); j++) {
                    arith_pair(pw[i], low[j]);
                    arith_pair(low[j], pw[i]);
                }
            }
        }
    }

    /* ---- 4. 随机全位模式（固定种子，可重复） ---- */
    srand(42);
    for (i = 0; i < 200000; i++) {
        arith_pair(rnd32(), rnd32());
    }
    srand(43);
    for (i = 0; i < 20000; i++) {
        cmp_pair(rnd32(), rnd32());
    }

    /* ---- 5. 转换：int32 <-> float ---- */
    {
        const int32_t iv[] = { 0, 1, -1, 2, -2, 0x7FFFFFFF, (-0x7FFFFFFF - 1),
                               100, -1000, 0x1000000, -0x1000000,
                               0x7FFFFF80, -0x7FFFFF80, 123456789, -987654321,
                               0xFFFFFF, 0x1000001, 16777217, -16777217,
                               0x3000000, 0x7F000000 };
        for (i = 0; i < (int)(sizeof(iv) / sizeof(iv[0])); i++) {
            note("i2f", (uint32_t)iv[i], 0, _floatsisf(iv[i]),
                 f2u((float)iv[i]));
        }
        srand(44);
        for (i = 0; i < 50000; i++) {
            int32_t v = (int32_t)rnd32();
            note("i2f", (uint32_t)v, 0, _floatsisf(v), f2u((float)v));
        }
        for (i = 0; i < NSPEC; i++) {
            float f = u2f(specials[i]);
            uint32_t exp;
            if (f != f) exp = 0u;                        /* NaN -> 0（本库约定） */
            else if (f > 2147483520.0f) exp = 0x7FFFFFFFu;   /* 饱和（Inf 含） */
            else if (f < -2147483520.0f) exp = 0x80000000u;
            else exp = (uint32_t)(int32_t)f;             /* 向零截断 */
            note("f2i", specials[i], 0, _fixsfsi(specials[i]), exp);
        }
        srand(45);
        for (i = 0; i < 50000; i++) {
            uint32_t u = rnd32();
            float f = u2f(u);
            uint32_t exp;
            if (f != f) exp = 0u;
            else if (f > 2147483520.0f) exp = 0x7FFFFFFFu;
            else if (f < -2147483520.0f) exp = 0x80000000u;
            else exp = (uint32_t)(int32_t)f;
            note("f2i", u, 0, _fixsfsi(u), exp);
        }

        /* ---- 5b. 无符号转换：uint32 <-> float（G7 S1' 连接）----
         * 边界含 0/1/0x7FFFFFFF/0x80000000/0xFFFFFFFF（D1 明文要求），
         * 再加全域抽样；宿主 C 的 uint32->float（最近偶数）与
         * float->uint32（向零截断）转换即 IEEE-754 参照。 */
        {
            const uint32_t uv[] = { 0u, 1u, 2u, 0x7FFFFFFFu, 0x80000000u,
                                    0x80000001u, 0xFFFFFFFFu, 0xFFFFFFu,
                                    0x1000000u, 0x1000001u, 0x7FFFFF80u,
                                    0x7FFFFF81u, 0xFFFFFE80u };
            for (i = 0; i < (int)(sizeof(uv) / sizeof(uv[0])); i++) {
                note("u2f", uv[i], 0, _floatunsisf(uv[i]),
                     f2u((float)uv[i]));
            }
            srand(46);
            for (i = 0; i < 50000; i++) {
                uint32_t v = rnd32();
                note("u2f", v, 0, _floatunsisf(v), f2u((float)v));
            }
            /* 无符号 f2u 口径（compiler-rt __fixunssfsi）：NaN -> 0、
             * 负值（含 -0.0）-> 0、>= 2^32 饱和 0xFFFFFFFF（Inf 同）。 */
            for (i = 0; i < NSPEC; i++) {
                float f = u2f(specials[i]);
                uint32_t exp;
                if (f != f) exp = 0u;
                else if (f < 0.0f) exp = 0u;
                else if (f >= 4294967296.0f) exp = 0xFFFFFFFFu;
                else exp = (uint32_t)f;
                note("f2u", specials[i], 0, _fixunssfsi(specials[i]), exp);
            }
            srand(47);
            for (i = 0; i < 50000; i++) {
                uint32_t u = rnd32();
                float f = u2f(u);
                uint32_t exp;
                if (f != f) exp = 0u;
                else if (f < 0.0f) exp = 0u;
                else if (f >= 4294967296.0f) exp = 0xFFFFFFFFu;
                else exp = (uint32_t)f;
                note("f2u", u, 0, _fixunssfsi(u), exp);
            }
        }
    }

    printf("host_fuzz: %ld checks, %ld mismatches\n", g_total, g_bad);
    if (g_bad != 0) {
        printf("host_fuzz: FAIL\n");
        return 1;
    }
    printf("host_fuzz: PASS\n");
    return 0;
}
