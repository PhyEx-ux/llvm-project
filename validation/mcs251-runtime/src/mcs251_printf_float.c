/*===-- mcs251_printf_float.c ----------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集 —— printf 浮点格式化引擎 out_float（G13a S3 独立 TU）。
 *
 * G13a S3（G13A-CODE-DESIGN-draft.md rev-2 §3-c）：out_float 原为
 * mcs251_printf.c 的 TU 内 static 函数，格式引擎 %f/%F、%g/%G、%e/%E
 * 分支直接调用——只拆 TU 会保留 UND、闭包仍拉入浮点对象，不拉则链接
 * 失败。本 TU 使按需闭包可以单独取舍浮点格式化：
 *   - 完整形态：printf.o（UND _out_float）+ 本对象（闭包传递拉入），
 *     浮点格式化可用（默认）；
 *   - 裁剪形态：printf.o 以 -DMCS251_PRINTF_NO_FLOAT 编译（无浮点
 *     引擎，判定规则见 mcs251_printf_internal.h），不拉入本对象。
 *
 * 迁移纪律：函数体自 mcs251_printf.c（S3 前版本 out_float 定义）
 * **逐字原样**迁入，仅 linkage 由 static 改外部（对象符号 _out_float；
 * 第二参数经 _out_float_PARM_2 静态槽，emitParameterSlots 既定机制，
 * 与 putchar 跨 TU 提供者同构）。除 out_char/out_str 改经
 * mcs251_printf_internal.h 声明引用外，无任何实现改动。
 *
 * 红线：纯 C；无 64 位；无浮点类型（位模式 u32 传入）；无内联汇编；
 * 无 pragma。
 */

#include "mcs251_printf_internal.h"

