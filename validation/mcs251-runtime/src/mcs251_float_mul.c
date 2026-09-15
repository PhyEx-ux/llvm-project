/*===-- mcs251_float_mul.c ------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— 乘法单元（__mulsf3）。
 *
 * 本文件只定义 __mulsf3，与加减、除法单元分离，便于按例程链接。
 *
 * 24x24 位尾数积用 16 位 limb schoolbook 拆解：
 *   (A:B)*(C:D) = A*C<<32 + (A*D + B*C)<<16 + B*D
 * 实测发射（-O2，Alice 复核）：整函数 10 条 MULW、0 条 MULAB，其中 6 条
 * 为零乘（后端 LowerMul32 对每个 i32 乘固定展开 3 条 MULW，不区分 16/8 位
 * 操作数）。即硬件乘确实被命中，但 16x16->32 是"按 32 位乘展开"的副产物，
 * 而非 C 层逐个 limb 精确映射；约 60B/2888B（2.1%）属可评估的展开浪费。
 */

#include "mcs251_float.h"
#include "mcs251_float_limb.h"

/* ------------------------------------------------------------------ */
/* __mulsf3：f32 乘法                                                  */
/* 第二参数 b 走 __mulsf3_PARM_2（4B 大端）。                          */
/*                                                                     */
/* 算法（roundTiesToEven）——与重写前逐位一致：                         */
/*   1. 非正规输入先左移规范化（MSB 到 bit 23），指数相应下调。        */
/*   2. 24x24 -> 48 位乘积用 16 位 limb schoolbook：                    */
/*        (A:B)*(C:D) = A*C<<32 + (A*D + B*C)<<16 + B*D                */
/*      硬件乘经后端 LowerMul32 命中（每 i32 乘展开 3 条 MULW；实测整    */
/*      函数 10 MULW/0 MULAB，见文件头注）。                            */
/*      32 位积经联合体拆回 16 位半字，不产生 32 位常量右移。           */
/*   3. 规格化到 24 位有效位并保留 guard/sticky（sticky 含 p0）。       */
/*   4. 上溢 -> Inf；exp<=0 右移入非正规域（guard/sticky 从移出位收集， */
/*      舍入可升最小正规数）；下溢尽移出 -> 带符号零。                 */
/*   5. 舍入到偶数同加法。                                             */
/* ------------------------------------------------------------------ */
uint32_t _mulsf3(uint32_t a, uint32_t b)
{
    volatile f32_hl ua, ub, ur, up;
    uint16_t ha, hb, la, lb;
    uint16_t A, B, C, D;
    uint16_t p0, p1, p2, adl, adh, bcl, bch, sl, sh, cy, m;
    uint16_t lo, hi, g, st;
    uint16_t sign_r, ea, eb;
    int16_t exp_r;

    ua.w = a; ub.w = b;
    ha = ua.h[F32_HI16]; hb = ub.h[F32_HI16];
    la = ua.h[F32_LO16]; lb = ub.h[F32_LO16];

    ea = (uint16_t)((uint16_t)(ha >> 7) & 0xffu);
    eb = (uint16_t)((uint16_t)(hb >> 7) & 0xffu);
    sign_r = (uint16_t)((uint16_t)(ha >> 15) ^ (uint16_t)(hb >> 15));
    A = (uint16_t)(ha & 0x7fu);
    C = (uint16_t)(hb & 0x7fu);
    B = la; D = lb;

    /* NaN（保留第一个 NaN 操作数的符号）
     * 注意：位域 uint16_t 参与移位前先经受整型提升（C11 6.3.1.1），
     * 若先移位后强转，`(uint32_t)(x << 31)` 中 x==1 时移位发生在 int
     * 上，1<<31 溢出有符号 int —— UB。必须先转 uint32_t 再移位。 */
    if ((ea == 0xffu) & ((A | B) != 0u))
        return ((uint32_t)(ha >> 15) << 31) | F32_QNAN;
    if ((eb == 0xffu) & ((C | D) != 0u))
        return ((uint32_t)(hb >> 15) << 31) | F32_QNAN;
    /* Inf */
    if (ea == 0xffu) {
        if ((eb | C | D) == 0u) return F32_NEG_QNAN;  /* Inf * 0 = NaN */
        return ((uint32_t)sign_r << 31) | F32_POS_INF;
    }
    if (eb == 0xffu) {
        if ((ea | A | B) == 0u) return F32_NEG_QNAN;
        return ((uint32_t)sign_r << 31) | F32_POS_INF;
    }
    /* 零（幅度判定，忽略符号位） */
    if (((ea | A | B) == 0u) | ((eb | C | D) == 0u))
        return ((uint32_t)sign_r << 31);

    /* 隐含位；非正规输入左移规范化，有效指数可降到 1-sh（<0）。 */
    if (ea != 0u) {
        A = (uint16_t)(A | 0x80u);
    } else {
        ea = 1u;
        while ((A & 0x80u) == 0u) {
            A = (uint16_t)((uint16_t)(A << 1) | (uint16_t)(B >> 15));
            B = (uint16_t)(B << 1);
            ea--;
        }
    }
    if (eb != 0u) {
        C = (uint16_t)(C | 0x80u);
    } else {
        eb = 1u;
        while ((C & 0x80u) == 0u) {
            C = (uint16_t)((uint16_t)(C << 1) | (uint16_t)(D >> 15));
            D = (uint16_t)(D << 1);
            eb--;
        }
    }
    exp_r = (int16_t)((int16_t)ea + (int16_t)eb - F32_EXP_BIAS);

    /* 24x24 -> 48 位积，经后端 LowerMul32 命中硬件乘（实测 10 MULW/0 MULAB，
     * 含 6 条零乘——见文件头注）。乘积拆回 16 位 limb 经联合体完成。 */
    up.w = (uint32_t)B * (uint32_t)D;
    p0 = up.h[F32_LO16];
    m  = up.h[F32_HI16];
    up.w = (uint32_t)A * (uint32_t)D;
    adh = up.h[F32_HI16]; adl = up.h[F32_LO16];
    up.w = (uint32_t)B * (uint32_t)C;
    bch = up.h[F32_HI16]; bcl = up.h[F32_LO16];

    sl = (uint16_t)(adl + bcl);
    sh = (uint16_t)((uint16_t)(adh + bch) + (uint16_t)(sl < adl ? 1u : 0u));
    p1 = (uint16_t)(m + sl);
    cy = (uint16_t)(p1 < m ? 1u : 0u);
    p2 = (uint16_t)((uint16_t)((uint16_t)A * C) + sh + cy);

    /* 规格化：p2 的 bit 15 承载隐含位；g = guard，st = guard 以下各位
     * （含 p0）的 OR。 */
    if (p2 & 0x8000u) {
        g  = (uint16_t)((p1 >> 7) & 1u);
        st = (uint16_t)(((p1 & 0x7fu) | p0) != 0u);
        lo = (uint16_t)((uint16_t)(p1 >> 8) | (uint16_t)((p2 & 0xffu) << 8));
        hi = (uint16_t)(p2 >> 8);
        exp_r = (int16_t)(exp_r + 1);
    } else {
        g  = (uint16_t)((p1 >> 6) & 1u);
        st = (uint16_t)(((p1 & 0x3fu) | p0) != 0u);
        lo = (uint16_t)((uint16_t)(p1 >> 7) | (uint16_t)((p2 & 0x7fu) << 9));
        hi = (uint16_t)(p2 >> 7);
    }

    /* 上溢（舍入前判定：舍入至多再 +1） */
    if (exp_r >= 0xFF) return ((uint32_t)sign_r << 31) | F32_POS_INF;

    if (exp_r <= 0) {
        /* 下溢/非正规：逐位右移入非正规域，移出位折叠进 guard/sticky。 */
        int16_t n = (int16_t)(1 - exp_r);
        uint16_t gg = 0u, sst = 0u;
        if (n >= 25) return ((uint32_t)sign_r << 31);  /* 全部移出，入零 */
        while (n > 0) {
            sst = (uint16_t)(sst | gg);
            gg = (uint16_t)(lo & 1u);
            lo = (uint16_t)((uint16_t)(lo >> 1) | (uint16_t)((hi & 1u) << 15));
            hi = (uint16_t)(hi >> 1);
            n--;
        }
        sst = (uint16_t)(sst | st | g);
        if (gg && (sst || (lo & 1u))) {
            lo++;
            if (lo == 0u) hi++;
        }
        if (hi & 0x80u) return ((uint32_t)sign_r << 31) | 0x00800000u;
        ur.h[F32_HI16] = (uint16_t)((uint16_t)(sign_r << 15) | (uint16_t)(hi & 0x7fu));
        ur.h[F32_LO16] = lo;
        return ur.w;
    }

    /* 正规数舍入到偶数 */
    if (g && (st || (lo & 1u))) {
        lo++;
        if (lo == 0u) {
            hi++;
            if (hi & 0x100u) { hi = 0x80u; exp_r = (int16_t)(exp_r + 1); }
        }
    }
    if (exp_r >= 0xFF) return ((uint32_t)sign_r << 31) | F32_POS_INF;
    ur.h[F32_HI16] = (uint16_t)((uint16_t)(sign_r << 15)
                                | (uint16_t)((uint16_t)exp_r << 7)
                                | (uint16_t)(hi & 0x7fu));
    ur.h[F32_LO16] = lo;
    return ur.w;
}

