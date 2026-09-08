/*===-- mcs251_float_arith.c ----------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— IEEE-754 binary32 运算实现。
 *
 * 红线：纯 C；无 64 位类型（int64_t/long long）；无浮点运算符；无内联汇编；
 * 无 pragma；无静态/全局可变状态（全部自动变量，重入安全）。
 *
 * 实现依据：IEEE 754-2008 标准 + 公开算法描述（对齐移位、恢复余数尾数除法、
 * 舍入到偶数）。未逐行参照 SDCC/compiler-rt/libgcc/Berkeley softfloat 文本。
 *
 * NaN 传递规则（IEEE 754）：运算输入含 NaN 时输出 NaN（qNan 规范化）；
 * Inf 参与运算按标准表（Inf+Inf=Inf、Inf-Inf=NaN、Inf*0=NaN、Inf/Inf=NaN、
 * 0/0=NaN、x/0：x!=0 时 Inf、x==0 时 NaN）。
 *
 * 舍入模式：固定舍入到最近偶数（roundTiesToEven），这是默认且唯一支持模式。
 */

#include "mcs251_float.h"
#include "mcs251_bitutil.h"

/* ------------------------------------------------------------------ */
/* 内部辅助：规范化 NaN 输出为 canonical qNan，保留符号位             */
/* ------------------------------------------------------------------ */
static uint32_t propagate_nan(uint32_t a, uint32_t b)
{
    /* 若 a 是 NaN，返回 a 的规范化（清除静默位之外的尾数，设 qNan） */
    if (F32_IS_NAN(a)) {
        return (a & F32_SIGN_MASK) | F32_QNAN;
    }
    if (F32_IS_NAN(b)) {
        return (b & F32_SIGN_MASK) | F32_QNAN;
    }
    return F32_QNAN;
}

/* ------------------------------------------------------------------ */
/* __negsf2：取负，翻转符号位（单参）                                  */
/* ------------------------------------------------------------------ */
uint32_t _negsf2(uint32_t a)
{
    /* NaN/Inf/零/正规/非正规统一处理：仅翻转符号位。
     * 注意：+0 取负得 -0，-0 取负得 +0（IEEE 754 要求）。 */
    return a ^ F32_SIGN_MASK;
}

/* ------------------------------------------------------------------ */
/* __extendsfdf2 / __truncdfsf2：double=f32 裁定下恒等（单参）        */
/* ------------------------------------------------------------------ */
uint32_t _extendsfdf2(uint32_t a)
{
    return a;
}

uint32_t _truncdfsf2(uint32_t a)
{
    return a;
}