/* 输出 f32 浮点（bit pattern 传入） */
void out_float(uint32_t bits, uint32_t fs)
{
    uint32_t sign, exp, mant;
    uint32_t int_part;
    char ibuf[11];
    uint32_t ipos = 0u;
    /* 2026-09-09 Kazimi 绕道（后端 bug 取证 parm_probe2/fD）：
     * `fs & 0xFFu` 对直接参数在 -O0 下被错译为固定直接寻址载荷
     * （mov r2,0x81 / mov r6,0x85，无重定位不对应任何符号）并破坏
     * 邻近寄存器分配。`x & 0xFF` 改写为移位+减法等价式：
     * fs - ((fs>>8)<<8)。&0xF / &0xFFFF / >>16 / >>31 实测无此问题。 */
    uint32_t frac_digits = fs - ((fs >> 8) << 8);
    uint32_t strip_trailing = fs >> 31;

    /* 2026-09-09 Kazimi 修复：sign 提取提前——原 Inf 分支在 sign 赋值
     * 前引用它（读未初始化变量，-Inf 可能丢负号或行为未定义）。 */
    sign = bits >> 31;

    /* NaN / Inf 处理 */
    if ((bits & 0x7FFFFFFFu) == 0x7F800000u) {
        if (sign) out_char('-');
        out_str("inf");
        return;
    }
    if (((bits >> 23) & 0xFFu) == 0xFFu && (bits & 0x7FFFFFu) != 0u) {
        out_str("nan");
        return;
    }

    exp = (bits >> 23) & 0xFFu;
    mant = bits & 0x7FFFFFu;

    if (sign) out_char('-');

    /* 零 */
    if ((bits & 0x7FFFFFFFu) == 0u) {
        uint32_t zi;
        out_char('0');
        if (frac_digits > 0u) {
            out_char('.');
            for (zi = 0u; zi < frac_digits; zi++) out_char('0');
        }
        return;
    }

    /* 加入隐含位 */
    if (exp != 0u) mant = mant | 0x00800000u;
    else exp = 1u;

    /* 实际值 = mant * 2^(exp-150)（150 = 23+127）
     * 2026-09-09 Kazimi 重写小数生成：原实现用 32x32->64 位长乘法算
     * frac_bits*10^frac_digits/2^shift，但丢失 mid<<16 的进位（"2.5"
     * 输出成 "2.491808"）。
     * 2026-09-10 二次重写（修复 Alice review P1）：上一版对
     * shift >= 32 直接把小数位全置零（%.6f 打 0.001f 得 "0.000000"，
     * 应 "0.001000"——0.001f 的 shift=33），且 shift 29..31 时 fb*10
     * 会溢出 32 位累加器。改为分段提取 + guard 位舍入（舍入规则
     * round-to-nearest-even，与 glibc 对齐；上一版纯截断，如
     * 123.4499969... 打成 123.449996，glibc 为 123.449997）：
     *   shift <= 28：逐位 ×10 提取（fb < 2^shift <= 2^28，×10 < 2^32
     *                不溢出），全程整数精确；多取一位 guard 位 + 精确
     *                余量，恰半局可精确判定；
     *   shift >  28：值 < 2^-4，小数先折成 F28 定点（frac28 =
     *                mant >> (shift-28)，初始截断误差 < 2^-28 ≈
     *                3.7e-9），再逐位 ×10 >> 28 提取。
     * 已知限界：shift > 28 时因 F28 初始截断，真值距舍入边界
     * < 2^-28 的近平局末位可能与正确舍入差 1（约 0.4% 概率；
     * "截断伪平局"已按上法纠正，此处仅剩 guard 落在 4/5 边界
     * 之下的不可判定窗口）；shift <= 28 路径完全精确。进位链
     * 999..9 -> 整数部分 +1。 */
    {
        int32_t unbiased = (int32_t)exp - 150;
        char fbuf2[8];
        uint32_t fpos2 = 0u;
        uint32_t fi2;
        uint32_t guard = 0u;   /* 第 frac_digits+1 位（舍入判据） */
        uint32_t rem = 0u;     /* guard 之后的余量（0 = 恰半局） */
        uint32_t tie_clipped = 0u;  /* F28 路径截去位非零（见舍入注释） */

        if (frac_digits > 6u) frac_digits = 6u;  /* fbuf2 容量保护 */

        if (unbiased >= 0) {
            /* 整数部分：mant << unbiased。值恰为整数，小数恒为 0。 */
            if (unbiased <= 8) {
                int_part = mant << unbiased;
            } else {
                int_part = 0xFFFFFFFFu;  /* 溢出，显示大数 */
            }
            for (fi2 = 0u; fi2 < frac_digits; fi2++) fbuf2[fi2] = '0';
            fpos2 = frac_digits;
        } else {
            /* unbiased < 0：值 = num0 / 2^shift（shift = -unbiased,
             * 1..149）。整数部分 = mant >> shift（shift >= 32 恒为 0，
             * 移位计数避开 >=32 的 UB）；小数分子 num0：
             * shift <= 24 取 mant 低 shift 位，shift > 24 时 mant
             * 整体都在小数点下（mant < 2^24）。 */
            uint32_t shift = (uint32_t)(-unbiased);
            uint32_t num0;
            uint32_t i;

            int_part = (shift >= 32u) ? 0u : (mant >> shift);

            if (shift <= 24u) {
                num0 = mant & ((1u << shift) - 1u);
            } else {
                num0 = mant;
            }

            if (shift <= 28u) {
                /* 精确路径：fb*10 < 2^28*10 < 2^32，永不溢出。
                 * 多提取一位 guard（fbuf2[8] 容量 >= 6+1）。 */
                uint32_t fb = num0;
                uint32_t mask = (1u << shift) - 1u;
                for (i = 0u; i <= frac_digits; i++) {
                    fb = fb * 10u;
                    if (i < frac_digits) {
                        fbuf2[i] = (char)('0' + (fb >> shift));
                    } else {
                        guard = fb >> shift;
                    }
                    fb = fb & mask;
                }
                rem = fb;  /* 精确余量 */
            } else {
                /* F28 半精确路径：frac28 = value * 2^28 截断。
                 * t = frac28*10 < 2^28*10 < 2^32，安全。 */
                uint32_t frac28;
                uint32_t sh28 = shift - 28u;
                frac28 = (sh28 >= 32u) ? 0u : (num0 >> sh28);
                /* 记录是否截掉了非零位：截断把高于半局的真值削到
                 * "恰半"时，rem==0 不再代表真恰半（见舍入注释） */
                if (sh28 < 32u && (num0 & ((1u << sh28) - 1u)) != 0u) {
                    tie_clipped = 1u;
                }
                for (i = 0u; i <= frac_digits; i++) {
                    uint32_t t = frac28 * 10u;
                    if (i < frac_digits) {
                        fbuf2[i] = (char)('0' + (t >> 28));
                    } else {
                        guard = t >> 28;
                    }
                    frac28 = t & 0x0FFFFFFFu;
                }
                rem = frac28;
            }
            fpos2 = frac_digits;
        }

        /* 舍入：guard > 5 进位；guard < 5 舍去；guard == 5 时余量
         * 非零进位；恰半保留偶数位（round-to-nearest-even，与
         * glibc printf 对精确十进制平局的行为一致，实测 0.25→"0.2"、
         * 0.5→"0"、2.5→"2"、0.0078125→"0.007812"）。
         * 例外：F28 路径截去位非零（tie_clipped）时，rem==0 是截断
         * 伪影——真值 = 截断值 + δ ∈ (半局, 半局+2^-28]，必须进位
         * （如 0.0156250009 → "0.01563"）。真恰半要求展开在
         * guard 位终结，即 shift <= 31 且截去位全零，此时
         * tie_clipped==0，仍走精确半偶分支。 */
        {
            uint32_t round_up;
            if (guard > 5u) {
                round_up = 1u;
            } else if (guard < 5u) {
                round_up = 0u;
            } else if (rem != 0u) {
                round_up = 1u;
            } else if (tie_clipped != 0u) {
                round_up = 1u;  /* 截断伪平局：真值必在半局之上 */
            } else {
                /* 恰半：看保留部分的末位奇偶（0 舍 1 进） */
                uint32_t last = (frac_digits > 0u)
                    ? (uint32_t)(fbuf2[frac_digits - 1u] - '0')
                    : (int_part % 10u);
                round_up = last & 1u;
            }
            if (round_up != 0u) {
                int32_t j = (int32_t)frac_digits - 1;
                for (;;) {
                    if (j < 0) {
                        /* 999..9 全进位：小数清零、整数部分 +1 */
                        int_part = int_part + 1u;
                        for (fi2 = 0u; fi2 < frac_digits; fi2++) {
                            fbuf2[fi2] = '0';
                        }
                        break;
                    }
                    if (fbuf2[j] == '9') {
                        fbuf2[j] = '0';
                        j--;
                    } else {
                        fbuf2[j] = (char)(fbuf2[j] + 1);
                        break;
                    }
                }
            }
        }

        /* 输出整数部分 */
        if (int_part == 0u) {
            ibuf[ipos] = '0';
            ipos++;
        } else {
            while (int_part != 0u) {
                ibuf[ipos] = (char)('0' + (int_part % 10u));
                ipos++;
                int_part = int_part / 10u;
            }
        }
        {
            uint32_t ti;
            for (ti = ipos; ti > 0u; ti--) out_char(ibuf[ti - 1u]);
        }

        /* 输出小数部分 */
        if (frac_digits > 0u) {
            out_char('.');
            /* strip_trailing（%g）：去掉末尾全部为零的位 */
            uint32_t keep = fpos2;
            if (strip_trailing) {
                while (keep > 0u && fbuf2[keep - 1u] == '0') keep--;
                if (keep == 0u) keep = 1u;  /* 至少留一位（"x.0"） */
            }
            for (fi2 = 0u; fi2 < keep; fi2++) out_char(fbuf2[fi2]);
        }
    }
}
