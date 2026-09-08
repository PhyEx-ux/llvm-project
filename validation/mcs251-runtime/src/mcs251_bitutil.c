/*===-- mcs251_bitutil.c --------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 运行时位工具 —— 独立编译单元，避免 clang -O2 在同文件内
 * 把 de Bruijn OR-propagation 链识别为 ctlz intrinsic。
 *
 * 当本函数与调用方在同一 TU 编译时，clang -O2 的跨函数分析会把
 * "OR 传播 + 查表" 优化为 llvm.ctlz intrinsic，而后端不支持 ctlz。
 * 放入独立 .c 文件编译，clang 无法跨 TU 做此优化。
 */

#include "mcs251_bitutil.h"

/* 实现在头文件中声明；此处为定义。
 * 头文件中的 static inline 已移除，改为 extern 定义。 */

static const uint8_t mcs251_debruijn_table_local[32] = {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
};

uint32_t mcs251_msb32(uint32_t v)
{
    uint32_t r;
    if (v == 0u) return 0u;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v = v - (v >> 1);
    r = (v * 0x077CB531u) >> 27;
    return (uint32_t)mcs251_debruijn_table_local[r] + 1u;
}
