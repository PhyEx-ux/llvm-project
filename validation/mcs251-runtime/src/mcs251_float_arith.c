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
 *
 * 文件拆分（G7 S1''）：加减/乘法/除法例程已拆到独立 TU
 *   mcs251_float_addsub.c  -> __addsf3 / __subsf3
 *   mcs251_float_mul.c     -> __mulsf3
 *   mcs251_float_div.c     -> __divsf3
 * 使链接器可按程序未解析符号单独拉取（MCS251 lld 无 --gc-sections）。
 * 本文件保留 __negsf2 与整数<->f32 转换例程。
 */

#include "mcs251_float.h"
#include "mcs251_bitutil.h"

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

/* ------------------------------------------------------------------ */
/* __floatunsisf：unsigned int32 -> float（单参，G7 S1' 连接）          */
/*                                                                     */
/* 语义：uint32 全域 0..0xFFFFFFFF 精确/正确舍入（最近偶数）到 binary32。*/
/* 参数槽 ABI：首参 DPL:DPH:B:A（DPL 为低字节）。                       */
/* 与 _floatsisf 的区别：无符号源，无符号分支，故不存在"取负"步骤；    */
/* 且 uint32 全域最大值 0xFFFFFFFF 的 binary32 指数为 127+31=158 < 255，*/
/* 永不产生 Inf（保留防御性上溢检查以对齐 _floatsisf 的结构）。        */
/* ------------------------------------------------------------------ */
uint32_t _floatunsisf(uint32_t a)
{
    uint32_t exp, mant;
    uint32_t guard, sticky;
    uint32_t msb;

    if (a == 0u) {
        return F32_POS_ZERO;
    }

    /* 最高设置位（1-based）。用 volatile 阻止 clang 把移位计数与 msb
     * 组合成后端不支持的 ctlz（同 _floatsisf 的既有配方）。 */
    msb = mcs251_msb32(a);
    {
        volatile uint32_t vmsb = msb;
        uint32_t bit_pos = vmsb - 1u;  /* hidden bit 的 0-based 位置 */

        exp = F32_EXP_BIAS + bit_pos;

        if (bit_pos >= 23u) {
            uint32_t sh = bit_pos - 23u;
            if (sh == 0u) {
                mant = a;
                guard = 0u;
                sticky = 0u;
            } else if (sh <= 7u) {
                uint32_t guard_mask = 1u << (sh - 1u);
                uint32_t sticky_mask = guard_mask - 1u;
                guard = (a & guard_mask) ? 1u : 0u;
                sticky = (a & sticky_mask) ? 1u : 0u;
                mant = a >> sh;
            } else {
                /* 大移位（bit_pos=31 -> sh=8）。注意：本分支**有符号域也
                 * 可达**（_floatsisf 的 INT32_MIN，mag=0x80000000，
                 * bit_pos=31）——只是那里 guard/sticky 恒为 0，故旧掩码
                 * 的偏差不改变结果；**会因此舍入出错的输入仅在无符号域
                 * 可达**。本函数是无符号域，sticky 只收集 guard 位**之下**
                 * 的位，掩码必须排除 guard 本身，否则半途偶数（如 0x...80
                 * 且保留位 LSB=0）会被错误进位。 */
                uint32_t sticky_mask = (1u << (sh - 1u)) - 1u;
                sticky = (a & sticky_mask) ? 1u : 0u;
                guard = (a >> (sh - 1u)) & 1u;
                mant = a >> sh;
            }
        } else {
            /* 左移：无精度损失（bit_pos < 23，源值 < 2^24 精确） */
            uint32_t sh = 23u - bit_pos;
            mant = a << sh;
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

    /* 防御性上溢（uint32 全域不可达，保留以对齐 _floatsisf 结构） */
    if (exp >= 0xFFu) {
        return F32_POS_INF;
    }

    return F32_PACK(0u, exp, mant & F32_MANT_MASK);
}

/* ------------------------------------------------------------------ */
/* __fixunssfsi：float -> unsigned int32（单参，G7 S1' 连接）           */
/*                                                                     */
/* 语义：向零截断（C cast 语义，同 _fixsfsi）。实现定义边界按          */
/* compiler-rt __fixunssfsi 口径：NaN -> 0；负值（含 -0.0）-> 0；      */
/* 值 >= 2^32 -> 饱和 0xFFFFFFFF（Inf 同）。                           */
/* 参数槽 ABI：首参 DPL:DPH:B:A（DPL 为低字节）。                       */
/* ------------------------------------------------------------------ */
uint32_t _fixunssfsi(uint32_t a)
{
    uint32_t sign, exp, mant;
    uint32_t result;

    /* NaN -> 0（实现定义，选择安全值，同 _fixsfsi） */
    if (F32_IS_NAN(a)) {
        return 0u;
    }

    /* Inf -> 饱和到 UINT32_MAX（实现定义） */
    if (F32_IS_INF(a)) {
        return (F32_SIGN(a) != 0u) ? 0u : 0xFFFFFFFFu;
    }

    sign = F32_SIGN(a);
    exp = F32_EXP(a);
    mant = F32_MANT(a);

    /* 零与负值 -> 0（负值转换在 C 中未定义；compiler-rt 口径取 0） */
    if (F32_IS_ZERO(a) || sign) {
        return 0u;
    }

    /* 非正规：太小，截断为 0 */
    if (exp == 0u) {
        return 0u;
    }

    /* 加入隐含位 */
    mant = mant | F32_HIDDEN_BIT;

    /* 实际指数 = exp - bias；< 0 时结果为 0 */
    if (exp < F32_EXP_BIAS) {
        return 0u;
    }

    {
        uint32_t unbiased = exp - F32_EXP_BIAS;
        /* 值 >= 2^32：饱和（unbiased == 31 仍在 uint32 域内，不饱和） */
        if (unbiased >= 32u) {
            return 0xFFFFFFFFu;
        }
        if (unbiased <= 23u) {
            result = mant >> (23u - unbiased);
        } else {
            result = mant << (unbiased - 23u);
        }
    }

    return result;
}
