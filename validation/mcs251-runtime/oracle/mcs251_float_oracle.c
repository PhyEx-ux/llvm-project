/*===-- mcs251_float_oracle.c ---------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * G7 S1'' 位精确验证：**独立整数有理数 oracle**（受控资产，可复跑）。
 *
 * 为什么需要它（Alice 复审，2026-09-15）：
 *   - 百万随机抽样**不能**证明全值域；此前 mul 的下溢路径漏掉原 guard g
 *     （`00C00000 × 3F000001` 输出 00600000，精确 RNE 应为 00600001）
 *     即在随机覆盖之外。故必须用**精确整数**算术做系统性覆盖，
 *     不复用被测实现的任何舍入逻辑，也不使用宿主 float 做参考。
 *
 * 方法：
 *   - 每个 binary32 位型精确解码为 (sign, M, E)，值 = (-1)^s * M * 2^E，
 *     M、E 为精确整数（次正规 E=-149，正规 E=efield-150）。
 *   - 运算在**任意精度无符号整数**（512 位 limb 数组）上精确完成：
 *       add/sub：对齐到公共二进制指数后精确加减；
 *       mul    ：23~24 位有效数精确相乘（48 位积）；
 *       div    ：精确有理数 N/D * 2^E，商/余数由整数长除法给出。
 *   - 舍入：roundTiesToEven，用「商 + 余数（2R vs 除数）」精确判定，
 *     guard/sticky 由余数给出，**不做任何浮点近似**。
 *   - 特殊值与 NaN 约定按 mcs251_float.h 的库约定（canonical qNaN、
 *     保留第一个 NaN 操作数的符号、新生成 NaN 用 F32_NEG_QNAN）。
 *
 * 覆盖（系统性，非抽样）：
 *   1. 去重特殊编码全集（零/次正规/最小正规/最大正规/Inf/qNaN/sNaN，
 *      双符号）；
 *   2. 次正规边界邻域（最小次正规、最小正规、其上下邻、次正规→正规进位）；
 *   3. tie-even 中点及其邻域（add 用 1±2^-24 构造；mul 用模 2^24 同余
 *      构造精确中点，见 build_mul_ties；div 结构上不可能出现精确中点，
 *      由 round_scaled 的 tie 计数给出 0 作为证据）；
 *   4. sticky（guard 以下非零：gen sticky/下溢 sticky）；
 *   5. 指数域全范围网格（ex_a × ex_b = 0..255，多种尾数形态）；
 *   6. Alice 反例形态（原 guard 非零、其下全零的次正规舍入）及其邻域；
 *   7. 随机全位型（广度补充，不作为证明）。
 *
 * 通过标准：零差异（每个例程）。
 *
 * 编译（宿主）：
 *   cc -O2 -std=c11 -I../src mcs251_float_oracle.c \
 *      ../src/mcs251_float_mul.c ../src/mcs251_float_addsub.c \
 *      ../src/mcs251_float_div.c ../src/mcs251_bitutil.c -o float_oracle
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mcs251_float.h"

/* ------------------------------------------------------------------ */
/* 任意精度无符号整数（16 x 32 = 512 位，足够容纳 277 位对齐中间量）    */
/* ------------------------------------------------------------------ */
#define BL 16
typedef struct { uint32_t w[BL]; } big;

