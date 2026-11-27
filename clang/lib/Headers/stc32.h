/*===---- stc32.h - Unified STC32G MCU device header for LLVM/Clang MCS251 -===*
 * LLVM/Clang MCS-251 统一 STC32G 系列芯片总头文件
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===*/

#ifndef __STC32_H
#define __STC32_H

#include <mcs251_intrins.h>

/* Select device derivative / 选择目标芯片型号:
 *   - Default (or -DSTC32G / -DSTC32G12K128): STC32G (STC32G12K128 / 8K64)
 *     默认（或指定 -DSTC32G / -DSTC32G12K128）：STC32G12K128 / 8K64 系列
 *   - -DSTC32G144K246 (or -DSTC32G_144K246): STC32G144K246
 *     指定 -DSTC32G144K246（或 -DSTC32G_144K246）：STC32G144K246 系列
 */
#if defined(STC32G144K246) || defined(STC32G_144K246)
#include <stc32g144k246.h>
#else
#include <stc32g.h>
#endif

/*===----------------------------------------------------------------------===*
 * STC32-prefixed convenience aliases for MCS-251 compiler capabilities
 * STC32 前缀的 MCS-251 编译器能力便捷别名
 *===----------------------------------------------------------------------===*/
#define STC32_SFR(addr) MCS251_SFR(addr)
#define STC32_XFR(addr) MCS251_XFR(addr)
#define STC32_XDATA MCS251_XDATA
#define STC32_CODE MCS251_CODE
#define STC32_REENTRANT MCS251_REENTRANT
#ifndef __cplusplus
#define STC32_BIT MCS251_BIT
#define STC32_SFR_BIT(bit_addr) MCS251_SFR_BIT(bit_addr)
#define STC32_RAM_BIT(bit_idx) MCS251_RAM_BIT(bit_idx)
#endif

#define STC32_PLACE_AT(addr) MCS251_PLACE_AT(addr)
#define STC32_BIND_AT(addr) MCS251_BIND_AT(addr)
#define STC32_RETAIN MCS251_RETAIN
#define STC32_NOINIT MCS251_NOINIT

#define STC32_ISR(vector) MCS251_ISR(vector)
#define STC32_ISR_PRIO(vector, prio) MCS251_ISR_PRIO(vector, prio)

#define STC32_BIT_MASK(n) MCS251_BIT_MASK(n)
#define STC32_SET_BITS(reg, mask) MCS251_SET_BITS(reg, mask)
#define STC32_CLR_BITS(reg, mask) MCS251_CLR_BITS(reg, mask)
#define STC32_MODIFY_REG(reg, clr_mask, set_mask)                              \
  MCS251_MODIFY_REG(reg, clr_mask, set_mask)

/* Global interrupt & priority init helpers / 全局中断临界区与优先级初始化辅助宏 */
#define stc32_ea_save_disable() mcs251_ea_save_disable()
#define stc32_ea_restore(saved) mcs251_ea_restore(saved)
#define stc32_ea_enable() mcs251_ea_enable()
#define stc32_ea_disable() mcs251_ea_disable()
#define stc32_isr_priority_init() __mcs251_isr_priority_init()

/* Hardware TFPU math coprocessor helpers / 硬件 TFPU 数学协处理器辅助宏 */
#define stc32_tfpu_sin(x) mcs251_tfpu_sin(x)
#define stc32_tfpu_cos(x) mcs251_tfpu_cos(x)
#define stc32_tfpu_tan(x) mcs251_tfpu_tan(x)
#define stc32_tfpu_atan(x) mcs251_tfpu_atan(x)
#define stc32_tfpu_sqrt(x) mcs251_tfpu_sqrt(x)
#define stc32_tfpu_add(a, b) mcs251_tfpu_add(a, b)
#define stc32_tfpu_sub(a, b) mcs251_tfpu_sub(a, b)
#define stc32_tfpu_mul(a, b) mcs251_tfpu_mul(a, b)
#define stc32_tfpu_div(a, b) mcs251_tfpu_div(a, b)

/* Opt-in Keil C251 fixed-width type aliases (avoid polluting default C NS).
 * Note: MCS-251 LLVM defaults to ILP32 (32-bit int), whereas Keil C251 uses
 * 16-bit int; these typedefs preserve exact Keil widths via <stdint.h>.
 * 按需开启的 Keil C251 定宽类型别名（避免默认污染标准 C 命名空间）。
 * 注意：MCS-251 LLVM 默认采用 ILP32（int 为 32 位），而 Keil C251 的 int 为
 * 16 位；以下别名基于 <stdint.h> 精确保持 Keil 位宽语义。
 */
#if defined(STC32_ENABLE_KEIL_TYPES)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
#ifndef __cplusplus
typedef __bit BOOL;
#endif
#endif /* STC32_ENABLE_KEIL_TYPES */

#endif /* __STC32_H */
