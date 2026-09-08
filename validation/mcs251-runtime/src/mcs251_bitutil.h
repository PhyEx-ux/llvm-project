/*===-- mcs251_bitutil.h --------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
 */
/*
 * MCS251 运行时位工具 —— 避免 clang -O2 综合出后端不支持的 ctlz/cttz 指令。
 *
 * 实现在独立 .c 文件（mcs251_bitutil.c）中，防止 clang 跨函数把
 * de Bruijn OR-propagation 链优化为 ctlz intrinsic。
 *
 * 算法出处：公共领域 de Bruijn 序列位扫描法（Hacker's Delight 2nd ed.
 * §5-3；Anderson, "Bit twiddling hacks"）。独立实现。
 */

#ifndef MCS251_BITUTIL_H
#define MCS251_BITUTIL_H

#include <stdint.h>

/*
 * msb32：返回 v 的最高设置位位置（1-based，即 floor(log2(v))+1）。
 * v=0 返回 0。
 * 实现在 mcs251_bitutil.c（独立 TU，防 ctlz）。
 */
uint32_t mcs251_msb32(uint32_t v);

#endif /* MCS251_BITUTIL_H */
