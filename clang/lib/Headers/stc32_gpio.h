/*===---- stc32_gpio.h - STC32G GPIO Pin, Bit-Address & Mask Abstraction ---===*
 * STC32G GPIO 引脚、硬件位地址与字节位掩码关系头文件
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===*
 * Relationship between Pin, Port SFR, Bit Address, and Byte Mask on STC32G:
 * STC32G 上“引脚 (Pin)、端口 SFR、硬件位地址 (sbit) 与字节掩码 (Mask)”的关系：
 *
 *   A physical pin is uniquely described by a tuple `(port, bit)`:
 *   任意物理引脚均由二元组 `(port, bit)` 唯一确定（port = 0..7，bit = 0..7）：
 *
 *                        STC32_PIN(4, 5)  [alias: STC32_P45]
 *                                 |
 *             +-------------------+-------------------+
 *             |                                       |
 *         port = 4                                 bit = 5
 *             |                                       |
 *     Selects Port SFR/XFRs:                   Determines Mask & Bit Offset:
 *     决定所属端口的全部寄存器：                决定位掩码与位偏移：
 *     - Data SFR (bit-addr): P4   (0xC0)       - Byte Mask: STC32_PIN_MASK = 1U << 5 = 0x20
 *     - Mode SFR (byte-only): P4M1 (0xB3)        用于非位寻址寄存器读改写（P4M1/P4M0/P4PU...）：
 *                             P4M0 (0xB4)        P4M1 &= ~0x20;  P4M0 |= 0x20;
 *     - XFRs (byte-only):     P4PU / P4NCS
 *                             P4SR / P4DR / P4IE
 *             |                                       |
 *             +-------------------+-------------------+
 *                                 v
 *                 Hardware Bit Address (C mode only):
 *                 硬件位地址（仅 C 模式，基址为 8 的倍数才支持）：
 *                 P4 (0xC0) + 5 = 0xC5  -->  STC32_PIN_BIT = P45
 *                 Compiles to single instruction: setb 0xc5 / clr 0xc5 / cpl 0xc5
 *                 直接编译为单条硬件位操作指令：  setb 0xc5 / clr 0xc5 / cpl 0xc5
 *
 * Why both Bit Address and Byte Mask exist / 为何同时存在“位操作”与“字节掩码”：
 *   1. Data ports P0..P7 sit at addresses divisible by 8 (0x80, 0x90, 0xA0,
 *      0xB0, 0xC0, 0xC8, 0xE8, 0xF8), so individual pins support atomic 1-insn
 *      bit access via `STC32_PIN_BIT(pin)` (`P45 = 1`, `P45 ^= 1`).
 *      数据端口 P0..P7 的地址均为 8 的倍数，因此读写引脚电平可直接走单条硬件位指令。
 *   2. Mode/electrical registers (PxM1, PxM0, PxPU, PxNCS, PxSR, PxDR, PxIE)
 *      are NOT bit-addressable, so configuring one pin without disturbing its
 *      7 sibling pins MUST use `STC32_PIN_MASK(pin)` for byte read-modify-write.
 *      模式与电气特性寄存器（PxM1/PxM0/PxPU 等）不支持硬件位寻址，修改单个引脚
 *      时必须使用字节位掩码 `STC32_PIN_MASK(pin)` 做读改写，避免影响同口其余 7 个引脚。
 *
 * Usage / 用法示例（切换引脚只需修改一处宏定义）：
 *   #include <stc32_gpio.h>
 *   #define BOARD_LED  STC32_P45   // 等价于 STC32_PIN(4, 5)
 *
 *   STC32_GPIO_SET_MODE(BOARD_LED, STC32_GPIO_MODE_PP); // 自动操作 P4M1/P4M0 + 0x20
 *   STC32_GPIO_SET(BOARD_LED);                          // 单条 setb 0xc5
 *   STC32_GPIO_TOGGLE(BOARD_LED);                       // 单条 cpl  0xc5
 *===-----------------------------------------------------------------------===*/

#ifndef __STC32_GPIO_H
#define __STC32_GPIO_H

#include <stc32.h>

