/*===-- mcs251_libc.c -----------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集运行时 —— mem/str 内存原语实现。
 *
 * 私有 ABI：memcpy/strcpy/memcmp 的第二指针经全局 uint32_t 槽传递
 * （setter + 主调用两步）。非重入、中断交错可破坏、有效缓冲区前提
 * 等硬性限制见 mcs251_libc.h 顶部警告（Alice 审查 RT-5）。
 *
 * 红线：纯 C；无 64 位类型；无浮点运算符；无内联汇编；无 pragma；
 * 静态全局仅限整数类型（指针类型的全局变量被后端拒绝）。
 *
 * 大端注意：MCS251 是大端架构，但本文件不依赖字节序——所有指针
 * 操作按字节粒度进行，大端/小端行为一致。
 */

#include "mcs251_libc.h"

/* ---- memset ---- */
void* memset(void* dst, int c, uint32_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n != 0u) {
        *p = (uint8_t)c;
        p++;
        n--;
    }
    return dst;
}

/* ---- memcpy：标准 3 参（PM 裁定 2026-09-15，见 mcs251_libc.h） ---- */
void* memcpy(void* dst, const void* src, uint32_t n)
{
    uint8_t* dp = (uint8_t*)dst;
    const uint8_t* sp = (const uint8_t*)src;
    while (n != 0u) {
        *dp = *sp;
        dp++;
        sp++;
        n--;
    }
    return dst;
}

/* ---- strcpy：标准 2 参（PM 裁定 2026-09-15，见 mcs251_libc.h） ---- */
char* strcpy(char* dst, const char* src)
{
    char* r = dst;
    const char* sp = (const char*)src;
    while (*sp != 0) {
        *dst = *sp;
        dst++;
        sp++;
    }
    *dst = 0;
    return r;
}

/* ---- strlen：已按 G13a-S2 切片原样拆出到独立 TU mcs251_str.c ----
 * （G13A-CODE-DESIGN-draft.md rev-2 §3-S2 / PM D4：MCS251 lld 无
 * --gc-sections，按需闭包要求独立对象；原型仍由 mcs251_libc.h 统一
 * 提供，本 TU 不再定义，避免重复定义。） */

/* ---- memcmp：标准 3 参（PM 裁定 2026-09-15，见 mcs251_libc.h） ---- */
int memcmp(const void* s1, const void* s2, uint32_t n)
{
    const uint8_t* pa = (const uint8_t*)s1;
    const uint8_t* pb = (const uint8_t*)s2;
    while (n != 0u) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
        n--;
    }
    return 0;
}
