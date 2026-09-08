/*===-- mcs251_float_cmp.c ------------------------------------------------===*/
/* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— f32 比较（7 个 helper）。
 *
 * 返回值约定（与 compiler-rt / libgcc ABI 一致，Alice 复审最终裁定）：
 *   __eqsf2 / __nesf2：有序相等返回 0；不等或任一 NaN 返回 1
 *     （两谓词的返回约定相同，消费方按零/非零判读）。
 *   __ltsf2 / __lesf2：三态比较——a<b 返回 -1（0xFFFFFFFF），
 *     a==b 返回 0，a>b 返回 +1；任一 NaN（无序）返回 +1
 *     （compiler-rt comparesf2.c：LE 族 UNORDERED=+1）。
 *   __gtsf2 / __gesf2：三态同上，但任一 NaN（无序）返回 -1
 *     （compiler-rt comparesf2.c：GE 族 UNORDERED=-1）。
 *   __unordsf2：无序（任一 NaN）返回 1，有序返回 0。
 *
 * 比较算法：IEEE 754 有序比较的位级实现。
 *   - +0 == -0（清除符号位后比较）
 *   - 符号不同：负数更小
 *   - 同号：比较指数+尾数组合值（正数大值大，负数大值小）
 */

#include "mcs251_float.h"

/*
 * 内部：三态有符号比较 a ? b（数值序），无 NaN。
 * 返回 -1（a<b）/ 0（a==b）/ +1（a>b），以 uint32 位模式表示。
 */
static uint32_t f32_cmp3(uint32_t a, uint32_t b)
{
    uint32_t sa, sb;
    uint32_t av, bv;  /* 绝对值（符号位清除） */

    sa = F32_SIGN(a);
    sb = F32_SIGN(b);
    av = a & 0x7FFFFFFFu;
    bv = b & 0x7FFFFFFFu;

    /* +0 == -0 */
    if (av == 0u && bv == 0u) {
        return 0u;
    }

    if (sa != sb) {
        /* 符号不同：a 为负则 a < b；a 为正则 a > b */
        return sa ? 0xFFFFFFFFu : 1u;
    }

    /* 同号：正数绝对值大者大；负数绝对值大者小 */
    if (sa == 0u) {
        return (av < bv) ? 0xFFFFFFFFu : ((av > bv) ? 1u : 0u);
    }
    return (av > bv) ? 0xFFFFFFFFu : ((av < bv) ? 1u : 0u);
}

uint32_t _eqsf2(uint32_t a, uint32_t b)
{
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 1u;  /* NaN 比较总为假 */
    }
    /* +0 == -0 */
    if ((a & 0x7FFFFFFFu) == 0u && (b & 0x7FFFFFFFu) == 0u) {
        return 0u;
    }
    return (a == b) ? 0u : 1u;
}

uint32_t _nesf2(uint32_t a, uint32_t b)
{
    /* 与 __eqsf2 同一返回约定：有序相等 0，不等或 NaN 1 */
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 1u;  /* NaN != x 为真 */
    }
    if ((a & 0x7FFFFFFFu) == 0u && (b & 0x7FFFFFFFu) == 0u) {
        return 0u;  /* +0 == -0 */
    }
    return (a != b) ? 1u : 0u;
}

uint32_t _ltsf2(uint32_t a, uint32_t b)
{
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 1u;  /* LE 族无序：+1 */
    }
    return f32_cmp3(a, b);
}

uint32_t _lesf2(uint32_t a, uint32_t b)
{
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 1u;  /* LE 族无序：+1 */
    }
    return f32_cmp3(a, b);
}

uint32_t _gtsf2(uint32_t a, uint32_t b)
{
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 0xFFFFFFFFu;  /* GE 族无序：-1（compiler-rt GE_UNORDERED） */
    }
    return f32_cmp3(a, b);
}

uint32_t _gesf2(uint32_t a, uint32_t b)
{
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 0xFFFFFFFFu;  /* GE 族无序：-1（compiler-rt GE_UNORDERED） */
    }
    return f32_cmp3(a, b);
}

uint32_t _unordsf2(uint32_t a, uint32_t b)
{
    /* 无序 = 任一为 NaN */
    if (F32_IS_NAN(a) || F32_IS_NAN(b)) {
        return 1u;
    }
    return 0u;
}
