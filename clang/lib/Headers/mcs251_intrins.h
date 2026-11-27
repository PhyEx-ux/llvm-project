/*===---- mcs251_intrins.h - MCS-251 compiler-native intrinsics & helpers --===*
 * MCS-251 编译器原生内建函数、空间限定符与硬件辅助宏
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===*/

#ifndef __MCS251_INTRINS_H
#define __MCS251_INTRINS_H

#if !defined(__mcs251__)
#error "mcs251_intrins.h requires the MCS-251 target (--target=mcs251-unknown-none)."
#endif

#include <stdint.h>

/*===----------------------------------------------------------------------===*
 * Direct SFR (AS6) and Extended SFR (XFR, 0x7E0000..0x7EFFFF) accessors
 * 直寻址 SFR（AS6）与扩展 SFR（XFR，0x7E0000..0x7EFFFF）访问宏
 *
 * Under the default v2 memory contract (xsmall/small: ASLayoutVersion=2),
 * direct SFRs in 0x80..0xFE MUST use address_space(6) so the backend emits
 * single-instruction direct SFR moves (`mov 0xNN, rM` / `mov rM, 0xNN`).
 * AS0 pointers lower to `@dr` RAM accesses under v2.
 * 在默认 v2 内存契约（xsmall/small，ASLayoutVersion=2）下，0x80..0xFE 区间
 * 的直寻址 SFR 必须使用 address_space(6)，以便后端生成单条直寻址 SFR 指令；
 * 普通 AS0 指针在 v2 下会降为 @dr 内存访问。
 *
 * Define MCS251_LEGACY_V1_SFR only when compiling both clang and llc with the
 * explicit compatibility contract `-mcs251-memory-contract=1,1,32,8,1`.
 * 仅当 clang 与 llc 两侧均显式使用旧版兼容契约 1,1,32,8,1 时才定义
 * MCS251_LEGACY_V1_SFR。
 *
 * Note: direct SFR address 0xFF (RSTCFG) is permanently forbidden in AS6 by
 * the MCS-251 hardware safety ruling.
 * 注意：按 MCS-251 硬件安全裁定，AS6 下永久禁止访问直寻址 0xFF（RSTCFG）。
 *===----------------------------------------------------------------------===*/
#if defined(MCS251_LEGACY_V1_SFR)
#define MCS251_SFR(addr) (*(volatile unsigned char *)(addr))
#else
#define MCS251_SFR(addr)                                                       \
  (*(volatile unsigned char __attribute__((address_space(6))) *)(addr))
#endif

/* XFR lives in physical 24-bit window 0x7E0000..0x7EFFFF (32-bit AS0 ptr).
 * XFR 位于 24 位物理窗口 0x7E0000..0x7EFFFF（使用 32 位 AS0 指针访问）。 */
#define MCS251_XFR(addr) (*(volatile unsigned char *)(addr))

/*===----------------------------------------------------------------------===*
 * Memory-space and calling-convention qualifiers
 * 存储空间限定符与调用约定修饰符
 *===----------------------------------------------------------------------===*/
#ifdef __cplusplus
#define MCS251_XDATA __attribute__((address_space(3)))
#define MCS251_CODE __attribute__((address_space(4)))
#else
#define MCS251_XDATA __xdata
#define MCS251_CODE __code
#define MCS251_BIT __bit
#endif
#define MCS251_REENTRANT __reentrant

#ifndef __cplusplus
/* Direct hardware bit-addressable SFR / RAM bit lvalue (C mode only).
 * 硬件可位寻址 SFR / 内部 RAM 位左值（仅 C 模式可用）。 */
#define MCS251_SFR_BIT(bit_addr) __builtin_mcs251_bit_lvalue(bit_addr)
#define MCS251_RAM_BIT(bit_idx)                                                \
  __builtin_mcs251_bit_lvalue(                                                 \
      (bit_idx) +                                                              \
      0 * (int)sizeof(char[((bit_idx) >= 0 && (bit_idx) <= 127) ? 1 : -1]))
#endif /* !__cplusplus */