/*===----------------------------------------------------------------------===*
 * 1. Pin tuple construction & token-pasting primitives
 * 1. 引脚二元组构造与编译期标识符拼接原语（零运行时开销）
 *===----------------------------------------------------------------------===*/
#define __STC32_GPIO_EXPAND(x) x
#define __STC32_GPIO_APPLY(fn, tuple) __STC32_GPIO_EXPAND(fn tuple)

/* Dedicated port/bit paste helpers: expand ONLY `port`/`bit`, keeping fixed
 * prefix/suffix tokens (`P`, `IE`, `M1`, etc.) immune to SFR macro expansion
 * (for example, `IE` is an SFR macro in <stc32g.h>).
 * 专用拼接原语：仅展开 port/bit 参数，防止固定后缀（如 SFR 宏 IE）被提前展开。 */
#define __STC32_GPIO_P_RAW(port) P##port
#define __STC32_GPIO_P(port) __STC32_GPIO_P_RAW(port)
#define __STC32_GPIO_PM1_RAW(port) P##port##M1
#define __STC32_GPIO_PM1(port) __STC32_GPIO_PM1_RAW(port)
#define __STC32_GPIO_PM0_RAW(port) P##port##M0
#define __STC32_GPIO_PM0(port) __STC32_GPIO_PM0_RAW(port)
#define __STC32_GPIO_PPU_RAW(port) P##port##PU
#define __STC32_GPIO_PPU(port) __STC32_GPIO_PPU_RAW(port)
#define __STC32_GPIO_PNCS_RAW(port) P##port##NCS
#define __STC32_GPIO_PNCS(port) __STC32_GPIO_PNCS_RAW(port)
#define __STC32_GPIO_PSR_RAW(port) P##port##SR
#define __STC32_GPIO_PSR(port) __STC32_GPIO_PSR_RAW(port)
#define __STC32_GPIO_PDR_RAW(port) P##port##DR
#define __STC32_GPIO_PDR(port) __STC32_GPIO_PDR_RAW(port)
#define __STC32_GPIO_PIE_RAW(port) P##port##IE
#define __STC32_GPIO_PIE(port) __STC32_GPIO_PIE_RAW(port)
#define __STC32_GPIO_PBIT_RAW(port, bit) P##port##bit
#define __STC32_GPIO_PBIT(port, bit) __STC32_GPIO_PBIT_RAW(port, bit)
#define __STC32_GPIO_PADDR_RAW(port) __STC32_PORT_ADDR_##port
#define __STC32_GPIO_PADDR(port) __STC32_GPIO_PADDR_RAW(port)

/* Canonical pin descriptor: STC32_PIN(port, bit), port in 0..7, bit in 0..7.
 * 标准引脚描述符：STC32_PIN(port, bit)，其中 port = 0..7，bit = 0..7。 */
#define STC32_PIN(port, bit) (port, bit)

/* Extract bare port digit (0..7) and bit index (0..7) from a pin descriptor.
 * `STC32_PIN_PORT(pin)` expands to the bare digit so it can also be passed
 * directly to `STC32_GPIO_SET_PORT_MODE` / `STC32_GPIO_PORT_*`.
 * 从引脚描述符提取端口号（0..7）与位序号（0..7）；端口号保持裸数字形式，
 * 可直接传给 `STC32_GPIO_SET_PORT_MODE` 与 `STC32_GPIO_PORT_*`。 */
#define __STC32_PIN_PORT_IMPL(port, bit) port
#define __STC32_PIN_NUM_IMPL(port, bit) bit
#define STC32_PIN_PORT(pin) __STC32_GPIO_APPLY(__STC32_PIN_PORT_IMPL, pin)
#define STC32_PIN_NUM(pin) __STC32_GPIO_APPLY(__STC32_PIN_NUM_IMPL, pin)

/* Port data SFR base address & hardware bit address (compile-time constants):
 *   STC32_PORT_SFR_ADDR(4)       == 0xC0U (P4)
 *   STC32_PIN_BITADDR(STC32_P45) == 0xC5U (P4.5 = 0xC0 + 5)
 * 端口数据 SFR 基址与硬件位地址（编译期常量，直观对应数据手册地址表）：
 *   P0=0x80, P1=0x90, P2=0xA0, P3=0xB0, P4=0xC0, P5=0xC8, P6=0xE8, P7=0xF8。 */