/* ------------------------------------------------------------------ */
/* __addsf3：f32 加法                                                  */
/* 第二参数 b 走 __addsf3_PARM_2（4B 大端）。                          */
/*                                                                     */
/* 算法（roundTiesToEven，全程精确舍入）：                             */
/*   1. 尾数左移 3 位预留 G(guard)/R/S 粘位：编码 v = sig24*8 + grs，   */
/*      bit 26 = 规格化隐含位，bit 2 = guard，bit 1..0 = 其余低位。    */
/*   2. 低指数操作数右移对齐；被移出的低位记为 jam（sticky 标志），    */
/*      q 只保留精确部分（不做 OR 拼接，避免污染 q 的奇偶性）。        */
/*   3. 同号相加：v = big + q，进位（bit 27）右移 1 位并保 jam。       */
/*      异号相减：真差 = v - θ（θ∈[0,1) 为对齐丢弃部分），故 big>q 时 */
/*      整数部分为 v-1（jam 时减 1 修正借位）；q>big 时真差 = v + θ，  */
/*      无需修正。big==q 且 jam 时真差量级 < 1 单位，直接入零。        */
/*   4. 减法结果左移规范化（sticky 与刻度无关，jam 标志保持）；        */
/*      指数降到 1 即进入非正规域（指数域 0 的编码刻度与 E=1 相同）。  */
/*   5. 舍入：guard=1 且（低位非零或 LSB=1）进位；非正规舍入升到      */
/*      bit 23 自然成为最小正规数（指数域 1、尾数 0）。                */
/* ------------------------------------------------------------------ */
uint32_t _addsf3(uint32_t a, uint32_t b)
{
    uint32_t sa, sb, ea, eb, ma, mb;
    int32_t exp_r;
    uint32_t sign_r;
    uint32_t big, q, jam, v;
    uint32_t d;

    sa = F32_SIGN(a);
    sb = F32_SIGN(b);
    ea = F32_EXP(a);
    eb = F32_EXP(b);
    ma = F32_MANT(a);
    mb = F32_MANT(b);

    /* NaN 处理 */
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return propagate_nan(a, b);
    }

    /* Inf 处理 */
    if (F32_IS_INF(a)) {
        if (F32_IS_INF(b)) {
            if (sa == sb) {
                return a;  /* 同号 Inf + Inf = Inf */
            }
            return F32_NEG_QNAN;  /* Inf - Inf = NaN (本库约定 -NaN) */
        }
        return a;  /* Inf + finite = Inf */
    }
    if (F32_IS_INF(b)) {
        return b;
    }

    /* 零处理 */
    if (F32_IS_ZERO(a)) {
        /* 0 + 0：符号按 IEEE 754 6.3（+0 除非两者都是 -0） */
        if (F32_IS_ZERO(b)) {
            if (sa == 1u && sb == 1u) return F32_NEG_ZERO;
            return F32_POS_ZERO;
        }
        return b;  /* 0 + b = b（含 -0 符号保留） */
    }
    if (F32_IS_ZERO(b)) {
        return a;
    }

    /* 加入隐含位；非正规有效指数为 1 */
    if (ea != 0u) {
        ma |= F32_HIDDEN_BIT;
    } else {
        ea = 1u;
    }
    if (eb != 0u) {
        mb |= F32_HIDDEN_BIT;
    } else {
        eb = 1u;
    }

    /* 对齐：大指数一侧（精确、不移位）为 big，小指数一侧右移为 q。 */
    if (ea >= eb) {
        exp_r = (int32_t)ea;
        d = ea - eb;
        big = ma << 3;
        q = mb << 3;
    } else {
        exp_r = (int32_t)eb;
        d = eb - ea;
        big = mb << 3;
        q = ma << 3;
    }
    if (d == 0u) {
        jam = 0u;  /* 同指数：完全精确 */
    } else if (d >= 27u) {
        q = 0u;   /* 全部移出（mb 非零保证 jam=1） */
        jam = 1u;
    } else {
        uint32_t mask = (1u << d) - 1u;
        jam = (q & mask) ? 1u : 0u;
        q = q >> d;
    }

    if (sa == sb) {
        /* 同号：绝对值相加（真和 = v + θ，θ<1 单位，sticky=jam） */
        v = big + q;
        sign_r = sa;
        if (v & 0x08000000u) {  /* 进位到 bit 27：右移 1 位保 jam */
            v = (v >> 1) | (v & 1u);
            exp_r += 1;
            if (exp_r >= 0xFF) {
                return F32_PACK(sign_r, 0xFFu, 0u);  /* Inf */
            }
        }
    } else {
        /* 异号：绝对值相减 */
        if (big > q) {
            v = big - q;
            sign_r = (ea >= eb) ? sa : sb;
            if (jam) {
                v -= 1u;  /* 真差 = v - θ 的整数部分是 v-1（借位） */
            }
        } else if (big < q) {
            v = q - big;
            sign_r = (ea >= eb) ? sb : sa;  /* 真差 = v + θ，无需修正 */
        } else {
            /* big == q：jam 时真差 = -θ（量级 < 1 单位，入带符号零） */
            if (jam) {
                return F32_PACK((ea >= eb) ? sb : sa, 0u, 0u);
            }
            return F32_POS_ZERO;  /* 精确抵消（roundTiesToEven 下 x-x=+0） */
        }
        /* 左移规范化直到隐含位（bit 26）就位；指数降到 1 即非正规域。
         * sticky 与刻度无关：jam 标志在舍入处统一使用。 */
        while (v != 0u && (v & 0x04000000u) == 0u && exp_r > 1) {
            v = v << 1;
            exp_r -= 1;
        }
    }

    /* 舍入到偶数：sig24 = v>>3，guard = bit 2，低位 = bit 1..0 ∨ jam。
     * 隐含位未就位（v < 2^26）只可能出现在 exp_r == 1，此时尾数按
     * 指数域 0（非正规）打包；舍入进位到 bit 23 即最小正规数。 */
    {
        uint32_t mant = v >> 3;
        uint32_t g = (v >> 2) & 1u;
        uint32_t st = ((v & 3u) != 0u || jam != 0u) ? 1u : 0u;

        if (g && (st || (mant & 1u))) {
            mant += 1u;
            if (mant & 0x01000000u) {  /* 进位出 24 位 */
                mant >>= 1;
                exp_r += 1;
                if (exp_r >= 0xFF) {
                    return F32_PACK(sign_r, 0xFFu, 0u);  /* Inf */
                }
            }
        }
        if (mant & 0x00800000u) {
            return F32_PACK(sign_r, (uint32_t)exp_r, mant);
        }
        return F32_PACK(sign_r, 0u, mant);
    }
}