/*===----------------------------------------------------------------------===*
 * G11 Fixed-address placement attributes
 * G11 固定物理地址放置属性
 *===----------------------------------------------------------------------===*/
#define MCS251_PLACE_AT(addr) __attribute__((mcu_place_at(addr)))
#define MCS251_BIND_AT(addr) __attribute__((mcu_bind_at(addr)))
#define MCS251_RETAIN __attribute__((mcu_retain))
#define MCS251_NOINIT __attribute__((noinit))

/*===----------------------------------------------------------------------===*
 * Interrupt service routine (ISR) & hardware priority attributes
 * 中断服务函数（ISR）与硬件中断优先级属性
 *
 * Usage / 用法示例:
 *   void MCS251_ISR(TMR0_VECTOR) timer0_isr(void) { ... }
 *   void MCS251_ISR_PRIO(UART1_VECTOR, 2) uart1_isr(void) { ... }
 *
 * Call `__mcs251_isr_priority_init()` during startup when using
 * `MCS251_ISR_PRIO(...)` or `--isr-priority=...`; mcs251-lld synthesizes it
 * automatically to program STC32G IP/IPH/IP2/IP2H/IP3/IP3H registers.
 * 使用 `MCS251_ISR_PRIO(...)` 或 `--isr-priority=...` 时请在启动阶段调用
 * `__mcs251_isr_priority_init()`，mcs251-lld 会自动合成该函数并写入
 * STC32G 的 IP/IPH/IP2/IP2H/IP3/IP3H 优先级寄存器。
 *===----------------------------------------------------------------------===*/
#define MCS251_ISR(vector) __attribute__((interrupt(vector)))
#define MCS251_ISR_PRIO(vector, prio)                                          \
  __attribute__((interrupt(vector), mcs251_isr_priority(prio)))

#ifdef __cplusplus
extern "C" {
#endif

/* Linker-synthesized STC32G hardware ISR priority SFR initializer.
 * 由链接器自动合成的 STC32G 硬件中断优先级寄存器初始化函数。 */
extern void __mcs251_isr_priority_init(void);

#ifdef __cplusplus
}
#endif

/*===----------------------------------------------------------------------===*
 * Global interrupt (EA = IE.7, bit 0xAF) atomic critical-section helpers
 * 全局中断位（EA = IE.7，位地址 0xAF）原子临界区辅助函数
 *===----------------------------------------------------------------------===*/
static __inline__ unsigned char __attribute__((__always_inline__))
mcs251_ea_save_disable(void) {
  return __builtin_mcs251_ea_save_disable();
}

static __inline__ void __attribute__((__always_inline__))
mcs251_ea_restore(unsigned char __saved) {
  __builtin_mcs251_ea_restore(__saved);
}

static __inline__ void __attribute__((__always_inline__))
mcs251_ea_enable(void) {
#ifndef __cplusplus
  __builtin_mcs251_bit_lvalue(0xAF) = 1;
#else
  __builtin_mcs251_ea_restore(1u);
#endif
}

static __inline__ void __attribute__((__always_inline__))
mcs251_ea_disable(void) {
#ifndef __cplusplus
  __builtin_mcs251_bit_lvalue(0xAF) = 0;
#else
  (void)__builtin_mcs251_ea_save_disable();
#endif
}

/*===----------------------------------------------------------------------===*
 * Native inline-assembly NOP & Keil-compatible NOP(N) chain (ELF mode)
 * 原生内联汇编 NOP 与 Keil 兼容 NOP(N) 延时宏链（ELF 模式）
 *===----------------------------------------------------------------------===*/
static __inline__ void __attribute__((__always_inline__))
__mcs251_nop_inline(void) {
  __asm__ volatile("nop");
}

#ifndef _nop_
#define _nop_() __mcs251_nop_inline()
#endif