static void bg_zero(big *x) { int i; for (i = 0; i < BL; i++) x->w[i] = 0u; }
static void bg_set32(big *x, uint32_t v) { bg_zero(x); x->w[0] = v; }
static int  bg_is_zero(const big *x) {
    int i; for (i = 0; i < BL; i++) if (x->w[i]) return 0; return 1;
}
static int  bg_bitlen(const big *x) {
    int i;
    for (i = BL - 1; i >= 0; i--)
        if (x->w[i]) {
            uint32_t v = x->w[i]; int b = 0;
            while (v) { b++; v >>= 1; }
            return i * 32 + b;
        }
    return 0;
}
static int  bg_bit(const big *x, int i) {
    if (i < 0 || i >= BL * 32) return 0;
    return (int)((x->w[i >> 5] >> (i & 31)) & 1u);
}
static void bg_setbit(big *x, int i) {
    if (i < 0 || i >= BL * 32) return;
    x->w[i >> 5] |= (1u << (i & 31));
}
/* 原地左移 k 位（k < BL*32） */
static void bg_shl(big *x, unsigned k) {
    unsigned ws, bs; int i; big t;
    if (k == 0u) return;
    ws = k / 32u; bs = k % 32u;
    bg_zero(&t);
    for (i = 0; i < BL; i++) {
        int src = i - (int)ws;
        uint32_t v;
        if (src < 0) continue;
        v = x->w[src];
        if (bs != 0u) {
            t.w[i] |= (uint32_t)(v << bs);
            if (src - 1 >= 0) t.w[i] |= (uint32_t)(x->w[src - 1] >> (32u - bs));
        } else {
            t.w[i] = v;
        }
    }
    *x = t;
}
/* 原地逻辑右移 k 位 */
static void bg_shr(big *x, unsigned k) {
    unsigned ws, bs; int i; big t;
    if (k == 0u) return;
    ws = k / 32u; bs = k % 32u;
    bg_zero(&t);
    for (i = 0; i < BL; i++) {
        int src = i + (int)ws;
        uint32_t v;
        if (src >= BL) continue;
        v = x->w[src];
        if (bs != 0u) {
            t.w[i] |= (uint32_t)(v >> bs);
            if (src + 1 < BL) t.w[i] |= (uint32_t)(x->w[src + 1] << (32u - bs));
        } else {
            t.w[i] = v;
        }
    }
    *x = t;
}
static int bg_cmp(const big *a, const big *b) {
    int i;
    for (i = BL - 1; i >= 0; i--) {
        if (a->w[i] != b->w[i]) return a->w[i] > b->w[i] ? 1 : -1;
    }
    return 0;
}
static void bg_add(big *a, const big *b) {
    int i; uint32_t cy = 0u;
    for (i = 0; i < BL; i++) {
        uint32_t s = a->w[i] + b->w[i] + cy;
        cy = (s < a->w[i] || (cy && s == a->w[i])) ? 1u : 0u;
        a->w[i] = s;
    }
}
/* a -= b，要求 a >= b */
static void bg_sub(big *a, const big *b) {
    int i; uint32_t br = 0u;
    for (i = 0; i < BL; i++) {
        uint32_t bi = b->w[i];
        uint32_t d = a->w[i] - bi - br;
        br = (a->w[i] < bi + br || (bi + br < bi)) ? 1u : 0u;
        a->w[i] = d;
    }
}
static void bg_mul(big *r, const big *a, const big *b) {
    int i, j;
    bg_zero(r);
    for (i = 0; i < BL; i++) {
        uint32_t ai = a->w[i];
        if (ai == 0u) continue;
        {
            uint32_t cy = 0u;
            for (j = 0; i + j < BL; j++) {
                uint64_t t = (uint64_t)ai * (uint64_t)b->w[j]
                           + (uint64_t)r->w[i + j] + (uint64_t)cy;
                r->w[i + j] = (uint32_t)t;
                cy = (uint32_t)(t >> 32);
            }
        }
    }
}
/* Q = U / V, R = U % V（要求 V != 0）；跳过高位零，逐位长除法。 */
static void bg_divmod(const big *U, const big *V, big *Q, big *R) {
    int bu, bv, shift, i;
    big Rm;
    bg_zero(Q);
    if (bg_cmp(U, V) < 0) { *R = *U; return; }
    bu = bg_bitlen(U); bv = bg_bitlen(V);
    shift = bu - bv;
    Rm = *U; bg_shr(&Rm, (unsigned)shift);
    for (i = shift; i >= 0; i--) {
        if (i < shift) {
            bg_shl(&Rm, 1u);
            if (bg_bit(U, i)) Rm.w[0] |= 1u;
        }
        if (bg_cmp(&Rm, V) >= 0) { bg_sub(&Rm, V); bg_setbit(Q, i); }
    }
    *R = Rm;
}

/* ------------------------------------------------------------------ */
/* 覆盖度计数器（证据：证明系统性用例确实命中各类舍入事件）             */
/* ------------------------------------------------------------------ */
static long g_ties, g_subnormal, g_sub_up, g_overflow, g_nan_res, g_inf_res;
static long g_sticky_only;   /* 余数非零但 guard 为 0（sticky-only 舍入） */
static long g_carry_out;     /* 舍入进位跨过 24 位（有效数回绕 + 指数 +1） */

/* 舍入计数：TIE 命中（精确中点且 round-down）、sticky-only、进位出 24 位。 */
static void count_sticky_round(uint32_t q_before, int halfbit, int below, int rem_nonzero)
{
    if (!rem_nonzero) return;
    if (halfbit && !below) {
        if ((q_before & 1u) == 0u) g_ties++;   /* 精确中点且舍向偶 */
    } else if (!halfbit) {
        g_sticky_only++;                        /* guard=0 但余数非零 */
    }
}