/* ------------------------------------------------------------------ */
/* __subsf3：减法 = 加法 + 取负第二操作数                              */
/* ------------------------------------------------------------------ */
uint32_t _subsf3(uint32_t a, uint32_t b)
{
    return _addsf3(a, b ^ F32_SIGN_MASK);
}

/* ------------------------------------------------------------------ */
/* __mulsf3：f32 乘法                                                  */
/* 第二参数 b 走 __mulsf3_PARM_2（4B 大端）。                          */
/*                                                                     */
/* 算法（roundTiesToEven）：                                           */
/*   1. 非正规输入先左移规范化（MSB 到 bit 23），指数相应下调。        */
/*   2. 24x24 -> 48 位乘积拆 12 位半字节（12x12 < 2^24 免 64 位）：    */
/*      P = p0<<24 + (p1+p2)<<12 + p3，取 hi = P>>24（精确）、         */
/*      lo = P&0xFFFFFF（精确），无信息损失。                          */
/*   3. v = P>>20（含隐含位与 guard），P>=2^47 时右移 1 位进位。       */
/*   4. 上溢 -> Inf；exp<=0 右移 (4-exp) 入非正规域（guard/sticky     */
/*      从移出位收集，舍入可升最小正规数）；下溢尽移出 -> 带符号零。   */
/*   5. 舍入到偶数同加法。                                             */
/* ------------------------------------------------------------------ */
uint32_t _mulsf3(uint32_t a, uint32_t b)
{
    uint32_t sa, sb, ea, eb, ma, mb;
    int32_t exp_r;
    uint32_t sign_r;
    uint32_t v;

    sa = F32_SIGN(a);
    sb = F32_SIGN(b);
    ea = F32_EXP(a);
    eb = F32_EXP(b);
    ma = F32_MANT(a);
    mb = F32_MANT(b);

    sign_r = sa ^ sb;

    /* NaN */
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return propagate_nan(a, b);
    }

    /* Inf */
    if (F32_IS_INF(a)) {
        if (F32_IS_ZERO(b)) return F32_NEG_QNAN;  /* Inf * 0 = NaN */
        return F32_PACK(sign_r, 0xFFu, 0u);    /* Inf * finite = Inf */
    }
    if (F32_IS_INF(b)) {
        if (F32_IS_ZERO(a)) return F32_NEG_QNAN;
        return F32_PACK(sign_r, 0xFFu, 0u);
    }

    /* 零 */
    if (F32_IS_ZERO(a) || F32_IS_ZERO(b)) {
        return F32_PACK(sign_r, 0u, 0u);  /* signed zero */
    }

    /* 隐含位；非正规输入左移规范化，有效指数可降到 1-sh（<0）。
     * volatile 阻止 clang -O2 把 msb 查找与移位组合为 ctlz。 */
    {
        int32_t eai = (int32_t)ea;
        int32_t ebi = (int32_t)eb;

        if (ea != 0u) {
            ma |= F32_HIDDEN_BIT;
        } else {
            volatile uint32_t vma = ma;
            uint32_t msb = mcs251_msb32(vma);
            uint32_t sh = 24u - msb;
            ma = vma << sh;
            eai = 1 - (int32_t)sh;
        }
        if (eb != 0u) {
            mb |= F32_HIDDEN_BIT;
        } else {
            volatile uint32_t vmb = mb;
            uint32_t msb = mcs251_msb32(vmb);
            uint32_t sh = 24u - msb;
            mb = vmb << sh;
            ebi = 1 - (int32_t)sh;
        }
        exp_r = eai + ebi - F32_EXP_BIAS;
    }

    /* 24x24 = 48 位乘积：12 位 halves 拆分，全程 32 位安全：
     *  p0 = ah*bh <= 0xFFF^2 < 2^24；mid = ah*bl + al*bh < 2^25；
     *  lo = ((mid&0xFFF)<<12) + p3 < 2^25（P 的低 24 位 + 进位）；
     *  hi = p0 + (mid>>12) + (lo>>24) = P>>24（精确，无溢出：
     *  2^24 + 2^13 + 1 < 2^32）。 */
    {
        uint32_t ah = ma >> 12, al = ma & 0xFFFu;
        uint32_t bh = mb >> 12, bl = mb & 0xFFFu;
        uint32_t p0 = ah * bh;
        uint32_t mid = ah * bl + al * bh;
        uint32_t p3 = al * bl;
        uint32_t lo = ((mid & 0xFFFu) << 12) + p3;
        uint32_t hi = p0 + (mid >> 12) + (lo >> 24);

        lo &= 0x00FFFFFFu;
        uint32_t jam = (lo & 0x001FFFFFu) ? 1u : 0u;  /* P bit 20..0 非零 */

        /* v = P>>20：bit 27/26 为隐含位候选，bit 2 = guard。
         * P ∈ [2^46, 2^48)，故 v >= 2^25，规范化仅需处理 bit 27 进位。 */
        v = (hi << 4) | (lo >> 20);
        if (v & 0x08000000u) {  /* P >= 2^47：隐含位在 bit 27 */
            v = (v >> 1) | (v & 1u);
            exp_r += 1;
        }

        /* 上溢（舍入前判定：舍入至多再 +1） */
        if (exp_r >= 0xFF) {
            return F32_PACK(sign_r, 0xFFu, 0u);
        }

        if (exp_r <= 0) {
            /* 下溢/非正规：mantissa = v * 2^(exp_r-4)，右移 (4-exp_r) 位。
             * guard = 被移出部分的最高位，sticky = 其余移出位 ∨ jam。 */
            int32_t n = 4 - exp_r;  /* >= 4 */
            if (n >= 28) {
                return F32_PACK(sign_r, 0u, 0u);  /* 全部移出，入零 */
            }
            {
                uint32_t nu = (uint32_t)n;  /* 4..27 */
                uint32_t g = (v >> (nu - 1u)) & 1u;
                uint32_t mask = (1u << (nu - 1u)) - 1u;
                uint32_t st = ((v & mask) != 0u || jam != 0u) ? 1u : 0u;
                uint32_t mant = v >> nu;  /* < 2^23：非正规域 */

                if (g && (st || (mant & 1u))) {
                    mant += 1u;
                    if (mant & 0x00800000u) {
                        return F32_PACK(sign_r, 1u, 0u);  /* 舍入升最小正规 */
                    }
                }
                return F32_PACK(sign_r, 0u, mant);
            }
        }

        /* 正规数舍入到偶数 */
        {
            uint32_t mant = v >> 3;  /* bit 26 隐含位 -> bit 23 */
            uint32_t g = (v >> 2) & 1u;
            uint32_t st = ((v & 3u) != 0u || jam != 0u) ? 1u : 0u;

            if (g && (st || (mant & 1u))) {
                mant += 1u;
                if (mant & 0x01000000u) {  /* 进位出 24 位 */
                    mant >>= 1;
                    exp_r += 1;
                    if (exp_r >= 0xFF) {
                        return F32_PACK(sign_r, 0xFFu, 0u);
                    }
                }
            }
            return F32_PACK(sign_r, (uint32_t)exp_r, mant);
        }
    }
}

