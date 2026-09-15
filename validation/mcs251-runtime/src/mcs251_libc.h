/*===-- mcs251_libc.h -----------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集运行时 —— 内存操作原语（mem/str 系列）。
 *
 * 范围裁定（PM 转达用户 2026-09-08）：只做内存操作原语，
 * 不含 printf/sprintf/putchar/math.h（用户自行处理）。
 *
 * ======================== 私有 ABI 警告（重要） ========================
 * memcpy / strcpy / memcmp 采用 "setter + 全局槽" 的两步私有调用约定：
 * 调用方先调 <fn>_set_src(src) 把源指针写入全局 uint32 槽，再调
 * 主函数传目的指针。由此带来以下硬性限制（Alice 审查 RT-5）：
 *   1. 非重入：主函数从全局槽取源指针；set 与 call 之间的任何
 *      同原语嵌套调用都会覆盖槽值，产生错误数据。
 *   2. 中断交错可破坏：若中断服务程序使用同一原语，被中断流程
 *      的槽值丢失，恢复后拷贝/比较错误源地址。
 *   3. 有效缓冲区前提：调用方必须保证 dst 有至少 n 字节可写、
 *      src 有至少 n 字节可读（strcpy 的 src 以 NUL 结尾），
 *      本实现不做任何长度/重叠检查。
 * 这不是通用 libc ABI，仅限本 MCS251 运行时配套固件在受控
 * （无嵌套、无中断交错或已关中断）场景下使用。
 * =======================================================================
 *
 * ABI 约束（本链 MCS251 后端实测）：
 *   1. 后端只支持单首参进寄存器（DPL/DPTR/DPL:DPH:B:A），第二及以后
 *      参数走 __<fn>_PARM_n 静态槽；但指针类型的静态槽参数被拒绝
 *      ("static pointer parameters are not supported")。
 *      因此需要两个指针的函数（memcpy/strcpy）用全局 uint32_t
 *      槽存储第二指针（见上方私有 ABI 警告）。memcpy/strcpy/memcmp
 *      已于 2026-09-15 按 PM 裁定统一迁移为标准原型（v2 静态指针槽），
 *      不再使用 setter/全局槽；上方警告自此仅作为历史记录保留。
 *   2. 全局指针变量也被拒绝，但全局 uint32_t 可以。故第二指针以
 *      uint32_t 存储，函数内部 cast 回指针使用。
 *   3. memset 第三参数（size）走 PARM_3 静态槽（uint32_t）。
 *
 * 构建契约：clang --target=mcs251-unknown-none -std=c11 -O0 -S -emit-llvm
 *           -> llc -mtriple=mcs251 -O0 -mcs251-object-format=elf -filetype=obj
 *   （libc 内存原语用 -O0：-O2 循环展开导致 PC-rel branch out of range）
 *
 * 大端注意：MCS251 是大端架构，但本文件按字节粒度操作，大端/小端一致。
 */

#ifndef MCS251_LIBC_H
#define MCS251_LIBC_H

#include <stdint.h>

/* ---- memset：单指针 + int + size ---- */
void* memset(void* dst, int c, uint32_t n);

/* ---- memcpy / strcpy：标准原型（同 memcmp 的 PM 裁定 2026-09-15，
 * 同一根因——demo TU 经宿主 string.h 声明的标准原型与私有 setter
 * ABI 的 P-4 记录冲突；v2 静态指针槽下第二指针直接走 PARM_2，
 * setter/全局槽 ABI 一并退役） ---- */
void* memcpy(void* dst, const void* src, uint32_t n);
char* strcpy(char* dst, const char* src);

/* ---- strlen：单指针 ---- */
uint32_t strlen(const char* s);

/* ---- memcmp：标准 3 参原型（PM 裁定 2026-09-15，统一与宿主 string.h
 * 的口径，消除 demo TU 经标准原型声明与 runtime 私有 2 参记录的 P-4
 * 签名冲突）。s1 走首参寄存器通道，s2 走 _memcmp_PARM_2 静态指针槽
 * （v2 合同 1,2,32,8,1 下合法——A4 静态指针槽通路），n 走 PARM_3。
 * 旧的 memcmp_set_src/setter 全局槽 ABI 随本裁定退役删除。 ---- */
int memcmp(const void* s1, const void* s2, uint32_t n);

/* 注：strncmp/strncmp_set_src 已按范围裁剪移除（不入库，不再提供）。 */

#endif /* MCS251_LIBC_H */