#ifndef NOP1
#define NOP1() _nop_()
#define NOP2() (NOP1(), NOP1())
#define NOP3() (NOP2(), NOP1())
#define NOP4() (NOP3(), NOP1())
#define NOP5() (NOP4(), NOP1())
#define NOP6() (NOP5(), NOP1())
#define NOP7() (NOP6(), NOP1())
#define NOP8() (NOP7(), NOP1())
#define NOP9() (NOP8(), NOP1())
#define NOP10() (NOP9(), NOP1())
#define NOP11() (NOP10(), NOP1())
#define NOP12() (NOP11(), NOP1())
#define NOP13() (NOP12(), NOP1())
#define NOP14() (NOP13(), NOP1())
#define NOP15() (NOP14(), NOP1())
#define NOP16() (NOP15(), NOP1())
#define NOP17() (NOP16(), NOP1())
#define NOP18() (NOP17(), NOP1())
#define NOP19() (NOP18(), NOP1())
#define NOP20() (NOP19(), NOP1())
#define NOP21() (NOP20(), NOP1())
#define NOP22() (NOP21(), NOP1())
#define NOP23() (NOP22(), NOP1())
#define NOP24() (NOP23(), NOP1())
#define NOP25() (NOP24(), NOP1())
#define NOP26() (NOP25(), NOP1())
#define NOP27() (NOP26(), NOP1())
#define NOP28() (NOP27(), NOP1())
#define NOP29() (NOP28(), NOP1())
#define NOP30() (NOP29(), NOP1())
#define NOP31() (NOP30(), NOP1())
#define NOP32() (NOP31(), NOP1())
#define NOP33() (NOP32(), NOP1())
#define NOP34() (NOP33(), NOP1())
#define NOP35() (NOP34(), NOP1())
#define NOP36() (NOP35(), NOP1())
#define NOP37() (NOP36(), NOP1())
#define NOP38() (NOP37(), NOP1())
#define NOP39() (NOP38(), NOP1())
#define NOP40() (NOP39(), NOP1())
#define __MCS251_NOP_CAT(a, b) a##b
#define NOP(n) __MCS251_NOP_CAT(NOP, n)()
#endif /* !NOP1 */

/*===----------------------------------------------------------------------===*
 * STC32G Hardware TFPU (DMA Math Coprocessor) single-precision float helpers
 * STC32G 硬件 TFPU（DMA 数学协处理器）单精度浮点内联封装
 *===----------------------------------------------------------------------===*/
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_sin(float __x) {
  return __builtin_mcs251_tfpu_sin(__x);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_cos(float __x) {
  return __builtin_mcs251_tfpu_cos(__x);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_tan(float __x) {
  return __builtin_mcs251_tfpu_tan(__x);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_atan(float __x) {
  return __builtin_mcs251_tfpu_atan(__x);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_sqrt(float __x) {
  return __builtin_mcs251_tfpu_sqrt(__x);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_add(float __a, float __b) {
  return __builtin_mcs251_tfpu_add(__a, __b);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_sub(float __a, float __b) {
  return __builtin_mcs251_tfpu_sub(__a, __b);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_mul(float __a, float __b) {
  return __builtin_mcs251_tfpu_mul(__a, __b);
}
static __inline__ float __attribute__((__always_inline__))
mcs251_tfpu_div(float __a, float __b) {
  return __builtin_mcs251_tfpu_div(__a, __b);
}

/*===----------------------------------------------------------------------===*
 * Byte-register bitmask helpers (for non-bit-addressable SFRs and XFRs)
 * 字节寄存器位掩码操作宏（用于非位寻址 SFR 与 XFR）
 *===----------------------------------------------------------------------===*/
#define MCS251_BIT_MASK(n) ((unsigned char)(1u << (n)))
#define MCS251_SET_BITS(reg, mask)                                             \
  do {                                                                         \
    (reg) = (unsigned char)((reg) | (unsigned char)(mask));                    \
  } while (0)
#define MCS251_CLR_BITS(reg, mask)                                             \
  do {                                                                         \
    (reg) = (unsigned char)((reg) & (unsigned char)~(unsigned char)(mask));    \
  } while (0)
#define MCS251_MODIFY_REG(reg, clr_mask, set_mask)                             \
  do {                                                                         \
    (reg) = (unsigned char)(((reg) & (unsigned char)~(unsigned char)(clr_mask)) | \
                            ((unsigned char)(set_mask) &                       \
                             (unsigned char)(clr_mask)));                      \
  } while (0)

#endif /* __MCS251_INTRINS_H */