/* ------------------------------------------------------------------ */
/* __divsf3：f32 除法                                                  */
/* ------------------------------------------------------------------ */
uint32_t _divsf3(uint32_t a, uint32_t b)
{
    uint32_t sa, sb, ma, mb;
    int32_t ea, eb, exp_r;
    uint32_t sign_r;
    uint32_t rem, quot;
    uint32_t i;

    sa = F32_SIGN(a);
    sb = F32_SIGN(b);
    ma = F32_MANT(a);
    mb = F32_MANT(b);

    sign_r = sa ^ sb;

    /* NaN */
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return propagate_nan(a, b);
    }

    /* Inf */
    if (F32_IS_INF(a)) {
        if (F32_IS_INF(b)) return F32_NEG_QNAN;  /* Inf/Inf = NaN */
        return F32_PACK(sign_r, 0xFFu, 0u);  /* Inf/finite = Inf */
    }
    if (F32_IS_INF(b)) {
        return F32_PACK(sign_r, 0u, 0u);  /* finite/Inf = 0 */
    }

    /* 除零 */
    if (F32_IS_ZERO(b)) {
        if (F32_IS_ZERO(a)) return F32_NEG_QNAN;  /* 0/0 = NaN */
        return F32_PACK(sign_r, 0xFFu, 0u);  /* x/0 = Inf */
    }
    if (F32_IS_ZERO(a)) {
        return F32_PACK(sign_r, 0u, 0u);  /* 0/x = signed 0 */
    }

    /* 提取尾数和指数，规范化非正规输入（左移至 hidden bit 就位）。
     * 用 volatile 阻止 clang -O2 把 msb 查找与后续移位组合为 ctlz。 */
    ea = (int32_t)F32_EXP(a);
    if (ea != 0) {
        ma = ma | F32_HIDDEN_BIT;
    } else {
        /* 非正规 a：左移使 MSB 到 bit 23，调整有效指数 */
        volatile uint32_t vma = ma;
        uint32_t msb = mcs251_msb32(vma);  /* 1-based position */
        uint32_t sh = 24u - msb;
        ma = vma << sh;
        ea = 1 - (int32_t)sh;
    }

    eb = (int32_t)F32_EXP(b);
    if (eb != 0) {
        mb = mb | F32_HIDDEN_BIT;
    } else {
        volatile uint32_t vmb = mb;
        uint32_t msb = mcs251_msb32(vmb);
        uint32_t sh = 24u - msb;
        mb = vmb << sh;
        eb = 1 - (int32_t)sh;
    }

    /* exp_result = (ea - 127) - (eb - 127) + 127 = ea - eb + 127 */
    exp_r = ea - eb + F32_EXP_BIAS;

    /* 规范化被除数：确保 ma >= mb，使商在 [1.0, 2.0)。
     * ma、mb 此时都有 hidden bit（bit 23 置位），范围 [2^23, 2^24)。
     * 若 ma < mb，左移 ma 一位，exp_r 减 1。
     * 左移后 ma 最多 25 位，仍在 uint32_t 范围内。
     * 因 ma/mb ∈ [0.5, 2)，左移一次后 ma/mb ∈ [1, 2)，保证 ma >= mb。 */
    if (ma < mb) {
        ma = ma << 1;
        exp_r = exp_r - 1;
    }

    /* 恢复余数长除法：产生 25 位商 + 精确余数。
     *
     * ma >= mb 保证第一个商位为 1（hidden bit）。
     * rem = ma - mb（余数，0 <= rem < mb，因为 ma < 2*mb）。
     * 然后 24 轮：每轮 rem <<= 1（移入 0），比较 rem 与 mb，
     * 若 >= 则减去并设商位。这等价于在 ma 后补 24 个零再除。
     *
     * 25 位商：bit 24 = hidden（1），bits 23..1 = 23 位尾数，bit 0 = guard。
     * rem 是精确余数（0 <= rem < mb），用于 sticky 判定。
     *
     * rem 不会溢出：每轮减法后 rem < mb < 2^24，左移后 rem < 2^25。
     * quot 不会溢出：25 位最大 0x1FFFFFF，远在 uint32_t 范围内。 */
    quot = 1u;
    rem = ma - mb;

    for (i = 0u; i < 24u; ++i) {
        rem = rem << 1;
        quot = quot << 1;
        if (rem >= mb) {
            rem = rem - mb;
            quot = quot | 1u;
        }
    }

    /* 舍入到偶数（roundTiesToEven）。
     * 24 位结果 = quot >> 1（bits 24..1，hidden + 23 mantissa）。
     * guard = quot & 1（bit 0，半个 ULP）。
     * sticky = (rem != 0)（精确余数是否有残留）。
     *
     * 舍入规则：guard=1 且 (sticky=1 或结果 LSB=1) 时向上舍入。
     * guard=1 且 sticky=0 且 LSB=0 时向下舍入（tie rounds to even）。
     * guard=0 时向下舍入（截断）。 */
    {
        uint32_t guard = quot & 1u;
        uint32_t sticky = (rem != 0u) ? 1u : 0u;
        uint32_t mant24 = quot >> 1;  /* 24 位（hidden + 23 mantissa） */

        /* 上溢检查（在舍入前，基于未舍入的指数） */
        if (exp_r >= 0xFF) {
            return F32_PACK(sign_r, 0xFFu, 0u);  /* Inf */
        }

        /* 非正规 / 下溢处理（exp_r <= 0）：
         * 需要把 25 位商右移 (2 - exp_r) 位，使指数归零为 subnormal 域。
         * mant_sub = quot >> shift，guard_sub 和 sticky_sub 从移出位收集。 */
        if (exp_r <= 0) {
            int32_t shift = 2 - exp_r;  /* >= 2 */
            if (shift >= 26) {
                /* 整个商被移出，guard=0，结果为零 */
                return F32_PACK(sign_r, 0u, 0u);
            }
            {
                uint32_t shift_u = (uint32_t)shift;
                uint32_t mant_sub = quot >> shift_u;
                uint32_t guard_sub = (quot >> (shift_u - 1u)) & 1u;
                uint32_t sticky_all = sticky;

                /* 收集 guard 以下的所有位为 sticky */
                if (shift_u >= 2u) {
                    uint32_t below_mask = (1u << (shift_u - 1u)) - 1u;
                    if (quot & below_mask) sticky_all = 1u;
                }

                /* 舍入到偶数 */
                if (guard_sub && (sticky_all || (mant_sub & 1u))) {
                    mant_sub = mant_sub + 1u;
                    /* 舍入进位到 bit 23（hidden）→ 最小正规数 */
                    if (mant_sub & F32_HIDDEN_BIT) {
                        return F32_PACK(sign_r, 1u, 0u);
                    }
                }
                return F32_PACK(sign_r, 0u, mant_sub & F32_MANT_MASK);
            }
        }

        /* 正规数舍入 */
        if (guard && (sticky || (mant24 & 1u))) {
            mant24 = mant24 + 1u;
            /* 进位到 bit 24：右移，指数加 1 */
            if (mant24 & 0x01000000u) {
                mant24 = mant24 >> 1;
                exp_r = exp_r + 1;
                if (exp_r >= 0xFF) {
                    return F32_PACK(sign_r, 0xFFu, 0u);  /* Inf */
                }
            }
        }
        return F32_PACK(sign_r, (uint32_t)exp_r, mant24 & F32_MANT_MASK);
    }
}