#define __STC32_PORT_ADDR_0 0x80U
#define __STC32_PORT_ADDR_1 0x90U
#define __STC32_PORT_ADDR_2 0xA0U
#define __STC32_PORT_ADDR_3 0xB0U
#define __STC32_PORT_ADDR_4 0xC0U
#define __STC32_PORT_ADDR_5 0xC8U
#define __STC32_PORT_ADDR_6 0xE8U
#define __STC32_PORT_ADDR_7 0xF8U
#define STC32_PORT_SFR_ADDR(port) __STC32_GPIO_PADDR(port)
#define __STC32_PIN_BITADDR_IMPL(port, bit)                                    \
  ((unsigned char)(STC32_PORT_SFR_ADDR(port) + (unsigned char)(bit)))
#define STC32_PIN_BITADDR(pin) __STC32_GPIO_APPLY(__STC32_PIN_BITADDR_IMPL, pin)

/*===----------------------------------------------------------------------===*
 * 2. Derive Byte Mask, Port SFRs, XFRs, and Hardware Bit Lvalue from a Pin
 * 2. 由单个引脚自动推导字节掩码（Mask）、端口 SFR/XFR 及硬件位左值
 *===----------------------------------------------------------------------===*/

/* Byte bitmask for bit `0..7`: STC32_PIN_MASK(STC32_P45) == 0x20U.
 * 字节位掩码（用于 PxM1/PxM0/PxPU 等字节寄存器）：例如 STC32_P45 -> 0x20U。 */
#define __STC32_PIN_MASK_IMPL(port, bit) ((unsigned char)(1U << (bit)))
#define STC32_PIN_MASK(pin) __STC32_GPIO_APPLY(__STC32_PIN_MASK_IMPL, pin)

/* Standalone bitmask constants / 独立位掩码常量 */
#define STC32_PIN_MASK_0 ((unsigned char)(1U << 0)) /* 0x01 */
#define STC32_PIN_MASK_1 ((unsigned char)(1U << 1)) /* 0x02 */
#define STC32_PIN_MASK_2 ((unsigned char)(1U << 2)) /* 0x04 */
#define STC32_PIN_MASK_3 ((unsigned char)(1U << 3)) /* 0x08 */
#define STC32_PIN_MASK_4 ((unsigned char)(1U << 4)) /* 0x10 */
#define STC32_PIN_MASK_5 ((unsigned char)(1U << 5)) /* 0x20 */
#define STC32_PIN_MASK_6 ((unsigned char)(1U << 6)) /* 0x40 */
#define STC32_PIN_MASK_7 ((unsigned char)(1U << 7)) /* 0x80 */
#define STC32_PIN_MASK_ALL ((unsigned char)0xFFU)

/* Port data & mode SFR accessors (AS6 direct SFRs):
 *   STC32_PIN_PORT_REG(STC32_P45) -> P4
 *   STC32_PIN_M1_REG(STC32_P45)   -> P4M1
 *   STC32_PIN_M0_REG(STC32_P45)   -> P4M0
 * 端口数据与模式寄存器推导（AS6 直寻址 SFR）：
 *   STC32_P45 -> P4 / P4M1 / P4M0。 */
#define __STC32_PORT_REG_IMPL(port, bit) __STC32_GPIO_P(port)
#define __STC32_M1_REG_IMPL(port, bit) __STC32_GPIO_PM1(port)
#define __STC32_M0_REG_IMPL(port, bit) __STC32_GPIO_PM0(port)
#define STC32_PIN_PORT_REG(pin) __STC32_GPIO_APPLY(__STC32_PORT_REG_IMPL, pin)
#define STC32_PIN_M1_REG(pin) __STC32_GPIO_APPLY(__STC32_M1_REG_IMPL, pin)
#define STC32_PIN_M0_REG(pin) __STC32_GPIO_APPLY(__STC32_M0_REG_IMPL, pin)

