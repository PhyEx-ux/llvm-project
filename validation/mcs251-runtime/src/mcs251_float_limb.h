/*===-- mcs251_float_limb.h -----------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— 16 位半字（limb）域编解码辅助（共享头）。
 *
 * 背景（G7 S1'' 实测）：目标为**大端**，且 MCS251 没有 32 位桶形移位器；
 * 后端把 32 位常量右移展开为 CY 链 RRC-A 序列，实测约 21B/bit
 * （v>>24 单条即 527B，v>>16 359B），而 16 位右移仅 ~38B、8 位更少。
 * 因此本运行时的位域提取/拼装统一改走 16 位半字：
 *
 *   hi16 = 高半字（大端 h[0] / 小端 h[1]），lo16 = 低半字
 *   sign = hi16 >> 15
 *   exp  = (hi16 >> 7) & 0xff
 *   mant = ((hi16 & 0x7f) << 16) | lo16
 *
 * 打包（mant -> 高/低半字）经 volatile 联合体写半字完成，避免 clang 把
 * 常量移位重新综合成昂贵的 32 位右移链。该头被各算术例程独立 TU 包含，
 * 自身不产生代码（无外部链接符号），故不破坏按例程链接。
 */

#ifndef MCS251_FLOAT_LIMB_H
#define MCS251_FLOAT_LIMB_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define F32_HI16 0u
#define F32_LO16 1u
#else
#define F32_HI16 1u
#define F32_LO16 0u
#endif

typedef union { uint32_t w; uint16_t h[2]; } f32_hl;

#endif /* MCS251_FLOAT_LIMB_H */
