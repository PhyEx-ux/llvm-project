/*===-- mcs251_float_addsub.c ---------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— 加减法单元（__addsf3 / __subsf3）。
 *
 * 本文件只定义 __addsf3 / __subsf3，与乘法、除法单元分离，使链接器可以
 * 按程序未解析符号单独拉取（MCS251 lld 无 --gc-sections，故用多 TU 拆分
 * 而非 -ffunction-sections 达成按例程链接）。
 *
 * 位域编解码走 16 位半字，见 mcs251_float_limb.h 的背景说明。
 */

#include "mcs251_float.h"
#include "mcs251_float_limb.h"

/* ------------------------------------------------------------------ */
/* __addsf3：f32 加法                                                  */
/* 第二参数 b 走 __addsf3_PARM_2（4B 大端）。                          */
/*                                                                     */
/* 算法（roundTiesToEven，全程精确舍入）——与重写前逐位一致：           */
/*   1. 尾数左移 3 位预留 G(guard)/R/S 粘位：编码 v = sig24*8 + grs，   */
/*      bit 26 = 规格化隐含位，bit 2 = guard，bit 1..0 = 其余低位。     */
/*   2. 低指数操作数右移对齐；被移出的低位记为 jam（sticky 标志），     */
/*      q 只保留精确部分（不做 OR 拼接，避免污染 q 的奇偶性）。         */
/*   3. 同号相加：v = big + q，进位（bit 27）右移 1 位并保 jam。       */
/*      异号相减：真差 = v - θ（θ∈[0,1) 为对齐丢弃部分），故 big>q 时 */
/*      整数部分为 v-1（jam 时减 1 修正借位）；q>big 时真差 = v + θ，  */
/*      无需修正。big==q 且 jam 时真差量级 < 1 单位，直接入零。        */
/*   4. 减法结果左移规范化（sticky 与刻度无关，jam 标志保持）；        */
/*      指数降到 1 即进入非正规域（指数域 0 的编码刻度与 E=1 相同）。  */
/*   5. 舍入：guard=1 且（低位非零或 LSB=1）进位；非正规舍入升到      */
/*      bit 23 自然成为最小正规数（指数域 1、尾数 0）。                */
/*                                                                     */
/* 与重写前实现的差异仅在位域存取（16 位半字 + 联合体），数值路径      */
/* 逐条对应，故输出位精确相同。                                        */
/* ------------------------------------------------------------------ */
uint32_t _addsf3(uint32_t a, uint32_t b)
{
    volatile f32_hl ua, ub, ur, um;
    uint16_t ha, hb, la, lb;
    uint16_t sa, sb, sign_r, ea, eb;
    uint16_t mah, mbh, bh, bl, qh, ql, jam, vh, vl, d, big_is_a;
    uint16_t mhi, mlo, g, st;
    int32_t exp_r;

    ua.w = a; ub.w = b;
    ha = ua.h[F32_HI16]; la = ua.h[F32_LO16];
    hb = ub.h[F32_HI16]; lb = ub.h[F32_LO16];

    sa = (uint16_t)(ha >> 15);
    sb = (uint16_t)(hb >> 15);
    mah = (uint16_t)(ha & 0x7fu);
    mbh = (uint16_t)(hb & 0x7fu);
    ea = (uint16_t)((uint16_t)(ha >> 7) & 0xffu);
    eb = (uint16_t)((uint16_t)(hb >> 7) & 0xffu);

    /* NaN 处理：规范化为 canonical qNaN，符号取第一个 NaN 操作数 */
    if (ea == 0xffu && (mah | la) != 0u)
        return ((uint32_t)(ha >> 15) << 31) | F32_QNAN;
    if (eb == 0xffu && (mbh | lb) != 0u)
        return ((uint32_t)(hb >> 15) << 31) | F32_QNAN;

    /* Inf 处理 */
    if (ea == 0xffu) {
        if (eb == 0xffu) {
            if (sa == sb) {
                return a;  /* 同号 Inf + Inf = Inf */
            }
            return F32_NEG_QNAN;  /* Inf - Inf = NaN (本库约定 -NaN) */
        }
        return a;  /* Inf + finite = Inf */
    }
    if (eb == 0xffu) {
        return b;
    }

    /* 零处理 */
    if ((mah | la) == 0u && ea == 0u) {
        /* 0 + 0：符号按 IEEE 754 6.3（+0 除非两者都是 -0） */
        if ((mbh | lb) == 0u && eb == 0u) {
            if (sa == 1u && sb == 1u) return F32_NEG_ZERO;
            return F32_POS_ZERO;
        }
        return b;  /* 0 + b = b（含 -0 符号保留） */
    }
    if ((mbh | lb) == 0u && eb == 0u) {
        return a;
    }

    /* 加入隐含位；非正规有效指数为 1 */
    exp_r = (int32_t)ea;
    if (exp_r != 0) {
        mah = (uint16_t)(mah | 0x80u);
    } else {
        exp_r = 1;
    }
    {
        int32_t ebi = (int32_t)eb;
        if (ebi != 0) {
            mbh = (uint16_t)(mbh | 0x80u);
        } else {
            ebi = 1;
        }
        /* 对齐：大指数一侧（精确、不移位）为 big，小指数一侧右移为 q。 */
        if (exp_r >= ebi) {
            d = (uint16_t)(exp_r - ebi);
            big_is_a = 1u;
            bh = (uint16_t)((uint16_t)(mah << 3) | (uint16_t)(la >> 13));
            bl = (uint16_t)(la << 3);
            qh = (uint16_t)((uint16_t)(mbh << 3) | (uint16_t)(lb >> 13));
            ql = (uint16_t)(lb << 3);
        } else {
            d = (uint16_t)(ebi - exp_r);
            big_is_a = 0u;
            bh = (uint16_t)((uint16_t)(mbh << 3) | (uint16_t)(lb >> 13));
            bl = (uint16_t)(lb << 3);
            qh = (uint16_t)((uint16_t)(mah << 3) | (uint16_t)(la >> 13));
            ql = (uint16_t)(la << 3);
            exp_r = ebi;
        }
    }

    if (d == 0u) {
        jam = 0u;  /* 同指数：完全精确 */
    } else if (d >= 27u) {
        qh = 0u; ql = 0u;  /* 全部移出（mb 非零保证 jam=1） */
        jam = 1u;
    } else {
        /* 统一走一条 32 位变量右移（后端展开为移位循环，仅一份代码）；
         * 移出的低位用掩码收集为 jam。 */
        volatile uint32_t vq = ((uint32_t)qh << 16) | (uint32_t)ql;
        uint32_t vmask = (1u << d) - 1u;
        jam = (uint16_t)((vq & vmask) != 0u);
        vq = vq >> d;
        um.w = vq;
        qh = um.h[F32_HI16];
        ql = um.h[F32_LO16];
    }

    if (sa == sb) {
        /* 同号：绝对值相加（真和 = v + θ，θ<1 单位，sticky=jam） */
        vl = (uint16_t)(bl + ql);
        vh = (uint16_t)((uint16_t)(bh + qh) + (uint16_t)(vl < bl ? 1u : 0u));
        sign_r = sa;
        if (vh & 0x0800u) {  /* 进位到 bit 27：右移 1 位保 jam */
            vl = (uint16_t)((uint16_t)(vl >> 1) | (uint16_t)((uint16_t)(vh & 1u) << 15)
                            | (uint16_t)(vl & 1u));
            vh = (uint16_t)(vh >> 1);
            exp_r = exp_r + 1;
            if (exp_r >= 0xFF) {
                return ((uint32_t)sign_r << 31) | F32_POS_INF;  /* Inf */
            }
        }
    } else {
        /* 异号：绝对值相减 */
        if ((bh > qh) | ((bh == qh) & (bl > ql))) {
            /* big > q：符号取 big 一侧；真差 = v - θ 的整数部分是 v-1 */
            vl = (uint16_t)(bl - ql);
            vh = (uint16_t)((uint16_t)(bh - qh) - (uint16_t)(bl < ql ? 1u : 0u));
            sign_r = big_is_a ? sa : sb;
            if (jam) {
                vl--;
                if (vl == 0xFFFFu) vh--;  /* jam 时借位修正 */
            }
        } else if ((bh < qh) | ((bh == qh) & (bl < ql))) {
            /* q > big：真差 = v + θ，无需修正；符号取 q 一侧 */
            vl = (uint16_t)(ql - bl);
            vh = (uint16_t)((uint16_t)(qh - bh) - (uint16_t)(ql < bl ? 1u : 0u));
            sign_r = big_is_a ? sb : sa;
        } else {
            /* big == q：jam 时真差 = -θ（量级 < 1 单位，入带符号零） */
            if (jam) {
                return ((uint32_t)(big_is_a ? sb : sa) << 31);
            }
            return F32_POS_ZERO;  /* 精确抵消（roundTiesToEven 下 x-x=+0） */
        }
        /* 左移规范化直到隐含位（bit 26）就位；指数降到 1 即非正规域。
         * sticky 与刻度无关：jam 标志在舍入处统一使用。 */
        while (((vh | vl) != 0u) && ((vh & 0x0400u) == 0u) && (exp_r > 1)) {
            vh = (uint16_t)((uint16_t)(vh << 1) | (uint16_t)(vl >> 15));
            vl = (uint16_t)(vl << 1);
            exp_r--;
        }
    }

    /* 舍入到偶数：sig24 = v>>3，guard = bit 2，低位 = bit 1..0 ∨ jam。
     * 隐含位未就位（v < 2^26）只可能出现在 exp_r == 1，此时尾数按
     * 指数域 0（非正规）打包；舍入进位到 bit 23 即最小正规数。 */
    mhi = (uint16_t)(vh >> 3);
    mlo = (uint16_t)((uint16_t)(vl >> 3) | (uint16_t)(vh << 13));
    g = (uint16_t)((vl >> 2) & 1u);
    st = (uint16_t)(((vl & 3u) != 0u) | (jam != 0u));

    if (g && (st || (mlo & 1u))) {
        mlo++;
        if (mlo == 0u) mhi++;
        if (mhi & 0x100u) {  /* 进位出 24 位 */
            mlo = (uint16_t)((uint16_t)(mlo >> 1) | (uint16_t)((uint16_t)(mhi & 1u) << 15));
            mhi = (uint16_t)(mhi >> 1);
            exp_r = exp_r + 1;
            if (exp_r >= 0xFF) {
                return ((uint32_t)sign_r << 31) | F32_POS_INF;  /* Inf */
            }
        }
    }
    ur.h[F32_HI16] = (uint16_t)((uint16_t)(sign_r << 15)
                                | (uint16_t)((mhi & 0x80u) ? (uint16_t)((uint32_t)exp_r << 7) : 0u)
                                | (uint16_t)(mhi & 0x7fu));
    ur.h[F32_LO16] = mlo;
    return ur.w;
}

/* ------------------------------------------------------------------ */
/* __subsf3：减法 = 加法 + 取负第二操作数                              */
/* ------------------------------------------------------------------ */
uint32_t _subsf3(uint32_t a, uint32_t b)
{
    return _addsf3(a, b ^ F32_SIGN_MASK);
}