/* Extended electrical XFR accessors (require EAXSFR() before access):
 * 扩展电气特性寄存器推导（位于 0x7EFE10..0x7EFE37，访问前须调用 EAXSFR()）：
 *   PU  : Internal ~4.1k pull-up enable   (1=enable, 0=disable) / 内部上拉使能
 *   NCS : Schmitt trigger disable         (0=enable, 1=disable) / 施密特触发关闭
 *   SR  : Slew rate control               (0=fast,   1=normal)  / 电平翻转速度
 *   DR  : Drive current control           (0=high,   1=normal)  / 驱动电流控制
 *   IE  : Digital input enable            (1=enable, 0=disable) / 数字输入使能 */
#define __STC32_PU_REG_IMPL(port, bit) __STC32_GPIO_PPU(port)
#define __STC32_NCS_REG_IMPL(port, bit) __STC32_GPIO_PNCS(port)
#define __STC32_SR_REG_IMPL(port, bit) __STC32_GPIO_PSR(port)
#define __STC32_DR_REG_IMPL(port, bit) __STC32_GPIO_PDR(port)
#define __STC32_IE_REG_IMPL(port, bit) __STC32_GPIO_PIE(port)
#define STC32_PIN_PU_REG(pin) __STC32_GPIO_APPLY(__STC32_PU_REG_IMPL, pin)
#define STC32_PIN_NCS_REG(pin) __STC32_GPIO_APPLY(__STC32_NCS_REG_IMPL, pin)
#define STC32_PIN_SR_REG(pin) __STC32_GPIO_APPLY(__STC32_SR_REG_IMPL, pin)
#define STC32_PIN_DR_REG(pin) __STC32_GPIO_APPLY(__STC32_DR_REG_IMPL, pin)
#define STC32_PIN_IE_REG(pin) __STC32_GPIO_APPLY(__STC32_IE_REG_IMPL, pin)

/* Hardware bit-addressable lvalue (C mode only):
 *   STC32_PIN_BIT(STC32_P45) -> P45 -> __builtin_mcs251_bit_lvalue(0xC5U)
 * 硬件可位寻址左值（仅 C 模式可用）：STC32_P45 -> P45（位地址 0xC5）。 */
#ifndef __cplusplus
#define __STC32_PIN_BIT_IMPL(port, bit) __STC32_GPIO_PBIT(port, bit)
#define STC32_PIN_BIT(pin) __STC32_GPIO_APPLY(__STC32_PIN_BIT_IMPL, pin)
#endif

/*===----------------------------------------------------------------------===*
 * 3. Named pin descriptors for all 64 STC32G GPIO pins (P0.0 .. P7.7)
 * 3. STC32G 全部 64 个 GPIO 引脚具名描述符（P0.0 .. P7.7）
 *===----------------------------------------------------------------------===*/
/* Port 0 (SFR 0x80, bit addrs 0x80..0x87, mode P0M1/P0M0 = 0x93/0x94) */
#define STC32_P00 STC32_PIN(0, 0)
#define STC32_P01 STC32_PIN(0, 1)
#define STC32_P02 STC32_PIN(0, 2)
#define STC32_P03 STC32_PIN(0, 3)
#define STC32_P04 STC32_PIN(0, 4)
#define STC32_P05 STC32_PIN(0, 5)
#define STC32_P06 STC32_PIN(0, 6)
#define STC32_P07 STC32_PIN(0, 7)

/* Port 1 (SFR 0x90, bit addrs 0x90..0x97, mode P1M1/P1M0 = 0x91/0x92) */
#define STC32_P10 STC32_PIN(1, 0)
#define STC32_P11 STC32_PIN(1, 1)
#define STC32_P12 STC32_PIN(1, 2)
#define STC32_P13 STC32_PIN(1, 3)
#define STC32_P14 STC32_PIN(1, 4)
#define STC32_P15 STC32_PIN(1, 5)
#define STC32_P16 STC32_PIN(1, 6)
#define STC32_P17 STC32_PIN(1, 7)

/* Port 2 (SFR 0xA0, bit addrs 0xA0..0xA7, mode P2M1/P2M0 = 0x95/0x96) */
#define STC32_P20 STC32_PIN(2, 0)
#define STC32_P21 STC32_PIN(2, 1)
#define STC32_P22 STC32_PIN(2, 2)
#define STC32_P23 STC32_PIN(2, 3)
#define STC32_P24 STC32_PIN(2, 4)
#define STC32_P25 STC32_PIN(2, 5)
#define STC32_P26 STC32_PIN(2, 6)
#define STC32_P27 STC32_PIN(2, 7)

