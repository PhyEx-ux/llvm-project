/*===-- mcs251rt_div.h ----------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 除法/取模运行时 —— 八个精确宽度签名的唯一定义点。
 *
 * 许可：Apache-2.0 WITH LLVM-exception（随本 LLVM fork 分发）。
 *
 * 独立实现声明：本目录八个实现依据公开算法描述（恢复余数的移位-减法除法，
 * 见目录 README.md 的出处一节）独立写出；未逐行参照 SDCC 源码或其编译产物，
 * 未参照 compiler-rt、libgcc、newlib 等既有实现文本。SDCC 材料仅按除法设计
 * v4 §10-Q1 的裁定用作行为参考与语义 oracle。
 *
 * 结果恢复转换依赖（除法设计 v4 §6.1 冻结清单第 5 项）：本库有符号结果的
 * 恢复依赖本链冻结 Clang 对 N=16/32 超范围无符号到有符号转换"保留低 N 位、
 * 按二补码解释"的行为（C11 6.3.1.3p3 实现定义），非可移植性质。该约定只在
 * 四个有符号包装层（mcs251rt_divsint.c、mcs251rt_divslong.c、
 * mcs251rt_modsint.c、mcs251rt_modslong.c）的最终 return 转换点生效，
 * 逐点注释在位；四个无符号助手无此类转换点。工具链身份或 §6.1 第 1-4 项
 * 任一编译条件变化即触发该依赖的全量重验。
 *
 * 构建契约按除法设计 v4 §6.1 冻结清单固定：
 *   clang --target=mcs251-unknown-none
 *   （driver 默认 -mcs251-memory-model=xsmall，即 cc1 契约
 *   -mcs251-memory-contract=1,2,32,8,1）+ cc1 与 llc 两串 DataLayout
 *   + 前端与后端各 -O2。
 */

#ifndef MCS251_RT_DIV_H
#define MCS251_RT_DIV_H

#include <stdint.h>

/*
 * 静态宽度断言（除法设计 v4 §6.1）：签名宽度与本链整数模型
 * （short=16、int=long=32）互锁。本头文件被每个源文件包含，
 * 断言随每个源文件编译生效。
 */
_Static_assert(sizeof(int16_t) == 2, "MCS251 runtime: int16_t must be 2 bytes");
_Static_assert(sizeof(uint16_t) == 2, "MCS251 runtime: uint16_t must be 2 bytes");
_Static_assert(sizeof(int32_t) == 4, "MCS251 runtime: int32_t must be 4 bytes");
_Static_assert(sizeof(uint32_t) == 4, "MCS251 runtime: uint32_t must be 4 bytes");

/*
 * 八个冻结签名：命名与宽度逐字对照除法设计 v4 §6.1 终表。接口与实现内
 * 中间量不使用裸 int、long、unsigned 定宽，只使用上列四种定宽类型。
 * 函数名经目标一次 '_' 前缀成为 __divuint 等八个链接符号；第二参数走
 * 对应 __<fn>_PARM_2 静态槽（i16 槽 2B、i32 槽 4B，大端存储）。
 */

uint16_t _divuint(uint16_t x, uint16_t y);   /* -> IR: define i16 @_divuint(i16, i16)  */
uint32_t _divulong(uint32_t x, uint32_t y);  /* -> IR: define i32 @_divulong(i32, i32)  */
int16_t _divsint(int16_t x, int16_t y);      /* -> IR: define i16 @_divsint(i16, i16)   */
int32_t _divslong(int32_t x, int32_t y);     /* -> IR: define i32 @_divslong(i32, i32)  */
uint16_t _moduint(uint16_t x, uint16_t y);   /* -> IR: define i16 @_moduint(i16, i16)   */
uint32_t _modulong(uint32_t x, uint32_t y);  /* -> IR: define i32 @_modulong(i32, i32)  */
int16_t _modsint(int16_t x, int16_t y);      /* -> IR: define i16 @_modsint(i16, i16)   */
int32_t _modslong(int32_t x, int32_t y);     /* -> IR: define i32 @_modslong(i32, i32)  */

#endif /* MCS251_RT_DIV_H */
