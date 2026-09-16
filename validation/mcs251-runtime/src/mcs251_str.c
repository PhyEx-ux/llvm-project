/*===-- mcs251_str.c ------------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集运行时 —— strlen（独立 TU）。
 *
 * G13a-S2 切片（G13A-CODE-DESIGN-draft.md rev-2 §3-S2 / PM D4）：strlen
 * 从 mcs251_libc.c 原样拆出为独立编译单元——不是新增实现，源级签名
 * （uint32_t strlen(const char*)，见 mcs251_libc.h）与函数体逐字保持，
 * Tag 28 签名记录因此与拆分前 mcs251_libc.c 发出的完全相同，签名协议
 * 与 Tag 28 语义零改动。拆 TU 的唯一目的：MCS251 lld 无 --gc-sections，
 * 只有独立对象才能被 drive.py 的按需闭包在程序确实引用 _strlen 时
 * 单独拉入（42/43 类程序），不再连带其余 mem 原语。
 *
 * 不重复定义：strlen 定义只存在于本 TU（mcs251_libc.c 已同步移除）。
 *
 * 红线（同 mcs251_libc.c）：纯 C；无 64 位类型；无浮点运算符；
 * 无内联汇编；无 pragma；静态全局仅限整数类型。
 */

#include "mcs251_libc.h"

/* ---- strlen（自 mcs251_libc.c 原样拆出，G13a-S2） ---- */
uint32_t strlen(const char* s)
{
    uint32_t n = 0u;
    while (*s != 0) {
        n++;
        s++;
    }
    return n;
}