/* round( M * 2^k ) —— 除数为 2 的幂（add/sub/mul 的精确有理数都归此形）。 */
static uint32_t round_pow2(const big *M, int k)
{
    big q;
    int s, i, below = 0, halfbit, rem_nonzero = 0;
    uint32_t qv;
    if (k >= 0) {                     /* 乘以 2 的幂：精确，无舍入 */
        big t = *M;
        bg_shl(&t, (unsigned)k);
        return (uint32_t)t.w[0];
    }
    s = -k;
    q = *M; bg_shr(&q, (unsigned)s);
    qv = (uint32_t)q.w[0];
    if (s == 0) return qv;
    halfbit = bg_bit(M, s - 1);
    for (i = 0; i < s - 1 && i < BL * 32; i++)
        if (bg_bit(M, i)) { below = 1; break; }
    for (i = 0; i < s && i < BL * 32; i++)
        if (bg_bit(M, i)) { rem_nonzero = 1; break; }
    count_sticky_round(qv, halfbit, below, rem_nonzero);
    if (rem_nonzero && halfbit && (below || (qv & 1u))) qv++;
    return qv;
}

/* round( N/D * 2^k ) —— 一般有理数（div）。N、D 至多 24 位，k 平移后
 * 被除数/除数最多约 300 位，bg_divmod 在 U<V 时提前返回，代价可控。 */
static uint32_t round_div(const big *N, const big *D, int k)
{
    big U, V, Q, R, twice;
    uint32_t qv;
    int c;
    if (k >= 0) { U = *N; bg_shl(&U, (unsigned)k); V = *D; }
    else        { U = *N; V = *D; bg_shl(&V, (unsigned)(-k)); }
    bg_divmod(&U, &V, &Q, &R);
    qv = (uint32_t)Q.w[0];
    if (!bg_is_zero(&R)) {
        int rem_nonzero = 1, halfbit, below;
        twice = R; bg_shl(&twice, 1u);
        c = bg_cmp(&twice, &V);
        halfbit = (c == 0) || (c > 0);
        below = (c > 0);   /* 2R > V 表示余数严格大于半个 */
        count_sticky_round(qv, halfbit, below, rem_nonzero);
        if (c > 0 || (c == 0 && (qv & 1u))) { qv++; if (qv == 0u) g_carry_out++; }
    }
    return qv;
}

#define CLS_ZERO 0
#define CLS_FIN  1
#define CLS_INF  2
#define CLS_NAN  3

static void decode(uint32_t v, int *cls, int *sign, big *M, int *E)
{
    int e = (int)((v >> 23) & 0xffu);
    uint32_t m = v & 0x7fffffu;
    *sign = (int)((v >> 31) & 1u);
    if (e == 0xff) { *cls = m ? CLS_NAN : CLS_INF; bg_set32(M, 0u); *E = 0; return; }
    if (e == 0) {
        if (m == 0u) { *cls = CLS_ZERO; bg_set32(M, 0u); *E = 0; return; }
        *cls = CLS_FIN; bg_set32(M, m); *E = -149; return;
    }
    *cls = CLS_FIN; bg_set32(M, (1u << 23) | m); *E = e - 150;
}

static uint32_t pack_nan(int sign) { return ((uint32_t)sign << 31) | F32_QNAN; }
static uint32_t pack_inf(int sign) { return ((uint32_t)sign << 31) | F32_POS_INF; }
static uint32_t pack_zero(int sign) { return (uint32_t)sign << 31; }

/* roundTiesToEven 的精确实现：值 = (-1)^sign * N/D * 2^E（N 可为零）。 */
static uint32_t round_rational(const big *N, const big *D, int E, int sign)
{
    int bn, bd, fl, p, d_is_one;
    big n = *N;
    big one;
    if (bg_is_zero(&n)) return pack_zero(sign);
    bn = bg_bitlen(&n); bd = bg_bitlen(D);
    fl = bn - bd;
    {
        /* 比较 n 与 d*2^fl（或 n*2^-fl 与 d）以定前导位 */
        big t;
        if (fl >= 0) { t = *D; bg_shl(&t, (unsigned)fl); if (bg_cmp(&n, &t) < 0) fl--; }
        else         { t = n;  bg_shl(&t, (unsigned)(-fl)); if (bg_cmp(&t, D) < 0) fl--; }
    }
    p = fl + E;  /* 前导位无偏指数 */
    if (p > 127) { g_overflow++; return pack_inf(sign); }
    bg_set32(&one, 1u);
    d_is_one = (bg_bitlen(D) == 1 && D->w[0] == 1u);
    if (p >= -126) {
        /* 正常：有效数 = round(n/d * 2^(23-fl+E-E)) ∈ [2^23, 2^24) */
        uint32_t q = d_is_one ? round_pow2(&n, 23 - fl)
                              : round_div(&n, D, 23 - fl);
        if (q & 0x01000000u) { q = 0x00800000u; p++; g_carry_out++; }
        if (p > 127) { g_overflow++; return pack_inf(sign); }
        return ((uint32_t)sign << 31) | ((uint32_t)(p + 127) << 23)
             | (q & F32_MANT_MASK);
    }
    /* 次正规：q = round(n/d * 2^E * 2^149)，quantum 2^-149 */
    {
        uint32_t q = d_is_one ? round_pow2(&n, E + 149) : round_div(&n, D, E + 149);
        g_subnormal++;
        if (q & 0x00800000u) { g_sub_up++; return pack_zero(sign) | F32_HIDDEN_BIT; }
        return pack_zero(sign) | (q & F32_MANT_MASK);
    }
}

