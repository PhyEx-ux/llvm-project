/*===-- mcs251_float_div.c ------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— 除法单元（__divsf3）。
 *
 * 本文件只定义 __divsf3，与加减、乘法单元分离，便于按例程链接。
 *
 * 恢复余数长除法（25 位商 + 精确余数），位域编解码走 16 位半字以避免
 * 后端把 32 位常量右移展开成昂贵的 RRC 链。非正规输入规范化调用
 * mcs251_msb32（bitutil 单元提供，独立 TU 防 clang 综合 ctlz）。
 */

#include "mcs251_float.h"
#include "mcs251_float_limb.h"
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
/* __divsf3：f32 除法                                                  */
/*                                                                     */
/* 恢复余数长除法核心与重写前逐位一致（25 位商 + 精确余数），仅把两处   */
/* 32 位 >>23 的指数提取换成 16 位半字读取（各约 483B -> ~38B）。       */
/* 商/尾数的拼装同理避免 32 位移位。                                   */
/* ------------------------------------------------------------------ */
uint32_t _divsf3(uint32_t a, uint32_t b)
{
    volatile f32_hl ua, ub;
    uint32_t sa, sb, ma, mb;
    int32_t ea, eb, exp_r;
    uint32_t sign_r;
    uint32_t rem, quot;
    uint32_t i;

    sa = F32_SIGN(a);
    sb = F32_SIGN(b);
    ma = F32_MANT(a);
    mb = F32_MANT(b);

    /* exp = (hi16 >> 7) & 0xff：读高半字避免 32 位 >>23 的 RRC 展开链。 */
    ua.w = a; ub.w = b;
    ea = (int32_t)((uint16_t)(ua.h[F32_HI16] >> 7) & 0xffu);
    eb = (int32_t)((uint16_t)(ub.h[F32_HI16] >> 7) & 0xffu);

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
     * 若 ma < mb，左移 ma 一位，exp_r 减 1。 */
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
     * rem 是精确余数（0 <= rem < mb），用于 sticky 判定。 */
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
     * sticky = (rem != 0)（精确余数是否有残留）。 */
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

