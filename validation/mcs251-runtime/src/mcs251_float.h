/*===-- mcs251_float.h ----------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— IEEE-754 单精度 (binary32) 软实现。
 *
 * ABI 约定：f32 helper 的 binary32 payload 复用 i32 ABI 的
 * DPL:DPH:B:A（DPL 为低字节）；第二及以后参数走 __<fn>_PARM_n 的
 * 四字节静态槽（大端存储）。函数名前缀 '_' 经目标约定成为链接符号 '__'
 * （如 _addsf3 -> __addsf3）。
 *
 * 本运行时仅实现 binary32 的已连接子集。不得添加 f64/double helper、
 * DI helper 或 math helper。
 *
 * 冻结令解除登记（G7 S1'，PM 裁定 2026-09-15，依据
 * validation/mcs251-models/proposals/G7-FLOAT-DESIGN-draft.md §6 D1 行
 * "坚决解决浮点问题，不绕过"）：原文"不得添加 unsigned conversion
 * helper"的禁令**就且仅就** `_floatunsisf`（uint32->f32）与
 * `_fixunssfsi`（f32->uint32）两个 i32 宽度 helper 解除；f64/double、
 * DI（i64）与 math helper 的禁令维持不变。窄类型（i8/i16）不新增
 * helper：IR 侧经 zext/sext 提升到 i32 调用本对 helper。
 *
 * 独立实现声明：依据 IEEE-754 标准与公开算法描述（移位-加法对齐、
 * 恢复余数尾数除法、标准舍入到偶数）独立写出；未逐行参照 SDCC、
 * compiler-rt、libgcc、Berkeley softfloat 等既有实现文本。
 *
 * 构建契约：clang --target=mcs251-unknown-none -O2 -S -emit-llvm
 *           -> llc -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj
 */

#ifndef MCS251_FLOAT_H
#define MCS251_FLOAT_H

#include <stdint.h>

/* 宽度互锁断言（ILP32 前提，Alice 复审裁定补全）。
 * 无条件部分（宿主与目标一致，host fuzz 也在验）：
 *   char=1、short=2、int=4、int32_t/uint32_t=4。
 * 目标侧部分（构建时传 -DMCS251_RT_TARGET，CMakeLists 与验收脚本均加）：
 *   long=4、指针=4 —— 宿主（LP64，long/指针 8 字节）编译 host_fuzz 时
 *   不定义该宏，跳过这两条，只按 ILP32 语义使用 int32 域。 */
_Static_assert(sizeof(char) == 1, "MCS251 float runtime: char must be 1 byte");
_Static_assert(sizeof(short) == 2, "MCS251 float runtime: short must be 2 bytes");
_Static_assert(sizeof(int) == 4, "MCS251 float runtime: int must be 4 bytes");
_Static_assert(sizeof(int32_t) == 4, "MCS251 float runtime: int32_t must be 4 bytes");
_Static_assert(sizeof(uint32_t) == 4, "MCS251 float runtime: uint32_t must be 4 bytes");
#ifdef MCS251_RT_TARGET
_Static_assert(sizeof(long) == 4, "MCS251 float runtime: target long must be 4 bytes (ILP32)");
_Static_assert(sizeof(void*) == 4, "MCS251 float runtime: target pointer must be 4 bytes (ILP32)");
#endif

/*
 * IEEE-754 binary32 位域布局（大端无关，用位移提取）：
 *   bit 31     : sign
 *   bit 30-23  : exponent (8 bits, bias 127)
 *   bit 22-0   : mantissa (23 bits, implicit leading 1 for normals)
 *
 * 特殊值编码：
 *   0x00000000 = +0.0      0x80000000 = -0.0
 *   0x7F800000 = +Inf      0xFF800000 = -Inf
 *   0x7F800001..0x7FFFFFFF / 0xFF800001..0xFFFFFFFF = NaN (sign bit irrelevant)
 *   exp=0, mant!=0 : subnormal (implicit leading 0)
 */

#define F32_SIGN_MASK   0x80000000u
#define F32_EXP_MASK    0x7F800000u
#define F32_MANT_MASK   0x007FFFFFu
#define F32_EXP_SHIFT   23
#define F32_EXP_BIAS    127
#define F32_HIDDEN_BIT  0x00800000u

/* 提取/组装辅助宏 */
#define F32_SIGN(v)  ((uint32_t)(v) >> 31)
#define F32_EXP(v)   (((uint32_t)(v) >> F32_EXP_SHIFT) & 0xFFu)
#define F32_MANT(v)  ((uint32_t)(v) & F32_MANT_MASK)
#define F32_PACK(s,e,m) (((uint32_t)(s) << 31) | ((uint32_t)(e) << F32_EXP_SHIFT) | ((uint32_t)(m) & F32_MANT_MASK))