/* Port 3 (SFR 0xB0, bit addrs 0xB0..0xB7, mode P3M1/P3M0 = 0xB1/0xB2) */
#define STC32_P30 STC32_PIN(3, 0)
#define STC32_P31 STC32_PIN(3, 1)
#define STC32_P32 STC32_PIN(3, 2)
#define STC32_P33 STC32_PIN(3, 3)
#define STC32_P34 STC32_PIN(3, 4)
#define STC32_P35 STC32_PIN(3, 5)
#define STC32_P36 STC32_PIN(3, 6)
#define STC32_P37 STC32_PIN(3, 7)

/* Port 4 (SFR 0xC0, bit addrs 0xC0..0xC7, mode P4M1/P4M0 = 0xB3/0xB4) */
#define STC32_P40 STC32_PIN(4, 0)
#define STC32_P41 STC32_PIN(4, 1)
#define STC32_P42 STC32_PIN(4, 2)
#define STC32_P43 STC32_PIN(4, 3)
#define STC32_P44 STC32_PIN(4, 4)
#define STC32_P45 STC32_PIN(4, 5)
#define STC32_P46 STC32_PIN(4, 6)
#define STC32_P47 STC32_PIN(4, 7)

/* Port 5 (SFR 0xC8, bit addrs 0xC8..0xCF, mode P5M1/P5M0 = 0xC9/0xCA) */
#define STC32_P50 STC32_PIN(5, 0)
#define STC32_P51 STC32_PIN(5, 1)
#define STC32_P52 STC32_PIN(5, 2)
#define STC32_P53 STC32_PIN(5, 3)
#define STC32_P54 STC32_PIN(5, 4)
#define STC32_P55 STC32_PIN(5, 5)
#define STC32_P56 STC32_PIN(5, 6)
#define STC32_P57 STC32_PIN(5, 7)

/* Port 6 (SFR 0xE8, bit addrs 0xE8..0xEF, mode P6M1/P6M0 = 0xCB/0xCC) */
#define STC32_P60 STC32_PIN(6, 0)
#define STC32_P61 STC32_PIN(6, 1)
#define STC32_P62 STC32_PIN(6, 2)
#define STC32_P63 STC32_PIN(6, 3)
#define STC32_P64 STC32_PIN(6, 4)
#define STC32_P65 STC32_PIN(6, 5)
#define STC32_P66 STC32_PIN(6, 6)
#define STC32_P67 STC32_PIN(6, 7)

/* Port 7 (SFR 0xF8, bit addrs 0xF8..0xFF, mode P7M1/P7M0 = 0xE1/0xE2) */
#define STC32_P70 STC32_PIN(7, 0)
#define STC32_P71 STC32_PIN(7, 1)
#define STC32_P72 STC32_PIN(7, 2)
#define STC32_P73 STC32_PIN(7, 3)
#define STC32_P74 STC32_PIN(7, 4)
#define STC32_P75 STC32_PIN(7, 5)
#define STC32_P76 STC32_PIN(7, 6)
#define STC32_P77 STC32_PIN(7, 7)

/*===----------------------------------------------------------------------===*
 * 4. GPIO I/O mode constants (PxM1:PxM0 encoding per STC32G datasheet)
 * 4. GPIO 工作模式常量（对应 STC32G 手册 PxM1:PxM0 两位编码）
 *
 *   PxM1 | PxM0 | Mode / 模式
 *   -----+------+---------------------------------------------------------
 *     0  |  0   | STC32_GPIO_MODE_QUASI  (Quasi-bidirectional / 准双向口)
 *     0  |  1   | STC32_GPIO_MODE_PP     (Push-pull output    / 推挽输出)
 *     1  |  0   | STC32_GPIO_MODE_INPUT  (High-Z input only   / 仅输入高阻)
 *     1  |  1   | STC32_GPIO_MODE_OD     (Open-drain output   / 开漏输出)
 *===----------------------------------------------------------------------===*/