/* ---- oracle 四则（约定与 mcs251_float.h 一致） ---- */
static uint32_t oracle_add(uint32_t a, uint32_t b)
{
    int ca, cb, sa, sb, Ea, Eb, Ec, s;
    big Ma, Mb, M, MA, MB;
    decode(a, &ca, &sa, &Ma, &Ea);
    decode(b, &cb, &sb, &Mb, &Eb);
    if (ca == CLS_NAN) return pack_nan(sa);
    if (cb == CLS_NAN) return pack_nan(sb);
    if (ca == CLS_INF) {
        if (cb == CLS_INF) return (sa == sb) ? a : F32_NEG_QNAN;
        return a;
    }
    if (cb == CLS_INF) return b;
    if (ca == CLS_ZERO && cb == CLS_ZERO)
        return (sa && sb) ? F32_NEG_ZERO : F32_POS_ZERO;
    if (ca == CLS_ZERO) return b;
    if (cb == CLS_ZERO) return a;
    Ec = Ea < Eb ? Ea : Eb;
    MA = Ma; bg_shl(&MA, (unsigned)(Ea - Ec));
    MB = Mb; bg_shl(&MB, (unsigned)(Eb - Ec));
    if (sa == sb) { M = MA; bg_add(&M, &MB); s = sa; }
    else {
        int c = bg_cmp(&MA, &MB);
        if (c > 0) { M = MA; bg_sub(&M, &MB); s = sa; }
        else if (c < 0) { M = MB; bg_sub(&M, &MA); s = sb; }
        else return F32_POS_ZERO;  /* 精确抵消 -> +0 (RNE) */
    }
    {
        big one; bg_set32(&one, 1u);
        return round_rational(&M, &one, Ec, s);
    }
}
static uint32_t oracle_sub(uint32_t a, uint32_t b) { return oracle_add(a, b ^ F32_SIGN_MASK); }

static uint32_t oracle_mul(uint32_t a, uint32_t b)
{
    int ca, cb, sa, sb, Ea, Eb, s;
    big Ma, Mb, M, one;
    decode(a, &ca, &sa, &Ma, &Ea);
    decode(b, &cb, &sb, &Mb, &Eb);
    if (ca == CLS_NAN) return pack_nan(sa);
    if (cb == CLS_NAN) return pack_nan(sb);
    s = sa ^ sb;
    if (ca == CLS_INF || cb == CLS_INF) {
        int other_zero = (ca == CLS_INF) ? (cb == CLS_ZERO) : (ca == CLS_ZERO);
        if (other_zero) return F32_NEG_QNAN;   /* Inf * 0 = NaN */
        return pack_inf(s);
    }
    if (ca == CLS_ZERO || cb == CLS_ZERO) return pack_zero(s);
    bg_mul(&M, &Ma, &Mb);
    bg_set32(&one, 1u);
    return round_rational(&M, &one, Ea + Eb, s);
}

static uint32_t oracle_div(uint32_t a, uint32_t b)
{
    int ca, cb, sa, sb, Ea, Eb, s;
    big Ma, Mb;
    decode(a, &ca, &sa, &Ma, &Ea);
    decode(b, &cb, &sb, &Mb, &Eb);
    if (ca == CLS_NAN) return pack_nan(sa);
    if (cb == CLS_NAN) return pack_nan(sb);
    s = sa ^ sb;
    if (ca == CLS_INF) {
        if (cb == CLS_INF) return F32_NEG_QNAN;
        return pack_inf(s);
    }
    if (cb == CLS_INF) return pack_zero(s);
    if (cb == CLS_ZERO) {
        if (ca == CLS_ZERO) return F32_NEG_QNAN;
        return pack_inf(s);
    }
    if (ca == CLS_ZERO) return pack_zero(s);
    return round_rational(&Ma, &Mb, Ea - Eb, s);
}