/* 特殊值常量 */
#define F32_POS_ZERO  0x00000000u
#define F32_NEG_ZERO  0x80000000u
#define F32_POS_INF   0x7F800000u
#define F32_NEG_INF   0xFF800000u
#define F32_QNAN      0x7FC00000u  /* quiet NaN (canonical, positive) */
#define F32_NEG_QNAN  0xFFC00000u  /* negative quiet NaN（本库约定：新生成 NaN 统一用此值，
                                     * 对齐 x86 Oracle-A；Alice 裁定 2026-09-08）*/

/* 判定宏（不产生分支以便优化器处理，但语义清晰） */
#define F32_IS_NAN(v)    ((F32_EXP(v) == 0xFFu) && (F32_MANT(v) != 0u))
#define F32_IS_INF(v)    ((F32_EXP(v) == 0xFFu) && (F32_MANT(v) == 0u))
#define F32_IS_ZERO(v)   (((uint32_t)(v) & 0x7FFFFFFFu) == 0u)
#define F32_IS_SUBNORM(v) ((F32_EXP(v) == 0u) && (F32_MANT(v) != 0u))

/*
 * === 软浮点运算（7 个）===
 * 签名约定：uint32_t _<name>(uint32_t a, uint32_t b)
 * 第二参数走 __<name>_PARM_2 静态槽（4 字节，大端）。
 *
 * __floatdisf / __fixsfdi、f64 conversion helpers 刻意不提供：本链不把
 * 任何未实现 family 用 payload 位宽伪装成已支持 ABI。
 * unsigned conversion 对（_floatunsisf/_fixunssfsi）按 G7 S1' 裁定已连接
 * （见上"冻结令解除登记"）。
 */
uint32_t _addsf3(uint32_t a, uint32_t b);   /* a + b */
uint32_t _subsf3(uint32_t a, uint32_t b);   /* a - b (= a + (-b)) */
uint32_t _mulsf3(uint32_t a, uint32_t b);   /* a * b */
uint32_t _divsf3(uint32_t a, uint32_t b);   /* a / b */
uint32_t _negsf2(uint32_t a);               /* -a (single argument) */
uint32_t _floatsisf(int32_t a);             /* signed i32 -> f32 */
uint32_t _fixsfsi(uint32_t a);              /* f32 -> signed i32 */
uint32_t _floatunsisf(uint32_t a);          /* unsigned i32 -> f32 (G7 S1') */
uint32_t _fixunssfsi(uint32_t a);           /* f32 -> unsigned i32 (G7 S1') */

/*
 * === 软浮点比较（7 个）===
 * 返回值约定（compiler-rt / libgcc ABI，Alice 复审最终裁定）：
 *   __eqsf2 / __nesf2：有序相等返回 0；不等或任一 NaN 返回 1
 *     （两个谓词返回约定相同，消费方按零/非零判读）。
 *   __ltsf2 / __lesf2：三态——a<b 返回 -1(0xFFFFFFFF)，a==b 返回 0，
 *     a>b 返回 +1；NaN 返回 +1（LE 族 UNORDERED=+1）。
 *   __gtsf2 / __gesf2：三态同上，但 NaN 返回 -1（GE 族 UNORDERED=-1）。
 *   __unordsf2：无序（任一 NaN）返回 1，有序返回 0。
 */
uint32_t _eqsf2(uint32_t a, uint32_t b);    /* 有序 a==b ? 0 : 1 */
uint32_t _nesf2(uint32_t a, uint32_t b);    /* NaN或 a!=b ? 1 : 0 */
uint32_t _ltsf2(uint32_t a, uint32_t b);    /* 三态；NaN→+1 */
uint32_t _lesf2(uint32_t a, uint32_t b);    /* 三态；NaN→+1 */
uint32_t _gtsf2(uint32_t a, uint32_t b);    /* 三态；NaN→-1 */
uint32_t _gesf2(uint32_t a, uint32_t b);    /* 三态；NaN→-1 */
uint32_t _unordsf2(uint32_t a, uint32_t b); /* 无序 ? 1 : 0 */

/* 注：math.h 系列（sin/cos/tan/sqrt/pow/log/exp/fabs/floor/ceil）
 * 不属于本入库范围（mcs251_float_math.c 保留工作树，不入库）。 */

#endif /* MCS251_FLOAT_H */