/* ------------------------------------------------------------------ */
/* __floatsisf：int32 -> float（单参）                                 */
/*                                                                     */
/* 注意：__floatdisf / __fixsfdi（DI = 64 位整数 <-> float）本库刻意   */
/* 不提供：本链当前无 i64 支持，不能用 int32 别名占用 compiler-rt      */
/* 标准名。double 宽度待编译器侧裁定后 revisiting（若未来 double 仍    */
/* 为 32 位浮点且 i64 落地，再按标准语义补齐）。                       */
/* ------------------------------------------------------------------ */
uint32_t _floatsisf(int32_t a)
{
    uint32_t sign, mag;
    uint32_t exp, mant;
    uint32_t guard, sticky;

    if (a == 0) {
        return F32_POS_ZERO;
    }

    sign = (a < 0) ? 1u : 0u;
    /* 取绝对值：用无符号域避免 INT32_MIN UB */
    mag = (uint32_t)a;
    if (sign) {
        mag = (uint32_t)(0u - mag);
    }

    /* 找最高设置位（用 de Bruijn 查表，避免 clang 综合出后端不支持的 ctlz）。
     * 注意：不使用 mag << lz 模式（clang 会把它与 msb 查找组合识别为 ctlz）。
     * 直接用 msb 值计算 exp，并用右移提取尾数。 */
    {
        uint32_t msb = mcs251_msb32(mag);  /* 1-based，bit position+1 */
        /* exp = bias + (msb-1)：值 = 1.xxx * 2^(msb-1)，float exp = msb-1+bias */
        exp = F32_EXP_BIAS + msb - 1u;
        /* 尾数：hidden bit 在 bit (msb-1)。
         * 我们需要把它对齐到 bit 23（hidden bit 标准位置）。
         * 如果 msb-1 > 23：右移 (msb-1-23) 位，guard/sticky 从移出位收集。
         * 如果 msb-1 < 23：左移 (23-(msb-1)) 位（无精度损失）。
         * 用 volatile 阻止 clang 把移位计数与 msb 组合成 ctlz。 */
        volatile uint32_t vmsb = msb;
        uint32_t bit_pos = vmsb - 1u;  /* hidden bit 的位置（0-based） */
        if (bit_pos >= 23u) {
            uint32_t sh = bit_pos - 23u;
            /* 右移：guard = sh-1 位之上的位，sticky = 低于 guard 的位 */
            if (sh == 0u) {
                mant = mag;
                guard = 0u;
                sticky = 0u;
            } else if (sh <= 7u) {
                /* 提取 guard 和 sticky */
                uint32_t guard_mask = 1u << (sh - 1u);
                uint32_t sticky_mask = guard_mask - 1u;
                guard = (mag & guard_mask) ? 1u : 0u;
                sticky = (mag & sticky_mask) ? 1u : 0u;
                mant = mag >> sh;
            } else {
                /* 大移位：只有 sticky */
                uint32_t sticky_mask = (1u << sh) - 1u;
                sticky = (mag & sticky_mask) ? 1u : 0u;
                guard = (mag >> (sh - 1u)) & 1u;
                mant = mag >> sh;
            }
        } else {
            /* 左移：无精度损失 */
            uint32_t sh = 23u - bit_pos;
            mant = mag << sh;
            guard = 0u;
            sticky = 0u;
        }
    }

    /* 舍入到偶数 */
    if (guard && (sticky || (mant & 1u))) {
        mant = mant + 1u;
        if (mant & 0x01000000u) {
            mant = mant >> 1;
            exp = exp + 1u;
        }
    }

    if (exp >= 0xFFu) {
        return F32_PACK(sign, 0xFFu, 0u);  /* 上溢到 Inf */
    }

    return F32_PACK(sign, exp, mant & F32_MANT_MASK);
}