#define STC32_GPIO_MODE_QUASI 0U /* M1=0, M0=0: Quasi-bidirectional / 准双向口 */
#define STC32_GPIO_MODE_PP    1U /* M1=0, M0=1: Push-pull output    / 推挽输出 */
#define STC32_GPIO_MODE_INPUT 2U /* M1=1, M0=0: High-Z input        / 高阻输入 */
#define STC32_GPIO_MODE_OD    3U /* M1=1, M0=1: Open-drain          / 开漏输出 */

/* Configure a group of pins on `port` selected by `mask` to `mode`.
 * 按字节掩码 `mask` 批量配置端口 `port` 上多个引脚的工作模式（不影响未选中位）。 */
#define STC32_GPIO_SET_PORT_MODE(port, mask, mode)                             \
  do {                                                                         \
    unsigned char __stc_m = (unsigned char)(mask);                             \
    if (((mode) & 2U) != 0U)                                                   \
      __STC32_GPIO_PM1(port) |= __stc_m;                                       \
    else                                                                       \
      __STC32_GPIO_PM1(port) &= (unsigned char)~__stc_m;                       \
    if (((mode) & 1U) != 0U)                                                   \
      __STC32_GPIO_PM0(port) |= __stc_m;                                       \
    else                                                                       \
      __STC32_GPIO_PM0(port) &= (unsigned char)~__stc_m;                       \
  } while (0)

/* Configure a single pin's I/O mode (automatically derives port & mask).
 * 配置单个引脚的工作模式（自动推导所属 PxM1/PxM0 与位掩码）。 */
#define STC32_GPIO_SET_MODE(pin, mode)                                         \
  STC32_GPIO_SET_PORT_MODE(STC32_PIN_PORT(pin), STC32_PIN_MASK(pin), mode)

/*===----------------------------------------------------------------------===*
 * 5. Single-pin level I/O operations
 *    - C mode  : uses hardware bit lvalue (`setb`/`clr`/`cpl`/`jb`/`jnb`)
 *    - C++ mode: uses byte-mask read-modify-write on P<port>
 * 5. 单引脚电平读写与翻转操作
 *    - C 模式  ：直接展开为硬件位操作指令（setb/clr/cpl/jb/jnb）
 *    - C++ 模式：自动回退为基于字节掩码的端口读改写
 *===----------------------------------------------------------------------===*/
#ifndef __cplusplus
#define STC32_GPIO_SET(pin)                                                    \
  do {                                                                         \
    STC32_PIN_BIT(pin) = 1;                                                    \
  } while (0)
#define STC32_GPIO_CLR(pin)                                                    \
  do {                                                                         \
    STC32_PIN_BIT(pin) = 0;                                                    \
  } while (0)
#define STC32_GPIO_TOGGLE(pin)                                                 \
  do {                                                                         \
    STC32_PIN_BIT(pin) ^= 1;                                                   \
  } while (0)
#define STC32_GPIO_WRITE(pin, val)                                             \
  do {                                                                         \
    STC32_PIN_BIT(pin) = ((val) != 0);                                         \
  } while (0)
#define STC32_GPIO_READ(pin) ((unsigned char)(STC32_PIN_BIT(pin) ? 1U : 0U))
#else /* __cplusplus */
#define STC32_GPIO_SET(pin)                                                    \
  do {                                                                         \
    STC32_PIN_PORT_REG(pin) |= STC32_PIN_MASK(pin);                            \
  } while (0)
#define STC32_GPIO_CLR(pin)                                                    \
  do {                                                                         \
    STC32_PIN_PORT_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);            \
  } while (0)
#define STC32_GPIO_TOGGLE(pin)                                                 \
  do {                                                                         \
    STC32_PIN_PORT_REG(pin) ^= STC32_PIN_MASK(pin);                            \
  } while (0)
#define STC32_GPIO_WRITE(pin, val)                                             \
  do {                                                                         \
    if ((val) != 0)                                                            \
      STC32_PIN_PORT_REG(pin) |= STC32_PIN_MASK(pin);                          \
    else                                                                       \
      STC32_PIN_PORT_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);          \
  } while (0)