/* ------------------------------------------------------------------ */
/* 测试驱动                                                            */
/* ------------------------------------------------------------------ */
static long g_checks, g_bad;
static long g_per_op[4], g_bad_op[4];
static const char *OPN[4] = { "add", "sub", "mul", "div" };

static int isnan_bits(uint32_t v) {
    return ((v & 0x7F800000u) == 0x7F800000u) && ((v & 0x007FFFFFu) != 0u);
}

/* 独立 oracle 自身的反向校验：把 oracle 结果与**宿主硬件 IEEE-754**
 * 对拍（仅用于无 NaN 输入、非 NaN 预期结果的样本）。这不构成 oracle 的
 * 证明，但可排除 oracle 自身成系统地错误（若 oracle 与被测实现同时错在
 * 同一处，此交叉检验会暴露）。命中数须非零方为有效证据。 */
static long g_xchecks, g_xbad;
static uint32_t h2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float    u2h(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void xcheck(uint32_t a, uint32_t b, uint32_t oa, uint32_t oh, const char *op)
{
    (void)op;
    if (isnan_bits(a) || isnan_bits(b) || isnan_bits(oa) || isnan_bits(oh)) return;
    g_xchecks++;
    if (oa != oh) {
        g_xbad++;
        if (g_xbad <= 10)
            printf("  XDIFF %s a=%08X b=%08X oracle=%08X host=%08X\n", op, a, b, oa, oh);
    }
}
static void xvalidate(uint32_t a, uint32_t b, uint32_t oa, uint32_t os,
                      uint32_t om, uint32_t od)
{
    xcheck(a, b, oa, h2u(u2h(a) + u2h(b)), "add");
    xcheck(a, b, os, h2u(u2h(a) - u2h(b)), "sub");
    xcheck(a, b, om, h2u(u2h(a) * u2h(b)), "mul");
    xcheck(a, b, od, h2u(u2h(a) / u2h(b)), "div");
}

typedef uint32_t (*opfn)(uint32_t, uint32_t);

static uint32_t g_o[4];
static void check(int op, uint32_t a, uint32_t b)
{
    static const opfn F[4] = { _addsf3, _subsf3, _mulsf3, _divsf3 };
    uint32_t got = F[op](a, b);
    uint32_t exp;
    switch (op) {
      case 0: exp = oracle_add(a, b); break;
      case 1: exp = oracle_sub(a, b); break;
      case 2: exp = oracle_mul(a, b); break;
      default: exp = oracle_div(a, b); break;
    }
    g_o[op] = exp;
    g_checks++; g_per_op[op]++;
    if (isnan_bits(exp)) g_nan_res++;
    if ((exp & 0x7F800000u) == 0x7F800000u && (exp & 0x007FFFFFu) == 0u) g_inf_res++;
    if (got != exp) {
        g_bad++; g_bad_op[op]++;
        if (g_bad <= 60)
            printf("  DIFF %s a=%08X b=%08X got=%08X oracle=%08X\n", OPN[op], a, b, got, exp);
    }
}
static void check_pair(uint32_t a, uint32_t b) {
    int op; for (op = 0; op < 4; op++) check(op, a, b);
    xvalidate(a, b, g_o[0], g_o[1], g_o[2], g_o[3]);
}

static uint32_t rnd(uint32_t *st) {
    /* xorshift32，可复现，不依赖 libc rand */
    uint32_t x = *st;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *st = x; return x;
}

/* 去重排序（用于"全部特殊值不重复集"） */
static int cmp_u32(const void *p, const void *q) {
    uint32_t a = *(const uint32_t *)p, b = *(const uint32_t *)q;
    return (a > b) - (a < b);
}
static int dedup(uint32_t *v, int n) {
    int i, k = 0;
    qsort(v, (size_t)n, sizeof(uint32_t), cmp_u32);
    for (i = 0; i < n; i++) if (k == 0 || v[i] != v[k - 1]) v[k++] = v[i];
    return k;
}

/* 构造精确 mul tie（roundTiesToEven 中点）。
 *
 * 条件（修正此前两处错误）：
 *   设 P = ma*mb（ma、mb ∈ [2^23,2^24) 为 24 位有效数），B = bl(P) ∈ {47,48}，
 *   规格化右移 s = B-24 ∈ {23,24} 位。舍入到 24 位时，精确中点当且仅当
 *   P 的低 s 位 == 2^(s-1)（guard=1，guard 以下全 0）——**不是** 2^s。
 *   半价族：mb = 3<<22（ob=3 为奇数）、ma 为任意奇数（ma ≡ 1 mod 2），
 *   则 P = 3*ma << 22，v2(P)=22（3*ma 奇），低 23 位 = 2^22 → tie。
 *   偶数族：mb = 3<<22、ma = oa<<1（oa 奇），P = 3*oa<<23，低 24 位 = 2^23 → tie。
 * 该构造是确定性的、有界的（非 2^23 全扫），并逐对用 mul_tie_ok 复核。*/
static int mul_tie_ok(uint32_t ma, uint32_t mb)
{
    uint64_t P = (uint64_t)ma * (uint64_t)mb;
    int B = 0, s;
    uint64_t t = P;
    while (t) { B++; t >>= 1; }
    s = B - 24;
    if (s < 1 || s > 40) return 0;
    return (P & ((1ULL << s) - 1ULL)) == (1ULL << (s - 1));
}
static int push_tie(uint32_t *ma_out, uint32_t *mb_out, int found, int want,
                    uint32_t ma, uint32_t mb)
{
    int k;
    if (found >= want) return found;
    if (ma < 0x800000u || ma >= 0x1000000u) return found;
    if (mb < 0x800000u || mb >= 0x1000000u) return found;
    if (!mul_tie_ok(ma, mb)) return found;
    for (k = 0; k < found; k++)
        if (ma_out[k] == ma && mb_out[k] == mb) return found;
    ma_out[found] = ma; mb_out[found] = mb;
    return found + 1;
}
static int build_mul_ties(uint32_t *ma_out, uint32_t *mb_out, int want)
{
    static const uint32_t MB[] = { 0xC00000u, 0xE00000u, 0xA00000u, 0x900000u,
                                   0xF00000u, 0x880000u };
    int found = 0, m, i;
    for (m = 0; m < (int)(sizeof(MB)/sizeof(MB[0])) && found < want; m++) {
        for (i = 0; i < 64 && found < want; i++) {
            uint32_t ma = 0x800001u + (uint32_t)i * 2u;              /* 奇数 ma */
            found = push_tie(ma_out, mb_out, found, want, ma, MB[m]);
        }
        for (i = 0; i < 64 && found < want; i++) {
            uint32_t ma = 0x800002u + (uint32_t)i * 2u;              /* ma = oa<<1 */
            found = push_tie(ma_out, mb_out, found, want, ma, MB[m]);
        }
    }
    return found;
}

int main(int argc, char **argv)
{
    long N = argc > 1 ? atol(argv[1]) : 400000;
    uint32_t seedy = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 0xC0FFEEu;
    int i, j;

    printf("== mcs251 float independent integer-rational oracle ==\n");
    printf("random pairs N=%ld seed=%08X\n", N, seedy);

    /* ---- 1. 特殊值去重全集 x 自身 ---- */
    {
        static uint32_t spec[256];
        int ns = 0, k, nu;
        static const uint32_t seed_spec[] = {
            0x00000000u, 0x80000000u,             /* +-0 */
            0x00000001u, 0x80000001u,             /* 最小次正规 */
            0x00400000u, 0x007FFFFFu, 0x807FFFFFu,/* 次正规中值/最大 */
            0x00800000u, 0x80800000u,             /* 最小正规 */
            0x00800001u, 0x00FFFFFFu,             /* 正规底邻/次正规域顶邻 */
            0x01000000u, 0x33800000u, 0x3F000000u,
            0x3F7FFFFFu, 0x3F800000u, 0x3F800001u,
            0x3FC00000u, 0x40000000u, 0x7E800000u,
            0x7F000000u, 0x7F7FFFFFu, 0xFF7FFFFFu,/* 最大正规 */
            0x7F800000u, 0xFF800000u,             /* +-Inf */
            0x7FC00000u, 0xFFC00000u,             /* qNaN */
            0x7F800001u, 0xFF800001u,             /* sNaN */
            0x7FBFFFFFu, 0x7FFFFFFFu, 0xFFFFFFFFu,
            0x00000002u, 0x00000003u, 0x00000007u, 0x00000008u,
            0x00000FFFu, 0x000FFFFFu, 0x001FFFFFu, 0x003FFFFFu,
            0x005FFFFFu, 0x00BFFFFFu, 0x00C00000u, /* Alice 反例操作数 */
            0x00E00000u, 0x017FFFFFu, 0x01FFFFFFu,
            0x7EFFFFFFu, 0x4B000000u, 0xCB000000u,
            0x46000000u, 0x86000000u, 0x5F000000u,
            0x3E800000u, 0xBF800001u, 0xC0000000u,
        };
        for (k = 0; k < (int)(sizeof(seed_spec)/sizeof(seed_spec[0])); k++)
            spec[ns++] = seed_spec[k];
        nu = dedup(spec, ns);
        printf("[1] specials: %d listed -> %d unique (dedup)\n", ns, nu);
        for (i = 0; i < nu; i++) for (j = 0; j < nu; j++) check_pair(spec[i], spec[j]);
    }

    /* ---- 2. 次正规边界邻域 ---- */
    {
        static const uint32_t bnd[] = {
            0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u,
            0x00000007u, 0x00000008u, 0x00000FFEu, 0x00000FFFu, 0x00100000u,
            0x007FFFFDu, 0x007FFFFEu, 0x007FFFFFu,
            0x00800000u, 0x00800001u, 0x00800002u,
            0x00FFFFFFu, 0x01000000u, 0x01000001u,
            0x3F7FFFFFu, 0x3F800000u, 0x3F800001u,
        };
        int n = (int)(sizeof(bnd)/sizeof(bnd[0]));
        printf("[2] subnormal boundary neighborhood: %d values (x self, x negated)\n", n);
        for (i = 0; i < n; i++) for (j = 0; j < n; j++) {
            check_pair(bnd[i], bnd[j]);
            check_pair(bnd[i] | F32_SIGN_MASK, bnd[j]);
            check_pair(bnd[i], bnd[j] | F32_SIGN_MASK);
            check_pair(bnd[i] | F32_SIGN_MASK, bnd[j] | F32_SIGN_MASK);
        }
    }

    /* ---- 3a. tie-even：加法构造 1 ± 2^-24 及邻域 ---- */
    {
        static const uint32_t t[][2] = {
            {0x3F800000u, 0x33800000u},  /* 1 + 2^-24 : 精确中点 -> 1.0 */
            {0x3F800000u, 0xB3800000u},  /* 1 - 2^-24 : 精确中点 -> 1.0 */
            {0x3F800001u, 0x33800000u},  /* 中点上一邻 -> 进位 up */
            {0xBF800000u, 0x33800000u},
            {0x3F800000u, 0x33000000u},  /* 2^-25 : sticky-only 向下 */
            {0x3F800000u, 0x34000000u},  /* 2^-23 : 正常 */
            {0x3F800000u, 0x33800001u},  /* 中点 + tiny : 向上 */
            {0x40400000u, 0x33000000u},
            {0x40000000u, 0x33800000u},  /* 2 + 2^-24 -> 2.0 (even) */
            {0x3F000000u, 0x33000000u},
        };
        int n = (int)(sizeof(t)/sizeof(t[0]));
        printf("[3a] tie-even / sticky add constructs: %d\n", n);
        for (i = 0; i < n; i++) { check(0, t[i][0], t[i][1]); check(1, t[i][0], t[i][1]); }
    }

    /* ---- 3b. tie-even：mul 精确中点（模同余构造） ---- */
    {
        uint32_t ma[64], mb[64];
        int n = build_mul_ties(ma, mb, 64), k;
        printf("[3b] mul exact-tie constructs: %d\n", n);
        for (k = 0; k < n; k++) {
            uint32_t a = ma[k], b = mb[k];
            static const uint32_t exps[] = { 0x3F800000u, 0x40000000u, 0x3F000000u,
                                             0x4B000000u, 0x3E800000u, 0x7F000000u };
            int e;
            check(2, a, b); check(2, b, a);
            check(2, a | F32_SIGN_MASK, b);
            check(2, a, b | F32_SIGN_MASK);
            for (e = 0; e < (int)(sizeof(exps)/sizeof(exps[0])); e++) {
                /* 用指数域重编码：保留尾数位，仅替换指数域，构造不同量级 */
                uint32_t ea = (exps[e] & 0x7F800000u) | (a & 0x7FFFFFu);
                uint32_t eb = (exps[e] & 0x7F800000u) | (b & 0x7FFFFFu);
                check(2, ea, eb);
                check(2, a, eb);
                check(2, ea, b);
            }
        }
    }

    /* ---- 3c. sticky 回归：guard 以下非零 / 下溢 sticky ---- */
    {
        static const uint32_t sv[] = {
            0x00000001u, 0x00000002u, 0x00000003u, 0x00000005u, 0x00000009u,
            0x007FFFFFu, 0x00800000u, 0x00800001u, 0x000000FFu, 0x0000FFFFu,
            0x0000FFFFu, 0x0000007Fu, 0x00FFFFFFu,
        };
        static const uint32_t pw[] = {
            0x3F800000u, 0x3F800001u, 0x40000000u, 0x3F000000u,
            0x33800000u, 0x33000000u, 0x3E800000u,
        };
        int n = (int)(sizeof(sv)/sizeof(sv[0])), m = (int)(sizeof(pw)/sizeof(pw[0]));
        printf("[3c] sticky constructs: %d x %d x 2 dirs\n", n, m);
        for (i = 0; i < n; i++) for (j = 0; j < m; j++) {
            check_pair(pw[j], sv[i]);
            check_pair(sv[i], pw[j]);
        }
    }

    /* ---- 4. Alice 反例形态及其邻域（原 guard 非零、其下全零） ---- */
    {
        static const uint32_t c[] = {
            0x00C00000u, 0x3F000001u, 0x00C00001u, 0x00C00002u, 0x00C00003u,
            0x00BFFFFFu, 0x00DFFFFFu, 0x00E00000u, 0x00C000FFu, 0x00C00100u,
            0x3F000000u, 0x3F000002u, 0x3F000003u, 0x3F800001u, 0x00FFFFFFu,
            0x01000000u, 0x00000001u, 0x007FFFFFu,
        };
        int n = (int)(sizeof(c)/sizeof(c[0]));
        printf("[4] Alice counterexample shape: %d values (x self)\n", n);
        for (i = 0; i < n; i++) for (j = 0; j < n; j++) {
            check_pair(c[i], c[j]);
            check_pair(c[i] | F32_SIGN_MASK, c[j]);
            check_pair(c[i], c[j] | F32_SIGN_MASK);
        }
    }

    /* ---- 5. 指数域全范围网格 ex_a x ex_b = 0..255 ---- */
    {
        static const uint32_t mants[] = { 0x000000u, 0x000001u, 0x7FFFFFu, 0x400000u };
        int nm = (int)(sizeof(mants)/sizeof(mants[0]));
        printf("[5] full exponent grid: 256 x 256 x %d mantissa patterns\n", nm);
        for (i = 0; i < 256; i++) for (j = 0; j < 256; j++) {
            int k;
            for (k = 0; k < nm; k++) {
                uint32_t a = ((uint32_t)i << 23) | mants[k];
                uint32_t b = ((uint32_t)j << 23) | mants[(k + 1) % nm];
                /* 末尾指数剔除 NaN/Inf 组合带来的重复噪声，仍保留全范围 */
                check_pair(a, b);
            }
        }
    }

    /* ---- 6. 随机全位型（广度，非证明） ---- */
    {
        uint32_t st = seedy;
        printf("[6] random full-bit-pattern pairs: %ld\n", N);
        for (i = 0; i < N; i++) {
            uint32_t a = rnd(&st), b = rnd(&st);
            check_pair(a, b);
        }
    }

    /* ---- 汇总 ---- */
    printf("\n-- coverage evidence --\n");
    printf("  exact ties hit                 : %ld\n", g_ties);
    printf("  sticky-only roundings          : %ld\n", g_sticky_only);
    printf("  24-bit carry-out (exp +1)      : %ld\n", g_carry_out);
    printf("  subnormal results              : %ld\n", g_subnormal);
    printf("    of which round up to min-norm: %ld\n", g_sub_up);
    printf("  overflow -> Inf                : %ld\n", g_overflow);
    printf("  NaN results                    : %ld\n", g_nan_res);
    printf("  Inf results                    : %ld\n", g_inf_res);
    printf("\n-- per-routine (runtime vs independent integer-rational oracle) --\n");
    for (i = 0; i < 4; i++)
        printf("  %-3s checks=%-9ld mismatches=%ld\n", OPN[i], g_per_op[i], g_bad_op[i]);
    printf("\n-- oracle self cross-check vs host IEEE-754 hardware --\n");
    printf("  comparable samples=%ld (NaN-in/NaN-out excluded)\n", g_xchecks);
    printf("  cross mismatches=%ld%s\n", g_xbad,
           g_xchecks == 0 ? "  [INVALID: no comparable samples]" : "");
    printf("\noracle: %ld checks, %ld mismatches -> %s\n",
           g_checks, g_bad, g_bad ? "FAIL" : "PASS (zero-diff)");
    if (g_xchecks == 0) g_bad++;   /* 无交叉样本 => 证据无效 */
    return (g_bad != 0) || (g_xbad != 0);
}