/* ------------------------------------------------------------------ */
/* __fixsfsi：float -> int32（单参）                                   */
/* 向零截断（C cast 语义）。                                           */
/* __fixsfdi 同上不提供（见 _floatsisf 处注释）。                      */
/* ------------------------------------------------------------------ */
uint32_t _fixsfsi(uint32_t a)
{
    uint32_t sign, exp, mant;
    uint32_t result;

    /* NaN -> 0（实现定义，选择安全值） */
    if (F32_IS_NAN(a)) {
        return 0u;
    }

    /* Inf -> 饱和到 INT32_MAX/INT32_MIN（实现定义） */
    if (F32_IS_INF(a)) {
        sign = F32_SIGN(a);
        return sign ? 0x80000000u : 0x7FFFFFFFu;
    }

    sign = F32_SIGN(a);
    exp = F32_EXP(a);
    mant = F32_MANT(a);

    /* 零 */
    if (F32_IS_ZERO(a)) {
        return 0u;
    }

    /* 非正规：太小，截断为 0 */
    if (exp == 0u) {
        return 0u;
    }

    /* 加入隐含位 */
    mant = mant | F32_HIDDEN_BIT;

    /* 实际指数 = exp - bias */
    /* 如果 exp - bias < 0，结果为 0 */
    if (exp < F32_EXP_BIAS) {
        return 0u;
    }

    /* 移位量 = 23 - (exp - bias) = 23 - exp + 127 = 150 - exp */
    {
        uint32_t unbiased = exp - F32_EXP_BIAS;
        if (unbiased >= 31u) {
            /* 溢出：饱和 */
            return sign ? 0x80000000u : 0x7FFFFFFFu;
        }
        if (unbiased <= 23u) {
            result = mant >> (23u - unbiased);
        } else {
            result = mant << (unbiased - 23u);
        }
    }

    /* 应用符号 */
    if (sign) {
        result = (uint32_t)(0u - result);
    }

    return result;
}