#define STC32_GPIO_READ(pin)                                                   \
  ((unsigned char)((STC32_PIN_PORT_REG(pin) & STC32_PIN_MASK(pin)) != 0U))
#endif /* !__cplusplus */

/* Multi-pin port-level mask operations (operate on multiple bits on one port
 * simultaneously using `STC32_PIN_MASK(pinA) | STC32_PIN_MASK(pinB)`).
 * 同端口多引脚批量掩码操作（配合 `STC32_PIN_MASK(pinA) | STC32_PIN_MASK(pinB)`
 * 一次置位/清零/翻转/读取同口多个引脚）。 */
#define STC32_GPIO_PORT_SET(port, mask)                                        \
  do {                                                                         \
    __STC32_GPIO_P(port) |= (unsigned char)(mask);                             \
  } while (0)
#define STC32_GPIO_PORT_CLR(port, mask)                                        \
  do {                                                                         \
    __STC32_GPIO_P(port) &= (unsigned char)~(unsigned char)(mask);             \
  } while (0)
#define STC32_GPIO_PORT_TOGGLE(port, mask)                                     \
  do {                                                                         \
    __STC32_GPIO_P(port) ^= (unsigned char)(mask);                             \
  } while (0)
#define STC32_GPIO_PORT_READ(port, mask)                                       \
  ((unsigned char)(__STC32_GPIO_P(port) & (unsigned char)(mask)))

/*===----------------------------------------------------------------------===*
 * 6. Extended electrical configuration helpers (XFR, requires EAXSFR() active)
 * 6. 引脚扩展电气特性配置宏（操作 XFR，调用前请先开启 EAXSFR()）
 *===----------------------------------------------------------------------===*/
/* Internal pull-up (~4.1 kOhm) / 内部上拉电阻使能与关闭 */
#define STC32_GPIO_PULLUP_EN(pin)                                              \
  do {                                                                         \
    STC32_PIN_PU_REG(pin) |= STC32_PIN_MASK(pin);                              \
  } while (0)
#define STC32_GPIO_PULLUP_DIS(pin)                                             \
  do {                                                                         \
    STC32_PIN_PU_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);              \
  } while (0)

/* Schmitt trigger input (NCS bit: 0=enabled, 1=disabled) / 施密特触发器开关 */
#define STC32_GPIO_SCHMITT_EN(pin)                                             \
  do {                                                                         \
    STC32_PIN_NCS_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);             \
  } while (0)
#define STC32_GPIO_SCHMITT_DIS(pin)                                            \
  do {                                                                         \
    STC32_PIN_NCS_REG(pin) |= STC32_PIN_MASK(pin);                             \
  } while (0)

/* Slew rate (SR bit: 0=fast, 1=normal/slow) / 翻转速度控制（0=高速，1=普通） */
#define STC32_GPIO_FAST_SLEW(pin)                                              \
  do {                                                                         \
    STC32_PIN_SR_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);              \
  } while (0)
#define STC32_GPIO_NORM_SLEW(pin)                                              \
  do {                                                                         \
    STC32_PIN_SR_REG(pin) |= STC32_PIN_MASK(pin);                              \
  } while (0)

/* Drive current (DR bit: 0=enhanced/high, 1=normal) / 驱动电流（0=增强，1=普通） */
#define STC32_GPIO_HIGH_DRIVE(pin)                                             \
  do {                                                                         \
    STC32_PIN_DR_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);              \
  } while (0)
#define STC32_GPIO_NORM_DRIVE(pin)                                             \
  do {                                                                         \
    STC32_PIN_DR_REG(pin) |= STC32_PIN_MASK(pin);                              \
  } while (0)

/* Digital input enable (IE bit: 1=digital, 0=analog/ADC) / 数字输入通道开关 */
#define STC32_GPIO_DIGITAL_IN_EN(pin)                                          \
  do {                                                                         \
    STC32_PIN_IE_REG(pin) |= STC32_PIN_MASK(pin);                              \
  } while (0)
#define STC32_GPIO_DIGITAL_IN_DIS(pin)                                         \
  do {                                                                         \
    STC32_PIN_IE_REG(pin) &= (unsigned char)~STC32_PIN_MASK(pin);              \
  } while (0)

#endif /* __STC32_GPIO_H */
