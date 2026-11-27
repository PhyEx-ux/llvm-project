/*===---- stc32g144k246.h - STC32G144K246 MCU register & vector definitions ---===*
 * STC32G144K246 芯片寄存器与 127 槽中断向量定义头文件
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Target MCU / 目标芯片: STC32G144K246 series (G1 127-slot profile)
 * Designed for LLVM/Clang MCS-251 (default v2 ELF memory contract).
 * 面向 LLVM/Clang MCS-251 工具链设计（默认 v2 ELF 内存契约）。
 *===-----------------------------------------------------------------------===*/

#ifndef __STC32G144K246_H
#define __STC32G144K246_H

#include <mcs251_intrins.h>

/*===----------------------------------------------------------------------===*
 * On-chip memory map constants (for linker flags & placement assertions)
 * 片上存储空间常量（供链接参数配置与静态地址断言使用）
 *===----------------------------------------------------------------------===*/
#ifndef STC32_FLASH_BASE
#define STC32_FLASH_BASE         0xFC2800UL     /* 246 KiB on-chip Flash base / 246KB 片上 Flash 基址 */
#endif
#ifndef STC32_FLASH_SIZE
#define STC32_FLASH_SIZE         0x03D800UL     /* 246 KiB on-chip Flash size / 246KB 片上 Flash 容量 */
#endif
#ifndef STC32_IRAM_SIZE
#define STC32_IRAM_SIZE          0x0080U        /* 128 B direct internal RAM / 128B 直接寻址内部 RAM */
#endif
#ifndef STC32_EDATA_END
#define STC32_EDATA_END          0x3FFFU        /* 16 KiB EDATA upper bound / 16KB 扩展内部 RAM 上界 */
#endif
#ifndef STC32_XDATA_BASE
#define STC32_XDATA_BASE         0x010000UL     /* XDATA base address / 片上 XDATA 基址 */
#endif
#ifndef STC32_XDATA_SIZE
#define STC32_XDATA_SIZE         0x020000UL     /* 128 KiB on-chip XDATA / 128KB 片上 XDATA 容量 */
#endif

/*===----------------------------------------------------------------------===*
 * Direct-address SFRs (0x80..0xFE, 118 registers)
 * 直寻址特殊功能寄存器 SFR（0x80..0xFE，共 118 个）
 * Uses address_space(6) under default v2 contract so access lowers to direct
 * 1-instruction SFR moves (`mov 0xNN, rM` / `mov rM, 0xNN`).
 * 默认 v2 契约下使用 address_space(6)，直接生成单条直寻址 SFR 指令。
 *
 * NOTE / 注意: Direct SFR 0xFF (RSTCFG) is permanently forbidden in AS6 by the
 * MCS-251 hardware safety ruling and is intentionally omitted.
 * 按 MCS-251 硬件安全裁定，AS6 下永久禁止直寻址 0xFF（RSTCFG），此处刻意省略。
 *===----------------------------------------------------------------------===*/
#define P0                       MCS251_SFR(0x80U)
#define SP                       MCS251_SFR(0x81U)
#define DPL                      MCS251_SFR(0x82U)
#define DPH                      MCS251_SFR(0x83U)
#define DPXL                     MCS251_SFR(0x84U)
#define SPH                      MCS251_SFR(0x85U)
#define PCON                     MCS251_SFR(0x87U)
#define TCON                     MCS251_SFR(0x88U)
#define TMOD                     MCS251_SFR(0x89U)
#define TL0                      MCS251_SFR(0x8AU)
#define TL1                      MCS251_SFR(0x8BU)
#define TH0                      MCS251_SFR(0x8CU)
#define TH1                      MCS251_SFR(0x8DU)
#define AUXR                     MCS251_SFR(0x8EU)
#define INTCLKO                  MCS251_SFR(0x8FU)
#define P1                       MCS251_SFR(0x90U)
#define P1M1                     MCS251_SFR(0x91U)
#define P1M0                     MCS251_SFR(0x92U)
#define P0M1                     MCS251_SFR(0x93U)
#define P0M0                     MCS251_SFR(0x94U)
#define P2M1                     MCS251_SFR(0x95U)
#define P2M0                     MCS251_SFR(0x96U)
#define AUXR2                    MCS251_SFR(0x97U)
#define SCON                     MCS251_SFR(0x98U)
#define SBUF                     MCS251_SFR(0x99U)
#define S2CON                    MCS251_SFR(0x9AU)
#define S2BUF                    MCS251_SFR(0x9BU)
#define IRCBAND                  MCS251_SFR(0x9DU)
#define LIRTRIM                  MCS251_SFR(0x9EU)
#define IRTRIM                   MCS251_SFR(0x9FU)
#define P2                       MCS251_SFR(0xA0U)
#define BUS_SPEED                MCS251_SFR(0xA1U)
#define P_SW1                    MCS251_SFR(0xA2U)
#define VRTRIM                   MCS251_SFR(0xA6U)
#define IE                       MCS251_SFR(0xA8U)
#define SADDR                    MCS251_SFR(0xA9U)
#define WKTCL                    MCS251_SFR(0xAAU)
#define WKTCH                    MCS251_SFR(0xABU)
#define S3CON                    MCS251_SFR(0xACU)
#define S3BUF                    MCS251_SFR(0xADU)
#define TA                       MCS251_SFR(0xAEU)
#define IE2                      MCS251_SFR(0xAFU)
#define P3                       MCS251_SFR(0xB0U)
#define P3M1                     MCS251_SFR(0xB1U)
#define P3M0                     MCS251_SFR(0xB2U)
#define P4M1                     MCS251_SFR(0xB3U)
#define P4M0                     MCS251_SFR(0xB4U)
#define IP2                      MCS251_SFR(0xB5U)
#define IP2H                     MCS251_SFR(0xB6U)
#define IPH                      MCS251_SFR(0xB7U)
#define IP                       MCS251_SFR(0xB8U)
#define SADEN                    MCS251_SFR(0xB9U)
#define P_SW2                    MCS251_SFR(0xBAU)
#define P_SW3                    MCS251_SFR(0xBBU)
#define ADC_CONTR                MCS251_SFR(0xBCU)
#define ADC_RES                  MCS251_SFR(0xBDU)
#define ADC_RESL                 MCS251_SFR(0xBEU)
#define P_SW4                    MCS251_SFR(0xBFU)
#define P4                       MCS251_SFR(0xC0U)
#define WDT_CONTR                MCS251_SFR(0xC1U)
#define IAP_DATA                 MCS251_SFR(0xC2U)
#define IAP_ADDRH                MCS251_SFR(0xC3U)
#define IAP_ADDRL                MCS251_SFR(0xC4U)
#define IAP_CMD                  MCS251_SFR(0xC5U)
#define IAP_TRIG                 MCS251_SFR(0xC6U)
#define IAP_CONTR                MCS251_SFR(0xC7U)
#define P5                       MCS251_SFR(0xC8U)
#define P5M1                     MCS251_SFR(0xC9U)
#define P5M0                     MCS251_SFR(0xCAU)
#define P6M1                     MCS251_SFR(0xCBU)
#define P6M0                     MCS251_SFR(0xCCU)
#define SPSTAT                   MCS251_SFR(0xCDU)
#define SPCTL                    MCS251_SFR(0xCEU)
#define SPDAT                    MCS251_SFR(0xCFU)
#define PSW                      MCS251_SFR(0xD0U)
#define PSW1                     MCS251_SFR(0xD1U)
#define T4H                      MCS251_SFR(0xD2U)
#define T4L                      MCS251_SFR(0xD3U)
#define T3H                      MCS251_SFR(0xD4U)
#define T3L                      MCS251_SFR(0xD5U)
#define T2H                      MCS251_SFR(0xD6U)
#define T2L                      MCS251_SFR(0xD7U)
#define TH4                      MCS251_SFR(0xD2U)
#define TL4                      MCS251_SFR(0xD3U)
#define TH3                      MCS251_SFR(0xD4U)
#define TL3                      MCS251_SFR(0xD5U)
#define TH2                      MCS251_SFR(0xD6U)
#define TL2                      MCS251_SFR(0xD7U)
#define USBCLK                   MCS251_SFR(0xDCU)
#define T3T4M                    MCS251_SFR(0xDDU)
#define T4T3M                    MCS251_SFR(0xDDU)
#define ADCCFG                   MCS251_SFR(0xDEU)
#define IP3                      MCS251_SFR(0xDFU)
#define ACC                      MCS251_SFR(0xE0U)
#define P7M1                     MCS251_SFR(0xE1U)
#define P7M0                     MCS251_SFR(0xE2U)
#define DPS                      MCS251_SFR(0xE3U)
#define CMPCR1                   MCS251_SFR(0xE6U)
#define CMPCR2                   MCS251_SFR(0xE7U)
#define P6                       MCS251_SFR(0xE8U)
#define WTST                     MCS251_SFR(0xE9U)
#define CKCON                    MCS251_SFR(0xEAU)
#define MXAX                     MCS251_SFR(0xEBU)
#define USBDAT                   MCS251_SFR(0xECU)
#define DMAIR                    MCS251_SFR(0xEDU)
#define IP3H                     MCS251_SFR(0xEEU)
#define AUXINTIF                 MCS251_SFR(0xEFU)
#define B                        MCS251_SFR(0xF0U)
#define USBCON                   MCS251_SFR(0xF4U)
#define IAP_TPS                  MCS251_SFR(0xF5U)
#define IAP_ADDRE                MCS251_SFR(0xF6U)
#define P7                       MCS251_SFR(0xF8U)
#define LINICR                   MCS251_SFR(0xF9U)
#define LINAR                    MCS251_SFR(0xFAU)
#define LINDR                    MCS251_SFR(0xFBU)
#define USBADR                   MCS251_SFR(0xFCU)
#define S4CON                    MCS251_SFR(0xFDU)
#define S4BUF                    MCS251_SFR(0xFEU)

/*===----------------------------------------------------------------------===*
 * Extended SFR (XFR) bus access switch (P_SW2 bit 7 = EAXFR)
 * 扩展 SFR（XFR）总线访问开关（P_SW2.7 = EAXFR）
 * Enable before accessing 0x7E0000..0x7EFFFF XFR registers; disable afterwards.
 * 访问 0x7E0000..0x7EFFFF 扩展寄存器前先开启 EAXSFR()，完成后关闭 EAXRAM()。
 *===----------------------------------------------------------------------===*/
#ifndef EAXSFR
#define EAXSFR()                 do { P_SW2 |= 0x80U; } while (0)
#endif
#ifndef EAXRAM
#define EAXRAM()                 do { P_SW2 &= (unsigned char)~0x80U; } while (0)
#endif
#define STC32_EAXFR_ENABLE()     EAXSFR()
#define STC32_EAXFR_DISABLE()    EAXRAM()

/*===----------------------------------------------------------------------===*
 * Hardware bit-addressable SFR bits (104 bits, C mode only)
 * 硬件可位寻址 SFR 位定义（共 104 个，仅 C 模式可用）
 * Lowered directly to hardware SETB/CLR/CPL/JB/JNB via __builtin_mcs251_bit_lvalue.
 * 经 __builtin_mcs251_bit_lvalue 直接降至硬件 SETB/CLR/CPL/JB/JNB 位指令。
 * Compiler-managed CPU registers (PSW, ACC, B) are intentionally omitted.
 * 编译器托管的核心寄存器位（PSW、ACC、B）刻意不导出以免破坏代码生成不变量。
 *===----------------------------------------------------------------------===*/
#ifndef __cplusplus
/* P0 (0x80) */
#define P00                      MCS251_SFR_BIT(0x80U) /* P0.0 */
#define P01                      MCS251_SFR_BIT(0x81U) /* P0.1 */
#define P02                      MCS251_SFR_BIT(0x82U) /* P0.2 */
#define P03                      MCS251_SFR_BIT(0x83U) /* P0.3 */
#define P04                      MCS251_SFR_BIT(0x84U) /* P0.4 */
#define P05                      MCS251_SFR_BIT(0x85U) /* P0.5 */
#define P06                      MCS251_SFR_BIT(0x86U) /* P0.6 */
#define P07                      MCS251_SFR_BIT(0x87U) /* P0.7 */
/* TCON (0x88) */
#define TF1                      MCS251_SFR_BIT(0x8FU) /* TCON.7 */
#define TR1                      MCS251_SFR_BIT(0x8EU) /* TCON.6 */
#define TF0                      MCS251_SFR_BIT(0x8DU) /* TCON.5 */
#define TR0                      MCS251_SFR_BIT(0x8CU) /* TCON.4 */
#define IE1                      MCS251_SFR_BIT(0x8BU) /* TCON.3 */
#define IT1                      MCS251_SFR_BIT(0x8AU) /* TCON.2 */
#define IE0                      MCS251_SFR_BIT(0x89U) /* TCON.1 */
#define IT0                      MCS251_SFR_BIT(0x88U) /* TCON.0 */
/* P1 (0x90) */
#define P10                      MCS251_SFR_BIT(0x90U) /* P1.0 */
#define P11                      MCS251_SFR_BIT(0x91U) /* P1.1 */
#define P12                      MCS251_SFR_BIT(0x92U) /* P1.2 */
#define P13                      MCS251_SFR_BIT(0x93U) /* P1.3 */
#define P14                      MCS251_SFR_BIT(0x94U) /* P1.4 */
#define P15                      MCS251_SFR_BIT(0x95U) /* P1.5 */
#define P16                      MCS251_SFR_BIT(0x96U) /* P1.6 */
#define P17                      MCS251_SFR_BIT(0x97U) /* P1.7 */
/* SCON (0x98) */
#define SM0                      MCS251_SFR_BIT(0x9FU) /* SCON.7 */
#define SM1                      MCS251_SFR_BIT(0x9EU) /* SCON.6 */
#define SM2                      MCS251_SFR_BIT(0x9DU) /* SCON.5 */
#define REN                      MCS251_SFR_BIT(0x9CU) /* SCON.4 */
#define TB8                      MCS251_SFR_BIT(0x9BU) /* SCON.3 */
#define RB8                      MCS251_SFR_BIT(0x9AU) /* SCON.2 */
#define TI                       MCS251_SFR_BIT(0x99U) /* SCON.1 */
#define RI                       MCS251_SFR_BIT(0x98U) /* SCON.0 */
/* P2 (0xA0) */
#define P20                      MCS251_SFR_BIT(0xA0U) /* P2.0 */
#define P21                      MCS251_SFR_BIT(0xA1U) /* P2.1 */
#define P22                      MCS251_SFR_BIT(0xA2U) /* P2.2 */
#define P23                      MCS251_SFR_BIT(0xA3U) /* P2.3 */
#define P24                      MCS251_SFR_BIT(0xA4U) /* P2.4 */
#define P25                      MCS251_SFR_BIT(0xA5U) /* P2.5 */
#define P26                      MCS251_SFR_BIT(0xA6U) /* P2.6 */
#define P27                      MCS251_SFR_BIT(0xA7U) /* P2.7 */
/* IE (0xA8) */
#define EA                       MCS251_SFR_BIT(0xAFU) /* IE.7 */
#define ELVD                     MCS251_SFR_BIT(0xAEU) /* IE.6 */
#define EADC                     MCS251_SFR_BIT(0xADU) /* IE.5 */
#define ES                       MCS251_SFR_BIT(0xACU) /* IE.4 */
#define ET1                      MCS251_SFR_BIT(0xABU) /* IE.3 */
#define EX1                      MCS251_SFR_BIT(0xAAU) /* IE.2 */
#define ET0                      MCS251_SFR_BIT(0xA9U) /* IE.1 */
#define EX0                      MCS251_SFR_BIT(0xA8U) /* IE.0 */
/* P3 (0xB0) */
#define P30                      MCS251_SFR_BIT(0xB0U) /* P3.0 */
#define P31                      MCS251_SFR_BIT(0xB1U) /* P3.1 */
#define P32                      MCS251_SFR_BIT(0xB2U) /* P3.2 */
#define P33                      MCS251_SFR_BIT(0xB3U) /* P3.3 */
#define P34                      MCS251_SFR_BIT(0xB4U) /* P3.4 */
#define P35                      MCS251_SFR_BIT(0xB5U) /* P3.5 */
#define P36                      MCS251_SFR_BIT(0xB6U) /* P3.6 */
#define P37                      MCS251_SFR_BIT(0xB7U) /* P3.7 */
#define RD                       MCS251_SFR_BIT(0xB7U) /* P3.7 */
#define WR                       MCS251_SFR_BIT(0xB6U) /* P3.6 */
#define T1                       MCS251_SFR_BIT(0xB5U) /* P3.5 */
#define T0                       MCS251_SFR_BIT(0xB4U) /* P3.4 */
#define INT1                     MCS251_SFR_BIT(0xB3U) /* P3.3 */
#define INT0                     MCS251_SFR_BIT(0xB2U) /* P3.2 */
#define TXD                      MCS251_SFR_BIT(0xB1U) /* P3.1 */
#define RXD                      MCS251_SFR_BIT(0xB0U) /* P3.0 */
/* IP (0xB8) */
#define PPCA                     MCS251_SFR_BIT(0xBFU) /* IP.7 */
#define PLVD                     MCS251_SFR_BIT(0xBEU) /* IP.6 */
#define PADC                     MCS251_SFR_BIT(0xBDU) /* IP.5 */
#define PS                       MCS251_SFR_BIT(0xBCU) /* IP.4 */
#define PT1                      MCS251_SFR_BIT(0xBBU) /* IP.3 */
#define PX1                      MCS251_SFR_BIT(0xBAU) /* IP.2 */
#define PT0                      MCS251_SFR_BIT(0xB9U) /* IP.1 */
#define PX0                      MCS251_SFR_BIT(0xB8U) /* IP.0 */
/* P4 (0xC0) */
#define P40                      MCS251_SFR_BIT(0xC0U) /* P4.0 */
#define P41                      MCS251_SFR_BIT(0xC1U) /* P4.1 */
#define P42                      MCS251_SFR_BIT(0xC2U) /* P4.2 */
#define P43                      MCS251_SFR_BIT(0xC3U) /* P4.3 */
#define P44                      MCS251_SFR_BIT(0xC4U) /* P4.4 */
#define P45                      MCS251_SFR_BIT(0xC5U) /* P4.5 */
#define P46                      MCS251_SFR_BIT(0xC6U) /* P4.6 */
#define P47                      MCS251_SFR_BIT(0xC7U) /* P4.7 */
/* P5 (0xC8) */
#define P50                      MCS251_SFR_BIT(0xC8U) /* P5.0 */
#define P51                      MCS251_SFR_BIT(0xC9U) /* P5.1 */
#define P52                      MCS251_SFR_BIT(0xCAU) /* P5.2 */
#define P53                      MCS251_SFR_BIT(0xCBU) /* P5.3 */
#define P54                      MCS251_SFR_BIT(0xCCU) /* P5.4 */
#define P55                      MCS251_SFR_BIT(0xCDU) /* P5.5 */
#define P56                      MCS251_SFR_BIT(0xCEU) /* P5.6 */
#define P57                      MCS251_SFR_BIT(0xCFU) /* P5.7 */
/* P6 (0xE8) */
#define P60                      MCS251_SFR_BIT(0xE8U) /* P6.0 */
#define P61                      MCS251_SFR_BIT(0xE9U) /* P6.1 */
#define P62                      MCS251_SFR_BIT(0xEAU) /* P6.2 */
#define P63                      MCS251_SFR_BIT(0xEBU) /* P6.3 */
#define P64                      MCS251_SFR_BIT(0xECU) /* P6.4 */
#define P65                      MCS251_SFR_BIT(0xEDU) /* P6.5 */
#define P66                      MCS251_SFR_BIT(0xEEU) /* P6.6 */
#define P67                      MCS251_SFR_BIT(0xEFU) /* P6.7 */
/* P7 (0xF8) */
#define P70                      MCS251_SFR_BIT(0xF8U) /* P7.0 */
#define P71                      MCS251_SFR_BIT(0xF9U) /* P7.1 */
#define P72                      MCS251_SFR_BIT(0xFAU) /* P7.2 */
#define P73                      MCS251_SFR_BIT(0xFBU) /* P7.3 */
#define P74                      MCS251_SFR_BIT(0xFCU) /* P7.4 */
#define P75                      MCS251_SFR_BIT(0xFDU) /* P7.5 */
#define P76                      MCS251_SFR_BIT(0xFEU) /* P7.6 */
#define P77                      MCS251_SFR_BIT(0xFFU) /* P7.7 */
#endif /* !__cplusplus */

/*===----------------------------------------------------------------------===*
 * Bitmask constants for non-bit-addressable control SFRs (191 masks)
 * 非位寻址控制 SFR 的具名位掩码常量（共 191 个）
 * Base SFRs whose address is not a multiple of 8 have no hardware bit address;
 * use byte read-modify-write (e.g. `P_SW2 |= P_SW2_EAXFR;`).
 * 基址非 8 倍数的 SFR 不支持硬件位寻址，请使用字节读改写（如 `P_SW2 |= P_SW2_EAXFR;`）。
 *===----------------------------------------------------------------------===*/
/* PCON (0x87) */
#define PCON_SMOD                    ((unsigned char)(1U << 7)) /* 0x80 */
#define PCON_SMOD0                   ((unsigned char)(1U << 6)) /* 0x40 */
#define PCON_LVDF                    ((unsigned char)(1U << 5)) /* 0x20 */
#define PCON_POF                     ((unsigned char)(1U << 4)) /* 0x10 */
#define PCON_GF1                     ((unsigned char)(1U << 3)) /* 0x08 */
#define PCON_GF0                     ((unsigned char)(1U << 2)) /* 0x04 */
#define PCON_PD                      ((unsigned char)(1U << 1)) /* 0x02 */
#define PCON_IDL                     ((unsigned char)(1U << 0)) /* 0x01 */
/* TMOD (0x89) */
#define TMOD_T1_GATE                 ((unsigned char)(1U << 7)) /* 0x80 */
#define TMOD_T1_CT                   ((unsigned char)(1U << 6)) /* 0x40 */
#define TMOD_T1_M1                   ((unsigned char)(1U << 5)) /* 0x20 */
#define TMOD_T1_M0                   ((unsigned char)(1U << 4)) /* 0x10 */
#define TMOD_T0_GATE                 ((unsigned char)(1U << 3)) /* 0x08 */
#define TMOD_T0_CT                   ((unsigned char)(1U << 2)) /* 0x04 */
#define TMOD_T0_M1                   ((unsigned char)(1U << 1)) /* 0x02 */
#define TMOD_T0_M0                   ((unsigned char)(1U << 0)) /* 0x01 */
/* AUXR (0x8E) */
#define AUXR_T0x12                   ((unsigned char)(1U << 7)) /* 0x80 */
#define AUXR_T1x12                   ((unsigned char)(1U << 6)) /* 0x40 */
#define AUXR_S1M0x6                  ((unsigned char)(1U << 5)) /* 0x20 */
#define AUXR_T2R                     ((unsigned char)(1U << 4)) /* 0x10 */
#define AUXR_T2_CT                   ((unsigned char)(1U << 3)) /* 0x08 */
#define AUXR_T2x12                   ((unsigned char)(1U << 2)) /* 0x04 */
#define AUXR_EXTRAM                  ((unsigned char)(1U << 1)) /* 0x02 */
#define AUXR_S1BRT                   ((unsigned char)(1U << 0)) /* 0x01 */
/* INTCLKO (0x8F) */
#define INTCLKO_EX4                  ((unsigned char)(1U << 6)) /* 0x40 */
#define INTCLKO_EX3                  ((unsigned char)(1U << 5)) /* 0x20 */
#define INTCLKO_EX2                  ((unsigned char)(1U << 4)) /* 0x10 */
#define INTCLKO_T2CLKO               ((unsigned char)(1U << 2)) /* 0x04 */
#define INTCLKO_T1CLKO               ((unsigned char)(1U << 1)) /* 0x02 */
#define INTCLKO_T0CLKO               ((unsigned char)(1U << 0)) /* 0x01 */
/* AUXR2 (0x97) */
#define AUXR2_RAMTINY                ((unsigned char)(1U << 7)) /* 0x80 */
#define AUXR2_CPUMODE                ((unsigned char)(1U << 6)) /* 0x40 */
#define AUXR2_LINSEL                 ((unsigned char)(1U << 5)) /* 0x20 */
#define AUXR2_LIN2EN                 ((unsigned char)(1U << 4)) /* 0x10 */
#define AUXR2_CANSEL                 ((unsigned char)(1U << 3)) /* 0x08 */
#define AUXR2_CAN2EN                 ((unsigned char)(1U << 2)) /* 0x04 */
#define AUXR2_CANEN                  ((unsigned char)(1U << 1)) /* 0x02 */
#define AUXR2_LINEN                  ((unsigned char)(1U << 0)) /* 0x01 */
/* S2CON (0x9A) */
#define S2CON_S2SM0                  ((unsigned char)(1U << 7)) /* 0x80 */
#define S2CON_S2SM1                  ((unsigned char)(1U << 6)) /* 0x40 */
#define S2CON_S2SM2                  ((unsigned char)(1U << 5)) /* 0x20 */
#define S2CON_S2REN                  ((unsigned char)(1U << 4)) /* 0x10 */
#define S2CON_S2TB8                  ((unsigned char)(1U << 3)) /* 0x08 */
#define S2CON_S2RB8                  ((unsigned char)(1U << 2)) /* 0x04 */
#define S2CON_S2TI                   ((unsigned char)(1U << 1)) /* 0x02 */
#define S2CON_S2RI                   ((unsigned char)(1U << 0)) /* 0x01 */
/* IRCBAND (0x9D) */
#define IRCBAND_USBCKS               ((unsigned char)(1U << 7)) /* 0x80 */
#define IRCBAND_USBCKS2              ((unsigned char)(1U << 6)) /* 0x40 */
#define IRCBAND_HIRCSEL1             ((unsigned char)(1U << 1)) /* 0x02 */
#define IRCBAND_HIRCSEL0             ((unsigned char)(1U << 0)) /* 0x01 */
/* P_SW1 (0xA2) */
#define P_SW1_S1_S1                  ((unsigned char)(1U << 7)) /* 0x80 */
#define P_SW1_S1_S0                  ((unsigned char)(1U << 6)) /* 0x40 */
#define P_SW1_CAN_S1                 ((unsigned char)(1U << 5)) /* 0x20 */
#define P_SW1_CAN_S0                 ((unsigned char)(1U << 4)) /* 0x10 */
#define P_SW1_SPI_S1                 ((unsigned char)(1U << 3)) /* 0x08 */
#define P_SW1_SPI_S0                 ((unsigned char)(1U << 2)) /* 0x04 */
#define P_SW1_LIN_S1                 ((unsigned char)(1U << 1)) /* 0x02 */
#define P_SW1_LIN_S0                 ((unsigned char)(1U << 0)) /* 0x01 */
/* WKTCH (0xAB) */
#define WKTCH_WKTEN                  ((unsigned char)(1U << 7)) /* 0x80 */
/* S3CON (0xAC) */
#define S3CON_S3SM0                  ((unsigned char)(1U << 7)) /* 0x80 */
#define S3CON_S3ST3                  ((unsigned char)(1U << 6)) /* 0x40 */
#define S3CON_S3SM2                  ((unsigned char)(1U << 5)) /* 0x20 */
#define S3CON_S3REN                  ((unsigned char)(1U << 4)) /* 0x10 */
#define S3CON_S3TB8                  ((unsigned char)(1U << 3)) /* 0x08 */
#define S3CON_S3RB8                  ((unsigned char)(1U << 2)) /* 0x04 */
#define S3CON_S3TI                   ((unsigned char)(1U << 1)) /* 0x02 */
#define S3CON_S3RI                   ((unsigned char)(1U << 0)) /* 0x01 */
/* IE2 (0xAF) */
#define IE2_EUSB                     ((unsigned char)(1U << 7)) /* 0x80 */
#define IE2_ET4                      ((unsigned char)(1U << 6)) /* 0x40 */
#define IE2_ET3                      ((unsigned char)(1U << 5)) /* 0x20 */
#define IE2_ES4                      ((unsigned char)(1U << 4)) /* 0x10 */
#define IE2_ES3                      ((unsigned char)(1U << 3)) /* 0x08 */
#define IE2_ET2                      ((unsigned char)(1U << 2)) /* 0x04 */
#define IE2_ESPI                     ((unsigned char)(1U << 1)) /* 0x02 */
#define IE2_ES2                      ((unsigned char)(1U << 0)) /* 0x01 */
/* IP2 (0xB5) */
#define IP2_PUSB                     ((unsigned char)(1U << 7)) /* 0x80 */
#define IP2_PI2C                     ((unsigned char)(1U << 6)) /* 0x40 */
#define IP2_PCMP                     ((unsigned char)(1U << 5)) /* 0x20 */
#define IP2_PX4                      ((unsigned char)(1U << 4)) /* 0x10 */
#define IP2_PPWMB                    ((unsigned char)(1U << 3)) /* 0x08 */
#define IP2_PPWMA                    ((unsigned char)(1U << 2)) /* 0x04 */
#define IP2_PSPI                     ((unsigned char)(1U << 1)) /* 0x02 */
#define IP2_PS2                      ((unsigned char)(1U << 0)) /* 0x01 */
/* IP2H (0xB6) */
#define IP2H_PUSBH                   ((unsigned char)(1U << 7)) /* 0x80 */
#define IP2H_PI2CH                   ((unsigned char)(1U << 6)) /* 0x40 */
#define IP2H_PCMPH                   ((unsigned char)(1U << 5)) /* 0x20 */
#define IP2H_PX4H                    ((unsigned char)(1U << 4)) /* 0x10 */
#define IP2H_PPWMBH                  ((unsigned char)(1U << 3)) /* 0x08 */
#define IP2H_PPWMAH                  ((unsigned char)(1U << 2)) /* 0x04 */
#define IP2H_PSPIH                   ((unsigned char)(1U << 1)) /* 0x02 */
#define IP2H_PS2H                    ((unsigned char)(1U << 0)) /* 0x01 */
/* IPH (0xB7) */
#define IPH_PPCAH                    ((unsigned char)(1U << 7)) /* 0x80 */
#define IPH_PLVDH                    ((unsigned char)(1U << 6)) /* 0x40 */
#define IPH_PADCH                    ((unsigned char)(1U << 5)) /* 0x20 */
#define IPH_PSH                      ((unsigned char)(1U << 4)) /* 0x10 */
#define IPH_PT1H                     ((unsigned char)(1U << 3)) /* 0x08 */
#define IPH_PX1H                     ((unsigned char)(1U << 2)) /* 0x04 */
#define IPH_PT0H                     ((unsigned char)(1U << 1)) /* 0x02 */
#define IPH_PX0H                     ((unsigned char)(1U << 0)) /* 0x01 */
/* P_SW2 (0xBA) */
#define P_SW2_EAXFR                  ((unsigned char)(1U << 7)) /* 0x80 */
#define P_SW2_I2C_S1                 ((unsigned char)(1U << 5)) /* 0x20 */
#define P_SW2_I2C_S0                 ((unsigned char)(1U << 4)) /* 0x10 */
#define P_SW2_CMPO_S                 ((unsigned char)(1U << 3)) /* 0x08 */
#define P_SW2_S4_S                   ((unsigned char)(1U << 2)) /* 0x04 */
#define P_SW2_S3_S                   ((unsigned char)(1U << 1)) /* 0x02 */
#define P_SW2_S2_S                   ((unsigned char)(1U << 0)) /* 0x01 */
/* P_SW3 (0xBB) */
#define P_SW3_I2S_S1                 ((unsigned char)(1U << 7)) /* 0x80 */
#define P_SW3_I2S_S0                 ((unsigned char)(1U << 6)) /* 0x40 */
#define P_SW3_S2SPI_S1               ((unsigned char)(1U << 5)) /* 0x20 */
#define P_SW3_S2SPI_S0               ((unsigned char)(1U << 4)) /* 0x10 */
#define P_SW3_S1SPI_S1               ((unsigned char)(1U << 3)) /* 0x08 */
#define P_SW3_S1SPI_S0               ((unsigned char)(1U << 2)) /* 0x04 */
#define P_SW3_CAN2_S1                ((unsigned char)(1U << 1)) /* 0x02 */
#define P_SW3_CAN2_S0                ((unsigned char)(1U << 0)) /* 0x01 */
/* ADC_CONTR (0xBC) */
#define ADC_CONTR_ADC_POWER          ((unsigned char)(1U << 7)) /* 0x80 */
#define ADC_CONTR_ADC_START          ((unsigned char)(1U << 6)) /* 0x40 */
#define ADC_CONTR_ADC_FLAG           ((unsigned char)(1U << 5)) /* 0x20 */
#define ADC_CONTR_ADC_EPWMT          ((unsigned char)(1U << 4)) /* 0x10 */
/* P_SW4 (0xBF) */
#define P_SW4_QSPI_S1                ((unsigned char)(1U << 1)) /* 0x02 */
#define P_SW4_QSPI_S0                ((unsigned char)(1U << 0)) /* 0x01 */
/* WDT_CONTR (0xC1) */
#define WDT_CONTR_WDT_FLAG           ((unsigned char)(1U << 7)) /* 0x80 */
#define WDT_CONTR_EN_WDT             ((unsigned char)(1U << 5)) /* 0x20 */
#define WDT_CONTR_CLR_WDT            ((unsigned char)(1U << 4)) /* 0x10 */
#define WDT_CONTR_IDL_WDT            ((unsigned char)(1U << 3)) /* 0x08 */
/* IAP_CONTR (0xC7) */
#define IAP_CONTR_IAPEN              ((unsigned char)(1U << 7)) /* 0x80 */
#define IAP_CONTR_SWBS               ((unsigned char)(1U << 6)) /* 0x40 */
#define IAP_CONTR_SWRST              ((unsigned char)(1U << 5)) /* 0x20 */
#define IAP_CONTR_CMD_FAIL           ((unsigned char)(1U << 4)) /* 0x10 */
#define IAP_CONTR_SWBS2              ((unsigned char)(1U << 3)) /* 0x08 */
/* SPSTAT (0xCD) */
#define SPSTAT_SPIF                  ((unsigned char)(1U << 7)) /* 0x80 */
#define SPSTAT_WCOL                  ((unsigned char)(1U << 6)) /* 0x40 */
/* SPCTL (0xCE) */
#define SPCTL_SSIG                   ((unsigned char)(1U << 7)) /* 0x80 */
#define SPCTL_SPEN                   ((unsigned char)(1U << 6)) /* 0x40 */
#define SPCTL_DORD                   ((unsigned char)(1U << 5)) /* 0x20 */
#define SPCTL_MSTR                   ((unsigned char)(1U << 4)) /* 0x10 */
#define SPCTL_CPOL                   ((unsigned char)(1U << 3)) /* 0x08 */
#define SPCTL_CPHA                   ((unsigned char)(1U << 2)) /* 0x04 */
#define SPCTL_SPR1                   ((unsigned char)(1U << 1)) /* 0x02 */
#define SPCTL_SPR0                   ((unsigned char)(1U << 0)) /* 0x01 */
/* T4T3M (0xDD) */
#define T4T3M_T4R                    ((unsigned char)(1U << 7)) /* 0x80 */
#define T4T3M_T4_CT                  ((unsigned char)(1U << 6)) /* 0x40 */
#define T4T3M_T4x12                  ((unsigned char)(1U << 5)) /* 0x20 */
#define T4T3M_T4CLKO                 ((unsigned char)(1U << 4)) /* 0x10 */
#define T4T3M_T3R                    ((unsigned char)(1U << 3)) /* 0x08 */
#define T4T3M_T3_CT                  ((unsigned char)(1U << 2)) /* 0x04 */
#define T4T3M_T3x12                  ((unsigned char)(1U << 1)) /* 0x02 */
#define T4T3M_T3CLKO                 ((unsigned char)(1U << 0)) /* 0x01 */
/* ADCCFG (0xDE) */
#define ADCCFG_RESFMT                ((unsigned char)(1U << 5)) /* 0x20 */
/* IP3 (0xDF) */
#define IP3_PI2S                     ((unsigned char)(1U << 3)) /* 0x08 */
#define IP3_PRTC                     ((unsigned char)(1U << 2)) /* 0x04 */
#define IP3_PS4                      ((unsigned char)(1U << 1)) /* 0x02 */
#define IP3_PS3                      ((unsigned char)(1U << 0)) /* 0x01 */
/* CMPCR1 (0xE6) */
#define CMPCR1_CMPEN                 ((unsigned char)(1U << 7)) /* 0x80 */
#define CMPCR1_CMPIF                 ((unsigned char)(1U << 6)) /* 0x40 */
#define CMPCR1_PIE                   ((unsigned char)(1U << 5)) /* 0x20 */
#define CMPCR1_NIE                   ((unsigned char)(1U << 4)) /* 0x10 */
#define CMPCR1_CMPOE                 ((unsigned char)(1U << 1)) /* 0x02 */
#define CMPCR1_CMPRES                ((unsigned char)(1U << 0)) /* 0x01 */
/* CMPCR2 (0xE7) */
#define CMPCR2_INVCMPO               ((unsigned char)(1U << 7)) /* 0x80 */
#define CMPCR2_DISFLT                ((unsigned char)(1U << 6)) /* 0x40 */
/* CKCON (0xEA) */
#define CKCON_RAMEXE                 ((unsigned char)(1U << 7)) /* 0x80 */
/* IP3H (0xEE) */
#define IP3H_PI2SH                   ((unsigned char)(1U << 3)) /* 0x08 */
#define IP3H_PRTCH                   ((unsigned char)(1U << 2)) /* 0x04 */
#define IP3H_PS4H                    ((unsigned char)(1U << 1)) /* 0x02 */
#define IP3H_PS3H                    ((unsigned char)(1U << 0)) /* 0x01 */
/* AUXINTIF (0xEF) */
#define AUXINTIF_INT4IF              ((unsigned char)(1U << 6)) /* 0x40 */
#define AUXINTIF_INT3IF              ((unsigned char)(1U << 5)) /* 0x20 */
#define AUXINTIF_INT2IF              ((unsigned char)(1U << 4)) /* 0x10 */
#define AUXINTIF_T4IF                ((unsigned char)(1U << 2)) /* 0x04 */
#define AUXINTIF_T3IF                ((unsigned char)(1U << 1)) /* 0x02 */
#define AUXINTIF_T2IF                ((unsigned char)(1U << 0)) /* 0x01 */
/* USBCON (0xF4) */
#define USBCON_ENUSB                 ((unsigned char)(1U << 7)) /* 0x80 */
#define USBCON_ENUSBRST              ((unsigned char)(1U << 6)) /* 0x40 */
#define USBCON_PS2M                  ((unsigned char)(1U << 5)) /* 0x20 */
#define USBCON_PUEN                  ((unsigned char)(1U << 4)) /* 0x10 */
#define USBCON_PDEN                  ((unsigned char)(1U << 3)) /* 0x08 */
#define USBCON_DFREC                 ((unsigned char)(1U << 2)) /* 0x04 */
#define USBCON_DP                    ((unsigned char)(1U << 1)) /* 0x02 */
#define USBCON_DM                    ((unsigned char)(1U << 0)) /* 0x01 */
/* LINICR (0xF9) */
#define LINICR_PLINH                 ((unsigned char)(1U << 3)) /* 0x08 */
#define LINICR_LINIF                 ((unsigned char)(1U << 2)) /* 0x04 */
#define LINICR_LINIE                 ((unsigned char)(1U << 1)) /* 0x02 */
#define LINICR_PLINL                 ((unsigned char)(1U << 0)) /* 0x01 */
/* S4CON (0xFD) */
#define S4CON_S4SM0                  ((unsigned char)(1U << 7)) /* 0x80 */
#define S4CON_S4ST4                  ((unsigned char)(1U << 6)) /* 0x40 */
#define S4CON_S4SM2                  ((unsigned char)(1U << 5)) /* 0x20 */
#define S4CON_S4REN                  ((unsigned char)(1U << 4)) /* 0x10 */
#define S4CON_S4TB8                  ((unsigned char)(1U << 3)) /* 0x08 */
#define S4CON_S4RB8                  ((unsigned char)(1U << 2)) /* 0x04 */
#define S4CON_S4TI                   ((unsigned char)(1U << 1)) /* 0x02 */
#define S4CON_S4RI                   ((unsigned char)(1U << 0)) /* 0x01 */

/*===----------------------------------------------------------------------===*
 * Extended SFRs (XFR, physical window 0x7E0000..0x7EFFFF, 1717 registers)
 * 扩展功能寄存器 XFR（物理窗口 0x7E0000..0x7EFFFF，共 1717 个）
 * Requires EAXFR=1 (call EAXSFR()) before access; uses 32-bit AS0 pointers.
 * 访问前须先开启 EAXSFR()；底层使用 32 位 AS0 指针访问。
 *===----------------------------------------------------------------------===*/
#define CLKSEL                   MCS251_XFR(0x7EFE00UL)
#define CLKDIV                   MCS251_XFR(0x7EFE01UL)
#define HIRCCR                   MCS251_XFR(0x7EFE02UL)
#define XOSCCR                   MCS251_XFR(0x7EFE03UL)
#define IRC32KCR                 MCS251_XFR(0x7EFE04UL)
#define MCLKOCR                  MCS251_XFR(0x7EFE05UL)
#define IRCDB                    MCS251_XFR(0x7EFE06UL)
#define IRC48MCR                 MCS251_XFR(0x7EFE07UL)
#define X32KCR                   MCS251_XFR(0x7EFE08UL)
#define IRC48MATRIM              MCS251_XFR(0x7EFE09UL)
#define IRC48MBTRIM              MCS251_XFR(0x7EFE0AUL)
#define HSCLKDIV                 MCS251_XFR(0x7EFE0BUL)
#define HPLLCR                   MCS251_XFR(0x7EFE0CUL)
#define HPLLPDIV                 MCS251_XFR(0x7EFE0DUL)
#define HPLL2CR                  MCS251_XFR(0x7EFE0EUL)
#define HPLL2PDIV                MCS251_XFR(0x7EFE0FUL)
#define P0PU                     MCS251_XFR(0x7EFE10UL)
#define P1PU                     MCS251_XFR(0x7EFE11UL)
#define P2PU                     MCS251_XFR(0x7EFE12UL)
#define P3PU                     MCS251_XFR(0x7EFE13UL)
#define P4PU                     MCS251_XFR(0x7EFE14UL)
#define P5PU                     MCS251_XFR(0x7EFE15UL)
#define P6PU                     MCS251_XFR(0x7EFE16UL)
#define P7PU                     MCS251_XFR(0x7EFE17UL)
#define P0NCS                    MCS251_XFR(0x7EFE18UL)
#define P1NCS                    MCS251_XFR(0x7EFE19UL)
#define P2NCS                    MCS251_XFR(0x7EFE1AUL)
#define P3NCS                    MCS251_XFR(0x7EFE1BUL)
#define P4NCS                    MCS251_XFR(0x7EFE1CUL)
#define P5NCS                    MCS251_XFR(0x7EFE1DUL)
#define P6NCS                    MCS251_XFR(0x7EFE1EUL)
#define P7NCS                    MCS251_XFR(0x7EFE1FUL)
#define P0SR                     MCS251_XFR(0x7EFE20UL)
#define P1SR                     MCS251_XFR(0x7EFE21UL)
#define P2SR                     MCS251_XFR(0x7EFE22UL)
#define P3SR                     MCS251_XFR(0x7EFE23UL)
#define P4SR                     MCS251_XFR(0x7EFE24UL)
#define P5SR                     MCS251_XFR(0x7EFE25UL)
#define P6SR                     MCS251_XFR(0x7EFE26UL)
#define P7SR                     MCS251_XFR(0x7EFE27UL)
#define P0DR                     MCS251_XFR(0x7EFE28UL)
#define P1DR                     MCS251_XFR(0x7EFE29UL)
#define P2DR                     MCS251_XFR(0x7EFE2AUL)
#define P3DR                     MCS251_XFR(0x7EFE2BUL)
#define P4DR                     MCS251_XFR(0x7EFE2CUL)
#define P5DR                     MCS251_XFR(0x7EFE2DUL)
#define P6DR                     MCS251_XFR(0x7EFE2EUL)
#define P7DR                     MCS251_XFR(0x7EFE2FUL)
#define P0IE                     MCS251_XFR(0x7EFE30UL)
#define P1IE                     MCS251_XFR(0x7EFE31UL)
#define P2IE                     MCS251_XFR(0x7EFE32UL)
#define P3IE                     MCS251_XFR(0x7EFE33UL)
#define P4IE                     MCS251_XFR(0x7EFE34UL)
#define P5IE                     MCS251_XFR(0x7EFE35UL)
#define P6IE                     MCS251_XFR(0x7EFE36UL)
#define P7IE                     MCS251_XFR(0x7EFE37UL)
#define P0PD                     MCS251_XFR(0x7EFE40UL)
#define P1PD                     MCS251_XFR(0x7EFE41UL)
#define P2PD                     MCS251_XFR(0x7EFE42UL)
#define P3PD                     MCS251_XFR(0x7EFE43UL)
#define P4PD                     MCS251_XFR(0x7EFE44UL)
#define P5PD                     MCS251_XFR(0x7EFE45UL)
#define P6PD                     MCS251_XFR(0x7EFE46UL)
#define P7PD                     MCS251_XFR(0x7EFE47UL)
#define P0BP                     MCS251_XFR(0x7EFE48UL)
#define P1BP                     MCS251_XFR(0x7EFE49UL)
#define P2BP                     MCS251_XFR(0x7EFE4AUL)
#define P3BP                     MCS251_XFR(0x7EFE4BUL)
#define P4BP                     MCS251_XFR(0x7EFE4CUL)
#define P5BP                     MCS251_XFR(0x7EFE4DUL)
#define P6BP                     MCS251_XFR(0x7EFE4EUL)
#define P7BP                     MCS251_XFR(0x7EFE4FUL)
#define LCMIFCFG                 MCS251_XFR(0x7EFE50UL)
#define LCMIFCFG2                MCS251_XFR(0x7EFE51UL)
#define LCMIFCR                  MCS251_XFR(0x7EFE52UL)
#define LCMIFSTA                 MCS251_XFR(0x7EFE53UL)
#define LCMIFDATL                MCS251_XFR(0x7EFE54UL)
#define LCMIFDATH                MCS251_XFR(0x7EFE55UL)
#define LCMIFPSCR                MCS251_XFR(0x7EFE56UL)
#define RSTCR6                   MCS251_XFR(0x7EFE58UL)
#define RSTCR7                   MCS251_XFR(0x7EFE59UL)
#define RSTCR8                   MCS251_XFR(0x7EFE5AUL)
#define RSTCR9                   MCS251_XFR(0x7EFE5BUL)
#define RSTCR10                  MCS251_XFR(0x7EFE5CUL)
#define RSTCR11                  MCS251_XFR(0x7EFE5DUL)
#define RSTCR12                  MCS251_XFR(0x7EFE5EUL)
#define RSTCR13                  MCS251_XFR(0x7EFE5FUL)
#define RTCCR                    MCS251_XFR(0x7EFE60UL)
#define RTCCFG                   MCS251_XFR(0x7EFE61UL)
#define RTCIEN                   MCS251_XFR(0x7EFE62UL)
#define RTCIF                    MCS251_XFR(0x7EFE63UL)
#define ALAHOUR                  MCS251_XFR(0x7EFE64UL)
#define ALAMIN                   MCS251_XFR(0x7EFE65UL)
#define ALASEC                   MCS251_XFR(0x7EFE66UL)
#define ALASSEC                  MCS251_XFR(0x7EFE67UL)
#define INIYEAR                  MCS251_XFR(0x7EFE68UL)
#define INIMONTH                 MCS251_XFR(0x7EFE69UL)
#define INIDAY                   MCS251_XFR(0x7EFE6AUL)
#define INIHOUR                  MCS251_XFR(0x7EFE6BUL)
#define INIMIN                   MCS251_XFR(0x7EFE6CUL)
#define INISEC                   MCS251_XFR(0x7EFE6DUL)
#define INISSEC                  MCS251_XFR(0x7EFE6EUL)
#define RTCWEEK                  MCS251_XFR(0x7EFE6FUL)
#define RTCYEAR                  MCS251_XFR(0x7EFE70UL)
#define RTCMONTH                 MCS251_XFR(0x7EFE71UL)
#define RTCDAY                   MCS251_XFR(0x7EFE72UL)
#define RTCHOUR                  MCS251_XFR(0x7EFE73UL)
#define RTCMIN                   MCS251_XFR(0x7EFE74UL)
#define RTCSEC                   MCS251_XFR(0x7EFE75UL)
#define RTCSSEC                  MCS251_XFR(0x7EFE76UL)
#define T11CR                    MCS251_XFR(0x7EFE78UL)
#define T11PS                    MCS251_XFR(0x7EFE79UL)
#define T11H                     MCS251_XFR(0x7EFE7AUL)
#define T11L                     MCS251_XFR(0x7EFE7BUL)
#define I2CCFG                   MCS251_XFR(0x7EFE80UL)
#define I2CMSCR                  MCS251_XFR(0x7EFE81UL)
#define I2CMSST                  MCS251_XFR(0x7EFE82UL)
#define I2CSLCR                  MCS251_XFR(0x7EFE83UL)
#define I2CSLST                  MCS251_XFR(0x7EFE84UL)
#define I2CSLADR                 MCS251_XFR(0x7EFE85UL)
#define I2CTXD                   MCS251_XFR(0x7EFE86UL)
#define I2CRXD                   MCS251_XFR(0x7EFE87UL)
#define I2CMSAUX                 MCS251_XFR(0x7EFE88UL)
#define I2CPSCR                  MCS251_XFR(0x7EFE89UL)
#define SPI_CLKDIV               MCS251_XFR(0x7EFE90UL)
#define PWMA_CLKDIV              MCS251_XFR(0x7EFE91UL)
#define PWMB_CLKDIV              MCS251_XFR(0x7EFE92UL)
#define TFPU_CLKDIV              MCS251_XFR(0x7EFE93UL)
#define I2S_CLKDIV               MCS251_XFR(0x7EFE94UL)
#define I2S2_CLKDIV              MCS251_XFR(0x7EFE95UL)
#define PWMC_CLKDIV              MCS251_XFR(0x7EFE96UL)
#define PWMD_CLKDIV              MCS251_XFR(0x7EFE97UL)
#define RSTFLAG                  MCS251_XFR(0x7EFE99UL)
#define RSTCR0                   MCS251_XFR(0x7EFE9AUL)
#define RSTCR1                   MCS251_XFR(0x7EFE9BUL)
#define RSTCR2                   MCS251_XFR(0x7EFE9CUL)
#define RSTCR3                   MCS251_XFR(0x7EFE9DUL)
#define RSTCR4                   MCS251_XFR(0x7EFE9EUL)
#define RSTCR5                   MCS251_XFR(0x7EFE9FUL)
#define TM0PS                    MCS251_XFR(0x7EFEA0UL)
#define TM1PS                    MCS251_XFR(0x7EFEA1UL)
#define TM2PS                    MCS251_XFR(0x7EFEA2UL)
#define TM3PS                    MCS251_XFR(0x7EFEA3UL)
#define TM4PS                    MCS251_XFR(0x7EFEA4UL)
#define ADCTIM                   MCS251_XFR(0x7EFEA8UL)
#define INTXIT                   MCS251_XFR(0x7EFEA9UL)
#define ADC_RESH                 MCS251_XFR(0x7EFEAAUL)
#define ADC2_RESH                MCS251_XFR(0x7EFEABUL)
#define TIMER_PS                 MCS251_XFR(0x7EFEACUL)
#define ADCEXCFG                 MCS251_XFR(0x7EFEADUL)
#define CMPEXCFG                 MCS251_XFR(0x7EFEAEUL)
#define ADCEXCFG2                MCS251_XFR(0x7EFEAFUL)
#define PWMA_ETRPS               MCS251_XFR(0x7EFEB0UL)
#define PWMA_ENO                 MCS251_XFR(0x7EFEB1UL)
#define PWMA_PS                  MCS251_XFR(0x7EFEB2UL)
#define PWMA_IOAUX               MCS251_XFR(0x7EFEB3UL)
#define PWMB_ETRPS               MCS251_XFR(0x7EFEB4UL)
#define PWMB_ENO                 MCS251_XFR(0x7EFEB5UL)
#define PWMB_PS                  MCS251_XFR(0x7EFEB6UL)
#define PWMB_IOAUX               MCS251_XFR(0x7EFEB7UL)
#define PWMA_PS2                 MCS251_XFR(0x7EFEB8UL)
#define PWMA_RCRH                MCS251_XFR(0x7EFEB9UL)
#define PWMB_RCRH                MCS251_XFR(0x7EFEBAUL)
#define PWMA_CR1                 MCS251_XFR(0x7EFEC0UL)
#define PWMA_CR2                 MCS251_XFR(0x7EFEC1UL)
#define PWMA_SMCR                MCS251_XFR(0x7EFEC2UL)
#define PWMA_ETR                 MCS251_XFR(0x7EFEC3UL)
#define PWMA_IER                 MCS251_XFR(0x7EFEC4UL)
#define PWMA_SR1                 MCS251_XFR(0x7EFEC5UL)
#define PWMA_SR2                 MCS251_XFR(0x7EFEC6UL)
#define PWMA_EGR                 MCS251_XFR(0x7EFEC7UL)
#define PWMA_CCMR1               MCS251_XFR(0x7EFEC8UL)
#define PWMA_CCMR2               MCS251_XFR(0x7EFEC9UL)
#define PWMA_CCMR3               MCS251_XFR(0x7EFECAUL)
#define PWMA_CCMR4               MCS251_XFR(0x7EFECBUL)
#define PWMA_CCER1               MCS251_XFR(0x7EFECCUL)
#define PWMA_CCER2               MCS251_XFR(0x7EFECDUL)
#define PWMA_CNTRH               MCS251_XFR(0x7EFECEUL)
#define PWMA_CNTRL               MCS251_XFR(0x7EFECFUL)
#define PWMA_PSCRH               MCS251_XFR(0x7EFED0UL)
#define PWMA_PSCRL               MCS251_XFR(0x7EFED1UL)
#define PWMA_ARRH                MCS251_XFR(0x7EFED2UL)
#define PWMA_ARRL                MCS251_XFR(0x7EFED3UL)
#define PWMA_RCR                 MCS251_XFR(0x7EFED4UL)
#define PWMA_CCR1H               MCS251_XFR(0x7EFED5UL)
#define PWMA_CCR1L               MCS251_XFR(0x7EFED6UL)
#define PWMA_CCR2H               MCS251_XFR(0x7EFED7UL)
#define PWMA_CCR2L               MCS251_XFR(0x7EFED8UL)
#define PWMA_CCR3H               MCS251_XFR(0x7EFED9UL)
#define PWMA_CCR3L               MCS251_XFR(0x7EFEDAUL)
#define PWMA_CCR4H               MCS251_XFR(0x7EFEDBUL)
#define PWMA_CCR4L               MCS251_XFR(0x7EFEDCUL)
#define PWMA_BKR                 MCS251_XFR(0x7EFEDDUL)
#define PWMA_DTR                 MCS251_XFR(0x7EFEDEUL)
#define PWMA_OISR                MCS251_XFR(0x7EFEDFUL)
#define PWMB_CR1                 MCS251_XFR(0x7EFEE0UL)
#define PWMB_CR2                 MCS251_XFR(0x7EFEE1UL)
#define PWMB_SMCR                MCS251_XFR(0x7EFEE2UL)
#define PWMB_ETR                 MCS251_XFR(0x7EFEE3UL)
#define PWMB_IER                 MCS251_XFR(0x7EFEE4UL)
#define PWMB_SR1                 MCS251_XFR(0x7EFEE5UL)
#define PWMB_SR2                 MCS251_XFR(0x7EFEE6UL)
#define PWMB_EGR                 MCS251_XFR(0x7EFEE7UL)
#define PWMB_CCMR1               MCS251_XFR(0x7EFEE8UL)
#define PWMB_CCMR2               MCS251_XFR(0x7EFEE9UL)
#define PWMB_CCMR3               MCS251_XFR(0x7EFEEAUL)
#define PWMB_CCMR4               MCS251_XFR(0x7EFEEBUL)
#define PWMB_CCER1               MCS251_XFR(0x7EFEECUL)
#define PWMB_CCER2               MCS251_XFR(0x7EFEEDUL)
#define PWMB_CNTRH               MCS251_XFR(0x7EFEEEUL)
#define PWMB_CNTRL               MCS251_XFR(0x7EFEEFUL)
#define PWMB_PSCRH               MCS251_XFR(0x7EFEF0UL)
#define PWMB_PSCRL               MCS251_XFR(0x7EFEF1UL)
#define PWMB_ARRH                MCS251_XFR(0x7EFEF2UL)
#define PWMB_ARRL                MCS251_XFR(0x7EFEF3UL)
#define PWMB_RCR                 MCS251_XFR(0x7EFEF4UL)
#define PWMB_CCR5H               MCS251_XFR(0x7EFEF5UL)
#define PWMB_CCR5L               MCS251_XFR(0x7EFEF6UL)
#define PWMB_CCR6H               MCS251_XFR(0x7EFEF7UL)
#define PWMB_CCR6L               MCS251_XFR(0x7EFEF8UL)
#define PWMB_CCR7H               MCS251_XFR(0x7EFEF9UL)
#define PWMB_CCR7L               MCS251_XFR(0x7EFEFAUL)
#define PWMB_CCR8H               MCS251_XFR(0x7EFEFBUL)
#define PWMB_CCR8L               MCS251_XFR(0x7EFEFCUL)
#define PWMB_BKR                 MCS251_XFR(0x7EFEFDUL)
#define PWMB_DTR                 MCS251_XFR(0x7EFEFEUL)
#define PWMB_OISR                MCS251_XFR(0x7EFEFFUL)
#define P0INTE                   MCS251_XFR(0x7EFD00UL)
#define P1INTE                   MCS251_XFR(0x7EFD01UL)
#define P2INTE                   MCS251_XFR(0x7EFD02UL)
#define P3INTE                   MCS251_XFR(0x7EFD03UL)
#define P4INTE                   MCS251_XFR(0x7EFD04UL)
#define P5INTE                   MCS251_XFR(0x7EFD05UL)
#define P6INTE                   MCS251_XFR(0x7EFD06UL)
#define P7INTE                   MCS251_XFR(0x7EFD07UL)
#define P8INTE                   MCS251_XFR(0x7EFD08UL)
#define P9INTE                   MCS251_XFR(0x7EFD09UL)
#define PAINTE                   MCS251_XFR(0x7EFD0AUL)
#define PBINTE                   MCS251_XFR(0x7EFD0BUL)
#define PCINTE                   MCS251_XFR(0x7EFD0CUL)
#define PDINTE                   MCS251_XFR(0x7EFD0DUL)
#define PEINTE                   MCS251_XFR(0x7EFD0EUL)
#define PFINTE                   MCS251_XFR(0x7EFD0FUL)
#define P0INTF                   MCS251_XFR(0x7EFD10UL)
#define P1INTF                   MCS251_XFR(0x7EFD11UL)
#define P2INTF                   MCS251_XFR(0x7EFD12UL)
#define P3INTF                   MCS251_XFR(0x7EFD13UL)
#define P4INTF                   MCS251_XFR(0x7EFD14UL)
#define P5INTF                   MCS251_XFR(0x7EFD15UL)
#define P6INTF                   MCS251_XFR(0x7EFD16UL)
#define P7INTF                   MCS251_XFR(0x7EFD17UL)
#define P8INTF                   MCS251_XFR(0x7EFD18UL)
#define P9INTF                   MCS251_XFR(0x7EFD19UL)
#define PAINTF                   MCS251_XFR(0x7EFD1AUL)
#define PBINTF                   MCS251_XFR(0x7EFD1BUL)
#define PCINTF                   MCS251_XFR(0x7EFD1CUL)
#define PDINTF                   MCS251_XFR(0x7EFD1DUL)
#define PEINTF                   MCS251_XFR(0x7EFD1EUL)
#define PFINTF                   MCS251_XFR(0x7EFD1FUL)
#define P0IM0                    MCS251_XFR(0x7EFD20UL)
#define P1IM0                    MCS251_XFR(0x7EFD21UL)
#define P2IM0                    MCS251_XFR(0x7EFD22UL)
#define P3IM0                    MCS251_XFR(0x7EFD23UL)
#define P4IM0                    MCS251_XFR(0x7EFD24UL)
#define P5IM0                    MCS251_XFR(0x7EFD25UL)
#define P6IM0                    MCS251_XFR(0x7EFD26UL)
#define P7IM0                    MCS251_XFR(0x7EFD27UL)
#define P8IM0                    MCS251_XFR(0x7EFD28UL)
#define P9IM0                    MCS251_XFR(0x7EFD29UL)
#define PAIM0                    MCS251_XFR(0x7EFD2AUL)
#define PBIM0                    MCS251_XFR(0x7EFD2BUL)
#define PCIM0                    MCS251_XFR(0x7EFD2CUL)
#define PDIM0                    MCS251_XFR(0x7EFD2DUL)
#define PEIM0                    MCS251_XFR(0x7EFD2EUL)
#define PFIM0                    MCS251_XFR(0x7EFD2FUL)
#define P0IM1                    MCS251_XFR(0x7EFD30UL)
#define P1IM1                    MCS251_XFR(0x7EFD31UL)
#define P2IM1                    MCS251_XFR(0x7EFD32UL)
#define P3IM1                    MCS251_XFR(0x7EFD33UL)
#define P4IM1                    MCS251_XFR(0x7EFD34UL)
#define P5IM1                    MCS251_XFR(0x7EFD35UL)
#define P6IM1                    MCS251_XFR(0x7EFD36UL)
#define P7IM1                    MCS251_XFR(0x7EFD37UL)
#define P8IM1                    MCS251_XFR(0x7EFD38UL)
#define P9IM1                    MCS251_XFR(0x7EFD39UL)
#define PAIM1                    MCS251_XFR(0x7EFD3AUL)
#define PBIM1                    MCS251_XFR(0x7EFD3BUL)
#define PCIM1                    MCS251_XFR(0x7EFD3CUL)
#define PDIM1                    MCS251_XFR(0x7EFD3DUL)
#define PEIM1                    MCS251_XFR(0x7EFD3EUL)
#define PFIM1                    MCS251_XFR(0x7EFD3FUL)
#define P0WKUE                   MCS251_XFR(0x7EFD40UL)
#define P1WKUE                   MCS251_XFR(0x7EFD41UL)
#define P2WKUE                   MCS251_XFR(0x7EFD42UL)
#define P3WKUE                   MCS251_XFR(0x7EFD43UL)
#define P4WKUE                   MCS251_XFR(0x7EFD44UL)
#define P5WKUE                   MCS251_XFR(0x7EFD45UL)
#define P6WKUE                   MCS251_XFR(0x7EFD46UL)
#define P7WKUE                   MCS251_XFR(0x7EFD47UL)
#define P8WKUE                   MCS251_XFR(0x7EFD48UL)
#define P9WKUE                   MCS251_XFR(0x7EFD49UL)
#define PAWKUE                   MCS251_XFR(0x7EFD4AUL)
#define PBWKUE                   MCS251_XFR(0x7EFD4BUL)
#define PCWKUE                   MCS251_XFR(0x7EFD4CUL)
#define PDWKUE                   MCS251_XFR(0x7EFD4DUL)
#define PEWKUE                   MCS251_XFR(0x7EFD4EUL)
#define PFWKUE                   MCS251_XFR(0x7EFD4FUL)
#define PINIPL                   MCS251_XFR(0x7EFD60UL)
#define PINIPH                   MCS251_XFR(0x7EFD61UL)
#define PIN2IPL                  MCS251_XFR(0x7EFD62UL)
#define PIN2IPH                  MCS251_XFR(0x7EFD63UL)
#define P_SWX1                   MCS251_XFR(0x7EFD69UL)
#define P_SWX2                   MCS251_XFR(0x7EFD6AUL)
#define P_SWX3                   MCS251_XFR(0x7EFD6BUL)
#define P_SWX4                   MCS251_XFR(0x7EFD6CUL)
#define P_SWX5                   MCS251_XFR(0x7EFD6DUL)
#define P_SWX6                   MCS251_XFR(0x7EFD6EUL)
#define P_SWX7                   MCS251_XFR(0x7EFD6FUL)
#define UR1TOCR                  MCS251_XFR(0x7EFD70UL)
#define UR1TOSR                  MCS251_XFR(0x7EFD71UL)
#define UR1TOTH                  MCS251_XFR(0x7EFD72UL)
#define UR1TOTL                  MCS251_XFR(0x7EFD73UL)
#define UR2TOCR                  MCS251_XFR(0x7EFD74UL)
#define UR2TOSR                  MCS251_XFR(0x7EFD75UL)
#define UR2TOTH                  MCS251_XFR(0x7EFD76UL)
#define UR2TOTL                  MCS251_XFR(0x7EFD77UL)
#define UR3TOCR                  MCS251_XFR(0x7EFD78UL)
#define UR3TOSR                  MCS251_XFR(0x7EFD79UL)
#define UR3TOTH                  MCS251_XFR(0x7EFD7AUL)
#define UR3TOTL                  MCS251_XFR(0x7EFD7BUL)
#define UR4TOCR                  MCS251_XFR(0x7EFD7CUL)
#define UR4TOSR                  MCS251_XFR(0x7EFD7DUL)
#define UR4TOTH                  MCS251_XFR(0x7EFD7EUL)
#define UR4TOTL                  MCS251_XFR(0x7EFD7FUL)
#define SPITOCR                  MCS251_XFR(0x7EFD80UL)
#define SPITOSR                  MCS251_XFR(0x7EFD81UL)
#define SPITOTH                  MCS251_XFR(0x7EFD82UL)
#define SPITOTL                  MCS251_XFR(0x7EFD83UL)
#define I2CTOCR                  MCS251_XFR(0x7EFD84UL)
#define I2CTOSR                  MCS251_XFR(0x7EFD85UL)
#define I2CTOTH                  MCS251_XFR(0x7EFD86UL)
#define I2CTOTL                  MCS251_XFR(0x7EFD87UL)
#define UR1TOTE                  MCS251_XFR(0x7EFD88UL)
#define UR2TOTE                  MCS251_XFR(0x7EFD89UL)
#define UR3TOTE                  MCS251_XFR(0x7EFD8AUL)
#define UR4TOTE                  MCS251_XFR(0x7EFD8BUL)
#define SPITOTE                  MCS251_XFR(0x7EFD8CUL)
#define I2CTOTE                  MCS251_XFR(0x7EFD8DUL)
#define I2S2CR                   MCS251_XFR(0x7EFD90UL)
#define I2S2SR                   MCS251_XFR(0x7EFD91UL)
#define I2S2DRH                  MCS251_XFR(0x7EFD92UL)
#define I2S2DRL                  MCS251_XFR(0x7EFD93UL)
#define I2S2PRH                  MCS251_XFR(0x7EFD94UL)
#define I2S2PRL                  MCS251_XFR(0x7EFD95UL)
#define I2S2CFGH                 MCS251_XFR(0x7EFD96UL)
#define I2S2CFGL                 MCS251_XFR(0x7EFD97UL)
#define I2SCR                    MCS251_XFR(0x7EFD98UL)
#define I2SSR                    MCS251_XFR(0x7EFD99UL)
#define I2SDRH                   MCS251_XFR(0x7EFD9AUL)
#define I2SDRL                   MCS251_XFR(0x7EFD9BUL)
#define I2SPRH                   MCS251_XFR(0x7EFD9CUL)
#define I2SPRL                   MCS251_XFR(0x7EFD9DUL)
#define I2SCFGH                  MCS251_XFR(0x7EFD9EUL)
#define I2SCFGL                  MCS251_XFR(0x7EFD9FUL)
#define I2SMD                    MCS251_XFR(0x7EFDA0UL)
#define I2SMCKDIV                MCS251_XFR(0x7EFDA1UL)
#define I2SINSEL                 MCS251_XFR(0x7EFDA2UL)
#define I2S2MD                   MCS251_XFR(0x7EFDA4UL)
#define I2S2MCKDIV               MCS251_XFR(0x7EFDA5UL)
#define I2S2INSEL                MCS251_XFR(0x7EFDA6UL)
#define CRECR                    MCS251_XFR(0x7EFDA8UL)
#define CRECNTH                  MCS251_XFR(0x7EFDA9UL)
#define CRECNTL                  MCS251_XFR(0x7EFDAAUL)
#define CRERES                   MCS251_XFR(0x7EFDABUL)
#define CRECFG                   MCS251_XFR(0x7EFDACUL)
#define S2CFG                    MCS251_XFR(0x7EFDB4UL)
#define S2ADDR                   MCS251_XFR(0x7EFDB5UL)
#define S2ADEN                   MCS251_XFR(0x7EFDB6UL)
#define S3CFG                    MCS251_XFR(0x7EFDB8UL)
#define S3ADDR                   MCS251_XFR(0x7EFDB9UL)
#define S3ADEN                   MCS251_XFR(0x7EFDBAUL)
#define S4CFG                    MCS251_XFR(0x7EFDBCUL)
#define S4ADDR                   MCS251_XFR(0x7EFDBDUL)
#define S4ADEN                   MCS251_XFR(0x7EFDBEUL)
#define USARTCR1                 MCS251_XFR(0x7EFDC0UL)
#define USARTCR2                 MCS251_XFR(0x7EFDC1UL)
#define USARTCR3                 MCS251_XFR(0x7EFDC2UL)
#define USARTCR4                 MCS251_XFR(0x7EFDC3UL)
#define USARTCR5                 MCS251_XFR(0x7EFDC4UL)
#define USARTGTR                 MCS251_XFR(0x7EFDC5UL)
#define USARTBRH                 MCS251_XFR(0x7EFDC6UL)
#define USARTBRL                 MCS251_XFR(0x7EFDC7UL)
#define USART2CR1                MCS251_XFR(0x7EFDC8UL)
#define USART2CR2                MCS251_XFR(0x7EFDC9UL)
#define USART2CR3                MCS251_XFR(0x7EFDCAUL)
#define USART2CR4                MCS251_XFR(0x7EFDCBUL)
#define USART2CR5                MCS251_XFR(0x7EFDCCUL)
#define USART2GTR                MCS251_XFR(0x7EFDCDUL)
#define USART2BRH                MCS251_XFR(0x7EFDCEUL)
#define USART2BRL                MCS251_XFR(0x7EFDCFUL)
#define USART3CR1                MCS251_XFR(0x7EFDD0UL)
#define USART3CR2                MCS251_XFR(0x7EFDD1UL)
#define USART3CR3                MCS251_XFR(0x7EFDD2UL)
#define USART3CR4                MCS251_XFR(0x7EFDD3UL)
#define USART3CR5                MCS251_XFR(0x7EFDD4UL)
#define USART3GTR                MCS251_XFR(0x7EFDD5UL)
#define USART3BRH                MCS251_XFR(0x7EFDD6UL)
#define USART3BRL                MCS251_XFR(0x7EFDD7UL)
#define USART4CR1                MCS251_XFR(0x7EFDD8UL)
#define USART4CR2                MCS251_XFR(0x7EFDD9UL)
#define USART4CR3                MCS251_XFR(0x7EFDDAUL)
#define USART4CR4                MCS251_XFR(0x7EFDDBUL)
#define USART4CR5                MCS251_XFR(0x7EFDDCUL)
#define USART4GTR                MCS251_XFR(0x7EFDDDUL)
#define USART4BRH                MCS251_XFR(0x7EFDDEUL)
#define USART4BRL                MCS251_XFR(0x7EFDDFUL)
#define CHIPID0                  MCS251_XFR(0x7EFDE0UL)
#define CHIPID1                  MCS251_XFR(0x7EFDE1UL)
#define CHIPID2                  MCS251_XFR(0x7EFDE2UL)
#define CHIPID3                  MCS251_XFR(0x7EFDE3UL)
#define CHIPID4                  MCS251_XFR(0x7EFDE4UL)
#define CHIPID5                  MCS251_XFR(0x7EFDE5UL)
#define CHIPID6                  MCS251_XFR(0x7EFDE6UL)
#define CHIPID7                  MCS251_XFR(0x7EFDE7UL)
#define CHIPID8                  MCS251_XFR(0x7EFDE8UL)
#define CHIPID9                  MCS251_XFR(0x7EFDE9UL)
#define CHIPID10                 MCS251_XFR(0x7EFDEAUL)
#define CHIPID11                 MCS251_XFR(0x7EFDEBUL)
#define CHIPID12                 MCS251_XFR(0x7EFDECUL)
#define CHIPID13                 MCS251_XFR(0x7EFDEDUL)
#define CHIPID14                 MCS251_XFR(0x7EFDEEUL)
#define CHIPID15                 MCS251_XFR(0x7EFDEFUL)
#define CHIPID16                 MCS251_XFR(0x7EFDF0UL)
#define CHIPID17                 MCS251_XFR(0x7EFDF1UL)
#define CHIPID18                 MCS251_XFR(0x7EFDF2UL)
#define CHIPID19                 MCS251_XFR(0x7EFDF3UL)
#define CHIPID20                 MCS251_XFR(0x7EFDF4UL)
#define CHIPID21                 MCS251_XFR(0x7EFDF5UL)
#define CHIPID22                 MCS251_XFR(0x7EFDF6UL)
#define CHIPID23                 MCS251_XFR(0x7EFDF7UL)
#define CHIPID24                 MCS251_XFR(0x7EFDF8UL)
#define CHIPID25                 MCS251_XFR(0x7EFDF9UL)
#define CHIPID26                 MCS251_XFR(0x7EFDFAUL)
#define CHIPID27                 MCS251_XFR(0x7EFDFBUL)
#define CHIPID28                 MCS251_XFR(0x7EFDFCUL)
#define CHIPID29                 MCS251_XFR(0x7EFDFDUL)
#define CHIPID30                 MCS251_XFR(0x7EFDFEUL)
#define CHIPID31                 MCS251_XFR(0x7EFDFFUL)
#define CRC_CR                   MCS251_XFR(0x7EFB30UL)
#define CRC_DIN                  MCS251_XFR(0x7EFB31UL)
#define CRC_DOH                  MCS251_XFR(0x7EFB32UL)
#define CRC_DOL                  MCS251_XFR(0x7EFB33UL)
#define CRC_DO0                  MCS251_XFR(0x7EFB32UL)
#define CRC_DO1                  MCS251_XFR(0x7EFB33UL)
#define CRC_DO2                  MCS251_XFR(0x7EFB34UL)
#define CRC_DO3                  MCS251_XFR(0x7EFB35UL)
#define ADC2_CONTR               MCS251_XFR(0x7EFB38UL)
#define ADC2CFG                  MCS251_XFR(0x7EFB39UL)
#define ADC2_RES                 MCS251_XFR(0x7EFB3AUL)
#define ADC2_RESL                MCS251_XFR(0x7EFB3BUL)
#define ADC2TIM                  MCS251_XFR(0x7EFB3CUL)
#define ADC2EXCFG                MCS251_XFR(0x7EFB3DUL)
#define ADC2INTR                 MCS251_XFR(0x7EFB3EUL)
#define ADC2EXCFG2               MCS251_XFR(0x7EFB3FUL)
#define CMP1_6P6N_CR1            MCS251_XFR(0x7EFB70UL)
#define CMP1_6P6N_CR2            MCS251_XFR(0x7EFB71UL)
#define CMP1_6P6N_CR3            MCS251_XFR(0x7EFB72UL)
#define CMP1_6P6N_CR4            MCS251_XFR(0x7EFB73UL)
#define CMP2_6P6N_CR1            MCS251_XFR(0x7EFB74UL)
#define CMP2_6P6N_CR2            MCS251_XFR(0x7EFB75UL)
#define CMP2_6P6N_CR3            MCS251_XFR(0x7EFB76UL)
#define CMP2_6P6N_CR4            MCS251_XFR(0x7EFB77UL)
#define CMP3_6P6N_CR1            MCS251_XFR(0x7EFB78UL)
#define CMP3_6P6N_CR2            MCS251_XFR(0x7EFB79UL)
#define CMP3_6P6N_CR3            MCS251_XFR(0x7EFB7AUL)
#define CMP3_6P6N_CR4            MCS251_XFR(0x7EFB7BUL)
#define CMP4_6P6N_CR1            MCS251_XFR(0x7EFB7CUL)
#define CMP4_6P6N_CR2            MCS251_XFR(0x7EFB7DUL)
#define CMP4_6P6N_CR3            MCS251_XFR(0x7EFB7EUL)
#define CMP4_6P6N_CR4            MCS251_XFR(0x7EFB7FUL)
#define TM5PS                    MCS251_XFR(0x7EFBB0UL)
#define TM6PS                    MCS251_XFR(0x7EFBB1UL)
#define T6T5CFG                  MCS251_XFR(0x7EFBB2UL)
#define T6T5M                    MCS251_XFR(0x7EFBB3UL)
#define TH6                      MCS251_XFR(0x7EFBB4UL)
#define TL6                      MCS251_XFR(0x7EFBB5UL)
#define TH5                      MCS251_XFR(0x7EFBB6UL)
#define TL5                      MCS251_XFR(0x7EFBB7UL)
#define TM7PS                    MCS251_XFR(0x7EFBB8UL)
#define TM8PS                    MCS251_XFR(0x7EFBB9UL)
#define T8T7CFG                  MCS251_XFR(0x7EFBBAUL)
#define T8T7M                    MCS251_XFR(0x7EFBBBUL)
#define TH8                      MCS251_XFR(0x7EFBBCUL)
#define TL8                      MCS251_XFR(0x7EFBBDUL)
#define TH7                      MCS251_XFR(0x7EFBBEUL)
#define TL7                      MCS251_XFR(0x7EFBBFUL)
#define TM9PS                    MCS251_XFR(0x7EFBC0UL)
#define TM10PS                   MCS251_XFR(0x7EFBC1UL)
#define T10T9CFG                 MCS251_XFR(0x7EFBC2UL)
#define T10T9M                   MCS251_XFR(0x7EFBC3UL)
#define TH10                     MCS251_XFR(0x7EFBC4UL)
#define TL10                     MCS251_XFR(0x7EFBC5UL)
#define TH9                      MCS251_XFR(0x7EFBC6UL)
#define TL9                      MCS251_XFR(0x7EFBC7UL)
#define TM17PS                   MCS251_XFR(0x7EFBC8UL)
#define TM18PS                   MCS251_XFR(0x7EFBC9UL)
#define T18T17CFG                MCS251_XFR(0x7EFBCAUL)
#define T18T17M                  MCS251_XFR(0x7EFBCBUL)
#define TH18                     MCS251_XFR(0x7EFBCCUL)
#define TL18                     MCS251_XFR(0x7EFBCDUL)
#define TH17                     MCS251_XFR(0x7EFBCEUL)
#define TL17                     MCS251_XFR(0x7EFBCFUL)
#define CHIPIDX0                 MCS251_XFR(0x7EFBD0UL)
#define CHIPIDX1                 MCS251_XFR(0x7EFBD1UL)
#define CHIPIDX2                 MCS251_XFR(0x7EFBD2UL)
#define CHIPIDX3                 MCS251_XFR(0x7EFBD3UL)
#define CHIPIDX4                 MCS251_XFR(0x7EFBD4UL)
#define CHIPIDX5                 MCS251_XFR(0x7EFBD5UL)
#define CHIPIDX6                 MCS251_XFR(0x7EFBD6UL)
#define CHIPIDX7                 MCS251_XFR(0x7EFBD7UL)
#define CHIPIDX8                 MCS251_XFR(0x7EFBD8UL)
#define CHIPIDX9                 MCS251_XFR(0x7EFBD9UL)
#define CHIPIDX10                MCS251_XFR(0x7EFBDAUL)
#define CHIPIDX11                MCS251_XFR(0x7EFBDBUL)
#define CHIPIDX12                MCS251_XFR(0x7EFBDCUL)
#define CHIPIDX13                MCS251_XFR(0x7EFBDDUL)
#define CHIPIDX14                MCS251_XFR(0x7EFBDEUL)
#define CHIPIDX15                MCS251_XFR(0x7EFBDFUL)
#define CHIPIDX16                MCS251_XFR(0x7EFBE0UL)
#define CHIPIDX17                MCS251_XFR(0x7EFBE1UL)
#define CHIPIDX18                MCS251_XFR(0x7EFBE2UL)
#define CHIPIDX19                MCS251_XFR(0x7EFBE3UL)
#define CHIPIDX20                MCS251_XFR(0x7EFBE4UL)
#define CHIPIDX21                MCS251_XFR(0x7EFBE5UL)
#define CHIPIDX22                MCS251_XFR(0x7EFBE6UL)
#define CHIPIDX23                MCS251_XFR(0x7EFBE7UL)
#define CHIPIDX24                MCS251_XFR(0x7EFBE8UL)
#define CHIPIDX25                MCS251_XFR(0x7EFBE9UL)
#define CHIPIDX26                MCS251_XFR(0x7EFBEAUL)
#define CHIPIDX27                MCS251_XFR(0x7EFBEBUL)
#define CHIPIDX28                MCS251_XFR(0x7EFBECUL)
#define CHIPIDX29                MCS251_XFR(0x7EFBEDUL)
#define CHIPIDX30                MCS251_XFR(0x7EFBEEUL)
#define CHIPIDX31                MCS251_XFR(0x7EFBEFUL)
#define HSPWMA_CFG               MCS251_XFR(0x7EFBF0UL)
#define HSPWMA_ADR               MCS251_XFR(0x7EFBF1UL)
#define HSPWMA_DAT               MCS251_XFR(0x7EFBF2UL)
#define HSPWMA_ADRH              MCS251_XFR(0x7EFBF3UL)
#define HSPWMB_CFG               MCS251_XFR(0x7EFBF4UL)
#define HSPWMB_ADR               MCS251_XFR(0x7EFBF5UL)
#define HSPWMB_DAT               MCS251_XFR(0x7EFBF6UL)
#define HSPWMB_ADRH              MCS251_XFR(0x7EFBF7UL)
#define HSSPI_CFG                MCS251_XFR(0x7EFBF8UL)
#define HSSPI_CFG2               MCS251_XFR(0x7EFBF9UL)
#define HSSPI_STA                MCS251_XFR(0x7EFBFAUL)
#define HSSPI_PSCR               MCS251_XFR(0x7EFBFBUL)
#define DMA_M2M_CFG              MCS251_XFR(0x7EFA00UL)
#define DMA_M2M_CR               MCS251_XFR(0x7EFA01UL)
#define DMA_M2M_STA              MCS251_XFR(0x7EFA02UL)
#define DMA_M2M_AMT              MCS251_XFR(0x7EFA03UL)
#define DMA_M2M_DONE             MCS251_XFR(0x7EFA04UL)
#define DMA_M2M_TXAH             MCS251_XFR(0x7EFA05UL)
#define DMA_M2M_TXAL             MCS251_XFR(0x7EFA06UL)
#define DMA_M2M_RXAH             MCS251_XFR(0x7EFA07UL)
#define DMA_M2M_RXAL             MCS251_XFR(0x7EFA08UL)
#define DMA_ADC_CFG              MCS251_XFR(0x7EFA10UL)
#define DMA_ADC_CR               MCS251_XFR(0x7EFA11UL)
#define DMA_ADC_STA              MCS251_XFR(0x7EFA12UL)
#define DMA_ADC_AMT              MCS251_XFR(0x7EFA13UL)
#define DMA_ADC_DONE             MCS251_XFR(0x7EFA14UL)
#define DMA_ADC_RXAH             MCS251_XFR(0x7EFA17UL)
#define DMA_ADC_RXAL             MCS251_XFR(0x7EFA18UL)
#define DMA_ADC_CFG2             MCS251_XFR(0x7EFA19UL)
#define DMA_ADC_CHSW0            MCS251_XFR(0x7EFA1AUL)
#define DMA_ADC_CHSW1            MCS251_XFR(0x7EFA1BUL)
#define DMA_ADC_ITVH             MCS251_XFR(0x7EFA1EUL)
#define DMA_ADC_ITVL             MCS251_XFR(0x7EFA1FUL)
#define DMA_SPI_CFG              MCS251_XFR(0x7EFA20UL)
#define DMA_SPI_CR               MCS251_XFR(0x7EFA21UL)
#define DMA_SPI_STA              MCS251_XFR(0x7EFA22UL)
#define DMA_SPI_AMT              MCS251_XFR(0x7EFA23UL)
#define DMA_SPI_DONE             MCS251_XFR(0x7EFA24UL)
#define DMA_SPI_TXAH             MCS251_XFR(0x7EFA25UL)
#define DMA_SPI_TXAL             MCS251_XFR(0x7EFA26UL)
#define DMA_SPI_RXAH             MCS251_XFR(0x7EFA27UL)
#define DMA_SPI_RXAL             MCS251_XFR(0x7EFA28UL)
#define DMA_SPI_CFG2             MCS251_XFR(0x7EFA29UL)
#define DMA_SPI_ITVH             MCS251_XFR(0x7EFA2EUL)
#define DMA_SPI_ITVL             MCS251_XFR(0x7EFA2FUL)
#define DMA_UR1T_CFG             MCS251_XFR(0x7EFA30UL)
#define DMA_UR1T_CR              MCS251_XFR(0x7EFA31UL)
#define DMA_UR1T_STA             MCS251_XFR(0x7EFA32UL)
#define DMA_UR1T_AMT             MCS251_XFR(0x7EFA33UL)
#define DMA_UR1T_DONE            MCS251_XFR(0x7EFA34UL)
#define DMA_UR1T_TXAH            MCS251_XFR(0x7EFA35UL)
#define DMA_UR1T_TXAL            MCS251_XFR(0x7EFA36UL)
#define DMA_UR1R_CFG             MCS251_XFR(0x7EFA38UL)
#define DMA_UR1R_CR              MCS251_XFR(0x7EFA39UL)
#define DMA_UR1R_STA             MCS251_XFR(0x7EFA3AUL)
#define DMA_UR1R_AMT             MCS251_XFR(0x7EFA3BUL)
#define DMA_UR1R_DONE            MCS251_XFR(0x7EFA3CUL)
#define DMA_UR1R_RXAH            MCS251_XFR(0x7EFA3DUL)
#define DMA_UR1R_RXAL            MCS251_XFR(0x7EFA3EUL)
#define DMA_UR2T_CFG             MCS251_XFR(0x7EFA40UL)
#define DMA_UR2T_CR              MCS251_XFR(0x7EFA41UL)
#define DMA_UR2T_STA             MCS251_XFR(0x7EFA42UL)
#define DMA_UR2T_AMT             MCS251_XFR(0x7EFA43UL)
#define DMA_UR2T_DONE            MCS251_XFR(0x7EFA44UL)
#define DMA_UR2T_TXAH            MCS251_XFR(0x7EFA45UL)
#define DMA_UR2T_TXAL            MCS251_XFR(0x7EFA46UL)
#define DMA_UR2R_CFG             MCS251_XFR(0x7EFA48UL)
#define DMA_UR2R_CR              MCS251_XFR(0x7EFA49UL)
#define DMA_UR2R_STA             MCS251_XFR(0x7EFA4AUL)
#define DMA_UR2R_AMT             MCS251_XFR(0x7EFA4BUL)
#define DMA_UR2R_DONE            MCS251_XFR(0x7EFA4CUL)
#define DMA_UR2R_RXAH            MCS251_XFR(0x7EFA4DUL)
#define DMA_UR2R_RXAL            MCS251_XFR(0x7EFA4EUL)
#define DMA_UR3T_CFG             MCS251_XFR(0x7EFA50UL)
#define DMA_UR3T_CR              MCS251_XFR(0x7EFA51UL)
#define DMA_UR3T_STA             MCS251_XFR(0x7EFA52UL)
#define DMA_UR3T_AMT             MCS251_XFR(0x7EFA53UL)
#define DMA_UR3T_DONE            MCS251_XFR(0x7EFA54UL)
#define DMA_UR3T_TXAH            MCS251_XFR(0x7EFA55UL)
#define DMA_UR3T_TXAL            MCS251_XFR(0x7EFA56UL)
#define DMA_UR3R_CFG             MCS251_XFR(0x7EFA58UL)
#define DMA_UR3R_CR              MCS251_XFR(0x7EFA59UL)
#define DMA_UR3R_STA             MCS251_XFR(0x7EFA5AUL)
#define DMA_UR3R_AMT             MCS251_XFR(0x7EFA5BUL)
#define DMA_UR3R_DONE            MCS251_XFR(0x7EFA5CUL)
#define DMA_UR3R_RXAH            MCS251_XFR(0x7EFA5DUL)
#define DMA_UR3R_RXAL            MCS251_XFR(0x7EFA5EUL)
#define DMA_UR4T_CFG             MCS251_XFR(0x7EFA60UL)
#define DMA_UR4T_CR              MCS251_XFR(0x7EFA61UL)
#define DMA_UR4T_STA             MCS251_XFR(0x7EFA62UL)
#define DMA_UR4T_AMT             MCS251_XFR(0x7EFA63UL)
#define DMA_UR4T_DONE            MCS251_XFR(0x7EFA64UL)
#define DMA_UR4T_TXAH            MCS251_XFR(0x7EFA65UL)
#define DMA_UR4T_TXAL            MCS251_XFR(0x7EFA66UL)
#define DMA_UR4R_CFG             MCS251_XFR(0x7EFA68UL)
#define DMA_UR4R_CR              MCS251_XFR(0x7EFA69UL)
#define DMA_UR4R_STA             MCS251_XFR(0x7EFA6AUL)
#define DMA_UR4R_AMT             MCS251_XFR(0x7EFA6BUL)
#define DMA_UR4R_DONE            MCS251_XFR(0x7EFA6CUL)
#define DMA_UR4R_RXAH            MCS251_XFR(0x7EFA6DUL)
#define DMA_UR4R_RXAL            MCS251_XFR(0x7EFA6EUL)
#define DMA_LCM_CFG              MCS251_XFR(0x7EFA70UL)
#define DMA_LCM_CR               MCS251_XFR(0x7EFA71UL)
#define DMA_LCM_STA              MCS251_XFR(0x7EFA72UL)
#define DMA_LCM_AMT              MCS251_XFR(0x7EFA73UL)
#define DMA_LCM_DONE             MCS251_XFR(0x7EFA74UL)
#define DMA_LCM_TXAH             MCS251_XFR(0x7EFA75UL)
#define DMA_LCM_TXAL             MCS251_XFR(0x7EFA76UL)
#define DMA_LCM_RXAH             MCS251_XFR(0x7EFA77UL)
#define DMA_LCM_RXAL             MCS251_XFR(0x7EFA78UL)
#define DMA_LCM_ITVH             MCS251_XFR(0x7EFA7EUL)
#define DMA_LCM_ITVL             MCS251_XFR(0x7EFA7FUL)
#define DMA_M2M_AMTH             MCS251_XFR(0x7EFA80UL)
#define DMA_M2M_DONEH            MCS251_XFR(0x7EFA81UL)
#define DMA_ADC_AMTH             MCS251_XFR(0x7EFA82UL)
#define DMA_ADC_DONEH            MCS251_XFR(0x7EFA83UL)
#define DMA_SPI_AMTH             MCS251_XFR(0x7EFA84UL)
#define DMA_SPI_DONEH            MCS251_XFR(0x7EFA85UL)
#define DMA_LCM_AMTH             MCS251_XFR(0x7EFA86UL)
#define DMA_LCM_DONEH            MCS251_XFR(0x7EFA87UL)
#define DMA_UR1T_AMTH            MCS251_XFR(0x7EFA88UL)
#define DMA_UR1T_DONEH           MCS251_XFR(0x7EFA89UL)
#define DMA_UR1R_AMTH            MCS251_XFR(0x7EFA8AUL)
#define DMA_UR1R_DONEH           MCS251_XFR(0x7EFA8BUL)
#define DMA_UR2T_AMTH            MCS251_XFR(0x7EFA8CUL)
#define DMA_UR2T_DONEH           MCS251_XFR(0x7EFA8DUL)
#define DMA_UR2R_AMTH            MCS251_XFR(0x7EFA8EUL)
#define DMA_UR2R_DONEH           MCS251_XFR(0x7EFA8FUL)
#define DMA_UR3T_AMTH            MCS251_XFR(0x7EFA90UL)
#define DMA_UR3T_DONEH           MCS251_XFR(0x7EFA91UL)
#define DMA_UR3R_AMTH            MCS251_XFR(0x7EFA92UL)
#define DMA_UR3R_DONEH           MCS251_XFR(0x7EFA93UL)
#define DMA_UR4T_AMTH            MCS251_XFR(0x7EFA94UL)
#define DMA_UR4T_DONEH           MCS251_XFR(0x7EFA95UL)
#define DMA_UR4R_AMTH            MCS251_XFR(0x7EFA96UL)
#define DMA_UR4R_DONEH           MCS251_XFR(0x7EFA97UL)
#define DMA_I2CT_CFG             MCS251_XFR(0x7EFA98UL)
#define DMA_I2CT_CR              MCS251_XFR(0x7EFA99UL)
#define DMA_I2CT_STA             MCS251_XFR(0x7EFA9AUL)
#define DMA_I2CT_AMT             MCS251_XFR(0x7EFA9BUL)
#define DMA_I2CT_DONE            MCS251_XFR(0x7EFA9CUL)
#define DMA_I2CT_TXAH            MCS251_XFR(0x7EFA9DUL)
#define DMA_I2CT_TXAL            MCS251_XFR(0x7EFA9EUL)
#define DMA_I2CR_CFG             MCS251_XFR(0x7EFAA0UL)
#define DMA_I2CR_CR              MCS251_XFR(0x7EFAA1UL)
#define DMA_I2CR_STA             MCS251_XFR(0x7EFAA2UL)
#define DMA_I2CR_AMT             MCS251_XFR(0x7EFAA3UL)
#define DMA_I2CR_DONE            MCS251_XFR(0x7EFAA4UL)
#define DMA_I2CR_RXAH            MCS251_XFR(0x7EFAA5UL)
#define DMA_I2CR_RXAL            MCS251_XFR(0x7EFAA6UL)
#define DMA_I2CT_AMTH            MCS251_XFR(0x7EFAA8UL)
#define DMA_I2CT_DONEH           MCS251_XFR(0x7EFAA9UL)
#define DMA_I2CR_AMTH            MCS251_XFR(0x7EFAAAUL)
#define DMA_I2CR_DONEH           MCS251_XFR(0x7EFAABUL)
#define DMA_I2C_CR               MCS251_XFR(0x7EFAADUL)
#define DMA_I2C_ST1              MCS251_XFR(0x7EFAAEUL)
#define DMA_I2C_ST2              MCS251_XFR(0x7EFAAFUL)
#define DMA_I2ST_CFG             MCS251_XFR(0x7EFAB0UL)
#define DMA_I2ST_CR              MCS251_XFR(0x7EFAB1UL)
#define DMA_I2ST_STA             MCS251_XFR(0x7EFAB2UL)
#define DMA_I2ST_AMT             MCS251_XFR(0x7EFAB3UL)
#define DMA_I2ST_DONE            MCS251_XFR(0x7EFAB4UL)
#define DMA_I2ST_TXAH            MCS251_XFR(0x7EFAB5UL)
#define DMA_I2ST_TXAL            MCS251_XFR(0x7EFAB6UL)
#define DMA_I2SR_CFG             MCS251_XFR(0x7EFAB8UL)
#define DMA_I2SR_CR              MCS251_XFR(0x7EFAB9UL)
#define DMA_I2SR_STA             MCS251_XFR(0x7EFABAUL)
#define DMA_I2SR_AMT             MCS251_XFR(0x7EFABBUL)
#define DMA_I2SR_DONE            MCS251_XFR(0x7EFABCUL)
#define DMA_I2SR_RXAH            MCS251_XFR(0x7EFABDUL)
#define DMA_I2SR_RXAL            MCS251_XFR(0x7EFABEUL)
#define DMA_I2ST_AMTH            MCS251_XFR(0x7EFAC0UL)
#define DMA_I2ST_DONEH           MCS251_XFR(0x7EFAC1UL)
#define DMA_I2SR_AMTH            MCS251_XFR(0x7EFAC2UL)
#define DMA_I2SR_DONEH           MCS251_XFR(0x7EFAC3UL)
#define DMA_I2C_ITVH             MCS251_XFR(0x7EFAC4UL)
#define DMA_I2C_ITVL             MCS251_XFR(0x7EFAC5UL)
#define DMA_I2S_ITVH             MCS251_XFR(0x7EFAC6UL)
#define DMA_I2S_ITVL             MCS251_XFR(0x7EFAC7UL)
#define DMA_UR1_ITVH             MCS251_XFR(0x7EFAC8UL)
#define DMA_UR1_ITVL             MCS251_XFR(0x7EFAC9UL)
#define DMA_UR2_ITVH             MCS251_XFR(0x7EFACAUL)
#define DMA_UR2_ITVL             MCS251_XFR(0x7EFACBUL)
#define DMA_UR3_ITVH             MCS251_XFR(0x7EFACCUL)
#define DMA_UR3_ITVL             MCS251_XFR(0x7EFACDUL)
#define DMA_UR4_ITVH             MCS251_XFR(0x7EFACEUL)
#define DMA_UR4_ITVL             MCS251_XFR(0x7EFACFUL)
#define DMA_QSPI_CFG             MCS251_XFR(0x7EFAD0UL)
#define DMA_QSPI_CR              MCS251_XFR(0x7EFAD1UL)
#define DMA_QSPI_STA             MCS251_XFR(0x7EFAD2UL)
#define DMA_QSPI_AMT             MCS251_XFR(0x7EFAD3UL)
#define DMA_QSPI_DONE            MCS251_XFR(0x7EFAD4UL)
#define DMA_QSPI_TXAH            MCS251_XFR(0x7EFAD5UL)
#define DMA_QSPI_TXAL            MCS251_XFR(0x7EFAD6UL)
#define DMA_QSPI_RXAH            MCS251_XFR(0x7EFAD7UL)
#define DMA_QSPI_RXAL            MCS251_XFR(0x7EFAD8UL)
#define DMA_QSPI_AMTH            MCS251_XFR(0x7EFADBUL)
#define DMA_QSPI_DONEH           MCS251_XFR(0x7EFADCUL)
#define DMA_QSPI_ITVH            MCS251_XFR(0x7EFADEUL)
#define DMA_QSPI_ITVL            MCS251_XFR(0x7EFADFUL)
#define DMA_ADC2_CFG             MCS251_XFR(0x7EFAE0UL)
#define DMA_ADC2_CR              MCS251_XFR(0x7EFAE1UL)
#define DMA_ADC2_STA             MCS251_XFR(0x7EFAE2UL)
#define DMA_ADC2_CFG2            MCS251_XFR(0x7EFAE3UL)
#define DMA_ADC2_AMTH            MCS251_XFR(0x7EFAE4UL)
#define DMA_ADC2_AMT             MCS251_XFR(0x7EFAE5UL)
#define DMA_ADC2_DONEH           MCS251_XFR(0x7EFAE6UL)
#define DMA_ADC2_DONE            MCS251_XFR(0x7EFAE7UL)
#define DMA_ADC2_RXAH            MCS251_XFR(0x7EFAE8UL)
#define DMA_ADC2_RXAL            MCS251_XFR(0x7EFAE9UL)
#define DMA_ADC2_CHSW0           MCS251_XFR(0x7EFAEAUL)
#define DMA_ADC2_CHSW1           MCS251_XFR(0x7EFAEBUL)
#define DMA_ADC2_ITVH            MCS251_XFR(0x7EFAEEUL)
#define DMA_ADC2_ITVL            MCS251_XFR(0x7EFAEFUL)
#define DMA_P2P1_DEST            MCS251_XFR(0x7EFAF0UL)
#define DMA_P2P1_SRC             MCS251_XFR(0x7EFAF1UL)
#define DMA_P2P2_DEST            MCS251_XFR(0x7EFAF2UL)
#define DMA_P2P2_SRC             MCS251_XFR(0x7EFAF3UL)
#define DMA_ARB_CFG              MCS251_XFR(0x7EFAF8UL)
#define DMA_ARB_STA              MCS251_XFR(0x7EFAF9UL)
#define QSPI_CR1                 MCS251_XFR(0x7EF900UL)
#define QSPI_CR2                 MCS251_XFR(0x7EF901UL)
#define QSPI_CR3                 MCS251_XFR(0x7EF902UL)
#define QSPI_CR4                 MCS251_XFR(0x7EF903UL)
#define QSPI_DCR1                MCS251_XFR(0x7EF904UL)
#define QSPI_DCR2                MCS251_XFR(0x7EF905UL)
#define QSPI_SR1                 MCS251_XFR(0x7EF906UL)
#define QSPI_SR2                 MCS251_XFR(0x7EF907UL)
#define QSPI_FCR                 MCS251_XFR(0x7EF908UL)
#define QSPI_HCR1                MCS251_XFR(0x7EF909UL)
#define QSPI_HCR2                MCS251_XFR(0x7EF90AUL)
#define QSPI_DLR1                MCS251_XFR(0x7EF910UL)
#define QSPI_DLR2                MCS251_XFR(0x7EF911UL)
#define QSPI_CCR1                MCS251_XFR(0x7EF914UL)
#define QSPI_CCR2                MCS251_XFR(0x7EF915UL)
#define QSPI_CCR3                MCS251_XFR(0x7EF916UL)
#define QSPI_CCR4                MCS251_XFR(0x7EF917UL)
#define QSPI_AR1                 MCS251_XFR(0x7EF918UL)
#define QSPI_AR2                 MCS251_XFR(0x7EF919UL)
#define QSPI_AR3                 MCS251_XFR(0x7EF91AUL)
#define QSPI_AR4                 MCS251_XFR(0x7EF91BUL)
#define QSPI_ABR                 MCS251_XFR(0x7EF91CUL)
#define QSPI_ABR1                MCS251_XFR(0x7EF91CUL)
#define QSPI_ABR2                MCS251_XFR(0x7EF91DUL)
#define QSPI_ABR3                MCS251_XFR(0x7EF91EUL)
#define QSPI_ABR4                MCS251_XFR(0x7EF91FUL)
#define QSPI_DR                  MCS251_XFR(0x7EF920UL)
#define QSPI_PSMKR1              MCS251_XFR(0x7EF924UL)
#define QSPI_PSMAR1              MCS251_XFR(0x7EF928UL)
#define QSPI_PIR1                MCS251_XFR(0x7EF92CUL)
#define QSPI_PIR2                MCS251_XFR(0x7EF92DUL)
#define PWMA_ENO2                MCS251_XFR(0x7EF930UL)
#define PWMA_IOAUX2              MCS251_XFR(0x7EF931UL)
#define PWMA_CR3                 MCS251_XFR(0x7EF932UL)
#define PWMA_SR3                 MCS251_XFR(0x7EF933UL)
#define PWMA_CCER3               MCS251_XFR(0x7EF934UL)
#define PWMA_CCMR1X              MCS251_XFR(0x7EF938UL)
#define PWMA_CCMR2X              MCS251_XFR(0x7EF939UL)
#define PWMA_CCMR3X              MCS251_XFR(0x7EF93AUL)
#define PWMA_CCMR4X              MCS251_XFR(0x7EF93BUL)
#define PWMA_CCMR5               MCS251_XFR(0x7EF93CUL)
#define PWMA_CCMR5X              MCS251_XFR(0x7EF93DUL)
#define PWMA_CCMR6               MCS251_XFR(0x7EF93EUL)
#define PWMA_CCMR6X              MCS251_XFR(0x7EF93FUL)
#define PWMA_CCR5H               MCS251_XFR(0x7EF940UL)
#define PWMA_CCR5L               MCS251_XFR(0x7EF941UL)
#define PWMA_CCR5X               MCS251_XFR(0x7EF942UL)
#define PWMA_CCR6H               MCS251_XFR(0x7EF943UL)
#define PWMA_CCR6L               MCS251_XFR(0x7EF944UL)
#define PWMA_DER                 MCS251_XFR(0x7EF948UL)
#define PWMA_DBA                 MCS251_XFR(0x7EF949UL)
#define PWMA_DBL                 MCS251_XFR(0x7EF94AUL)
#define PWMA_DMACR               MCS251_XFR(0x7EF94BUL)
#define PWMC_ENO2                MCS251_XFR(0x7EF950UL)
#define PWMC_IOAUX2              MCS251_XFR(0x7EF951UL)
#define PWMC_CR3                 MCS251_XFR(0x7EF952UL)
#define PWMC_SR3                 MCS251_XFR(0x7EF953UL)
#define PWMC_CCER3               MCS251_XFR(0x7EF954UL)
#define PWMC_CCMR1X              MCS251_XFR(0x7EF958UL)
#define PWMC_CCMR2X              MCS251_XFR(0x7EF959UL)
#define PWMC_CCMR3X              MCS251_XFR(0x7EF95AUL)
#define PWMC_CCMR4X              MCS251_XFR(0x7EF95BUL)
#define PWMC_CCMR5               MCS251_XFR(0x7EF95CUL)
#define PWMC_CCMR5X              MCS251_XFR(0x7EF95DUL)
#define PWMC_CCMR6               MCS251_XFR(0x7EF95EUL)
#define PWMC_CCMR6X              MCS251_XFR(0x7EF95FUL)
#define PWMC_CCR5H               MCS251_XFR(0x7EF960UL)
#define PWMC_CCR5L               MCS251_XFR(0x7EF961UL)
#define PWMC_CCR5X               MCS251_XFR(0x7EF962UL)
#define PWMC_CCR6H               MCS251_XFR(0x7EF963UL)
#define PWMC_CCR6L               MCS251_XFR(0x7EF964UL)
#define PWMC_DER                 MCS251_XFR(0x7EF968UL)
#define PWMC_DBA                 MCS251_XFR(0x7EF969UL)
#define PWMC_DBL                 MCS251_XFR(0x7EF96AUL)
#define PWMC_DMACR               MCS251_XFR(0x7EF96BUL)
#define HSPWMC_CFG               MCS251_XFR(0x7EF970UL)
#define HSPWMC_ADR               MCS251_XFR(0x7EF971UL)
#define HSPWMC_DAT               MCS251_XFR(0x7EF972UL)
#define HSPWMC_ADRH              MCS251_XFR(0x7EF973UL)
#define HSPWMD_CFG               MCS251_XFR(0x7EF974UL)
#define HSPWMD_ADR               MCS251_XFR(0x7EF975UL)
#define HSPWMD_DAT               MCS251_XFR(0x7EF976UL)
#define HSPWMD_ADRH              MCS251_XFR(0x7EF977UL)
#define DMA_PWMAT_CFG            MCS251_XFR(0x7EF980UL)
#define DMA_PWMAT_CR             MCS251_XFR(0x7EF981UL)
#define DMA_PWMAT_STA            MCS251_XFR(0x7EF982UL)
#define DMA_PWMAT_AMTH           MCS251_XFR(0x7EF984UL)
#define DMA_PWMAT_AMT            MCS251_XFR(0x7EF985UL)
#define DMA_PWMAT_DONEH          MCS251_XFR(0x7EF986UL)
#define DMA_PWMAT_DONE           MCS251_XFR(0x7EF987UL)
#define DMA_PWMAT_TXAH           MCS251_XFR(0x7EF988UL)
#define DMA_PWMAT_TXAL           MCS251_XFR(0x7EF989UL)
#define DMA_PWMA_ITVH            MCS251_XFR(0x7EF98EUL)
#define DMA_PWMA_ITVL            MCS251_XFR(0x7EF98FUL)
#define DMA_PWMAR_CFG            MCS251_XFR(0x7EF990UL)
#define DMA_PWMAR_CR             MCS251_XFR(0x7EF991UL)
#define DMA_PWMAR_STA            MCS251_XFR(0x7EF992UL)
#define DMA_PWMAR_AMTH           MCS251_XFR(0x7EF994UL)
#define DMA_PWMAR_AMT            MCS251_XFR(0x7EF995UL)
#define DMA_PWMAR_DONEH          MCS251_XFR(0x7EF996UL)
#define DMA_PWMAR_DONE           MCS251_XFR(0x7EF997UL)
#define DMA_PWMAR_RXAH           MCS251_XFR(0x7EF998UL)
#define DMA_PWMAR_RXAL           MCS251_XFR(0x7EF999UL)
#define DMA_PWMCT_CFG            MCS251_XFR(0x7EF9A0UL)
#define DMA_PWMCT_CR             MCS251_XFR(0x7EF9A1UL)
#define DMA_PWMCT_STA            MCS251_XFR(0x7EF9A2UL)
#define DMA_PWMCT_AMTH           MCS251_XFR(0x7EF9A4UL)
#define DMA_PWMCT_AMT            MCS251_XFR(0x7EF9A5UL)
#define DMA_PWMCT_DONEH          MCS251_XFR(0x7EF9A6UL)
#define DMA_PWMCT_DONE           MCS251_XFR(0x7EF9A7UL)
#define DMA_PWMCT_TXAH           MCS251_XFR(0x7EF9A8UL)
#define DMA_PWMCT_TXAL           MCS251_XFR(0x7EF9A9UL)
#define DMA_PWMC_ITVH            MCS251_XFR(0x7EF9AEUL)
#define DMA_PWMC_ITVL            MCS251_XFR(0x7EF9AFUL)
#define DMA_PWMCR_CFG            MCS251_XFR(0x7EF9B0UL)
#define DMA_PWMCR_CR             MCS251_XFR(0x7EF9B1UL)
#define DMA_PWMCR_STA            MCS251_XFR(0x7EF9B2UL)
#define DMA_PWMCR_AMTH           MCS251_XFR(0x7EF9B4UL)
#define DMA_PWMCR_AMT            MCS251_XFR(0x7EF9B5UL)
#define DMA_PWMCR_DONEH          MCS251_XFR(0x7EF9B6UL)
#define DMA_PWMCR_DONE           MCS251_XFR(0x7EF9B7UL)
#define DMA_PWMCR_RXAH           MCS251_XFR(0x7EF9B8UL)
#define DMA_PWMCR_RXAL           MCS251_XFR(0x7EF9B9UL)
#define P8PU                     MCS251_XFR(0x7EF9C0UL)
#define P9PU                     MCS251_XFR(0x7EF9C1UL)
#define PAPU                     MCS251_XFR(0x7EF9C2UL)
#define PBPU                     MCS251_XFR(0x7EF9C3UL)
#define PCPU                     MCS251_XFR(0x7EF9C4UL)
#define PDPU                     MCS251_XFR(0x7EF9C5UL)
#define PEPU                     MCS251_XFR(0x7EF9C6UL)
#define PFPU                     MCS251_XFR(0x7EF9C7UL)
#define P8NCS                    MCS251_XFR(0x7EF9C8UL)
#define P9NCS                    MCS251_XFR(0x7EF9C9UL)
#define PANCS                    MCS251_XFR(0x7EF9CAUL)
#define PBNCS                    MCS251_XFR(0x7EF9CBUL)
#define PCNCS                    MCS251_XFR(0x7EF9CCUL)
#define PDNCS                    MCS251_XFR(0x7EF9CDUL)
#define PENCS                    MCS251_XFR(0x7EF9CEUL)
#define PFNCS                    MCS251_XFR(0x7EF9CFUL)
#define P8SR                     MCS251_XFR(0x7EF9D0UL)
#define P9SR                     MCS251_XFR(0x7EF9D1UL)
#define PASR                     MCS251_XFR(0x7EF9D2UL)
#define PBSR                     MCS251_XFR(0x7EF9D3UL)
#define PCSR                     MCS251_XFR(0x7EF9D4UL)
#define PDSR                     MCS251_XFR(0x7EF9D5UL)
#define PESR                     MCS251_XFR(0x7EF9D6UL)
#define PFSR                     MCS251_XFR(0x7EF9D7UL)
#define P8DR                     MCS251_XFR(0x7EF9D8UL)
#define P9DR                     MCS251_XFR(0x7EF9D9UL)
#define PADR                     MCS251_XFR(0x7EF9DAUL)
#define PBDR                     MCS251_XFR(0x7EF9DBUL)
#define PCDR                     MCS251_XFR(0x7EF9DCUL)
#define PDDR                     MCS251_XFR(0x7EF9DDUL)
#define PEDR                     MCS251_XFR(0x7EF9DEUL)
#define PFDR                     MCS251_XFR(0x7EF9DFUL)
#define P8IE                     MCS251_XFR(0x7EF9E0UL)
#define P9IE                     MCS251_XFR(0x7EF9E1UL)
#define PAIE                     MCS251_XFR(0x7EF9E2UL)
#define PBIE                     MCS251_XFR(0x7EF9E3UL)
#define PCIE                     MCS251_XFR(0x7EF9E4UL)
#define PDIE                     MCS251_XFR(0x7EF9E5UL)
#define PEIE                     MCS251_XFR(0x7EF9E6UL)
#define PFIE                     MCS251_XFR(0x7EF9E7UL)
#define P8PD                     MCS251_XFR(0x7EF9F0UL)
#define P9PD                     MCS251_XFR(0x7EF9F1UL)
#define PAPD                     MCS251_XFR(0x7EF9F2UL)
#define PBPD                     MCS251_XFR(0x7EF9F3UL)
#define PCPD                     MCS251_XFR(0x7EF9F4UL)
#define PDPD                     MCS251_XFR(0x7EF9F5UL)
#define PEPD                     MCS251_XFR(0x7EF9F6UL)
#define PFPD                     MCS251_XFR(0x7EF9F7UL)
#define P8BP                     MCS251_XFR(0x7EF9F8UL)
#define P9BP                     MCS251_XFR(0x7EF9F9UL)
#define PABP                     MCS251_XFR(0x7EF9FAUL)
#define PBBP                     MCS251_XFR(0x7EF9FBUL)
#define PCBP                     MCS251_XFR(0x7EF9FCUL)
#define PDBP                     MCS251_XFR(0x7EF9FDUL)
#define PEBP                     MCS251_XFR(0x7EF9FEUL)
#define PFBP                     MCS251_XFR(0x7EF9FFUL)
#define SPI2CTL                  MCS251_XFR(0x7EF800UL)
#define SPI2STAT                 MCS251_XFR(0x7EF801UL)
#define SPI2DAT                  MCS251_XFR(0x7EF802UL)
#define SPI2TOTE                 MCS251_XFR(0x7EF803UL)
#define SPI2TOCR                 MCS251_XFR(0x7EF804UL)
#define SPI2TOSR                 MCS251_XFR(0x7EF805UL)
#define SPI2TOTH                 MCS251_XFR(0x7EF806UL)
#define SPI2TOTL                 MCS251_XFR(0x7EF807UL)
#define SPI2_CLKDIV              MCS251_XFR(0x7EF808UL)
#define HSSPI2_CFG               MCS251_XFR(0x7EF809UL)
#define HSSPI2_CFG2              MCS251_XFR(0x7EF80AUL)
#define HSSPI2_STA               MCS251_XFR(0x7EF80BUL)
#define HSSPI2_PSCR              MCS251_XFR(0x7EF80CUL)
#define DMA_SPI2_CFG             MCS251_XFR(0x7EF810UL)
#define DMA_SPI2_CR              MCS251_XFR(0x7EF811UL)
#define DMA_SPI2_STA             MCS251_XFR(0x7EF812UL)
#define DMA_SPI2_CFG2            MCS251_XFR(0x7EF813UL)
#define DMA_SPI2_AMTH            MCS251_XFR(0x7EF814UL)
#define DMA_SPI2_AMT             MCS251_XFR(0x7EF815UL)
#define DMA_SPI2_DONEH           MCS251_XFR(0x7EF816UL)
#define DMA_SPI2_DONE            MCS251_XFR(0x7EF817UL)
#define DMA_SPI2_TXAH            MCS251_XFR(0x7EF818UL)
#define DMA_SPI2_TXAL            MCS251_XFR(0x7EF819UL)
#define DMA_SPI2_RXAH            MCS251_XFR(0x7EF81AUL)
#define DMA_SPI2_RXAL            MCS251_XFR(0x7EF81BUL)
#define DMA_SPI2_ITVH            MCS251_XFR(0x7EF81EUL)
#define DMA_SPI2_ITVL            MCS251_XFR(0x7EF81FUL)
#define SPI3CTL                  MCS251_XFR(0x7EF820UL)
#define SPI3STAT                 MCS251_XFR(0x7EF821UL)
#define SPI3DAT                  MCS251_XFR(0x7EF822UL)
#define SPI3TOTE                 MCS251_XFR(0x7EF823UL)
#define SPI3TOCR                 MCS251_XFR(0x7EF824UL)
#define SPI3TOSR                 MCS251_XFR(0x7EF825UL)
#define SPI3TOTH                 MCS251_XFR(0x7EF826UL)
#define SPI3TOTL                 MCS251_XFR(0x7EF827UL)
#define SPI3_CLKDIV              MCS251_XFR(0x7EF828UL)
#define HSSPI3_CFG               MCS251_XFR(0x7EF829UL)
#define HSSPI3_CFG2              MCS251_XFR(0x7EF82AUL)
#define HSSPI3_STA               MCS251_XFR(0x7EF82BUL)
#define HSSPI3_PSCR              MCS251_XFR(0x7EF82CUL)
#define DMA_SPI3_CFG             MCS251_XFR(0x7EF830UL)
#define DMA_SPI3_CR              MCS251_XFR(0x7EF831UL)
#define DMA_SPI3_STA             MCS251_XFR(0x7EF832UL)
#define DMA_SPI3_CFG2            MCS251_XFR(0x7EF833UL)
#define DMA_SPI3_AMTH            MCS251_XFR(0x7EF834UL)
#define DMA_SPI3_AMT             MCS251_XFR(0x7EF835UL)
#define DMA_SPI3_DONEH           MCS251_XFR(0x7EF836UL)
#define DMA_SPI3_DONE            MCS251_XFR(0x7EF837UL)
#define DMA_SPI3_TXAH            MCS251_XFR(0x7EF838UL)
#define DMA_SPI3_TXAL            MCS251_XFR(0x7EF839UL)
#define DMA_SPI3_RXAH            MCS251_XFR(0x7EF83AUL)
#define DMA_SPI3_RXAL            MCS251_XFR(0x7EF83BUL)
#define DMA_SPI3_ITVH            MCS251_XFR(0x7EF83EUL)
#define DMA_SPI3_ITVL            MCS251_XFR(0x7EF83FUL)
#define I2C2CFG                  MCS251_XFR(0x7EF860UL)
#define I2C2MSCR                 MCS251_XFR(0x7EF861UL)
#define I2C2MSST                 MCS251_XFR(0x7EF862UL)
#define I2C2SLCR                 MCS251_XFR(0x7EF863UL)
#define I2C2SLST                 MCS251_XFR(0x7EF864UL)
#define I2C2SLADR                MCS251_XFR(0x7EF865UL)
#define I2C2TXD                  MCS251_XFR(0x7EF866UL)
#define I2C2RXD                  MCS251_XFR(0x7EF867UL)
#define I2C2MSAUX                MCS251_XFR(0x7EF868UL)
#define I2C2PSCR                 MCS251_XFR(0x7EF869UL)
#define I2C2TOTE                 MCS251_XFR(0x7EF86BUL)
#define I2C2TOCR                 MCS251_XFR(0x7EF86CUL)
#define I2C2TOSR                 MCS251_XFR(0x7EF86DUL)
#define I2C2TOTH                 MCS251_XFR(0x7EF86EUL)
#define I2C2TOTL                 MCS251_XFR(0x7EF86FUL)
#define DMA_I2C2T_CFG            MCS251_XFR(0x7EF870UL)
#define DMA_I2C2T_CR             MCS251_XFR(0x7EF871UL)
#define DMA_I2C2T_STA            MCS251_XFR(0x7EF872UL)
#define DMA_I2C2T_AMTH           MCS251_XFR(0x7EF874UL)
#define DMA_I2C2T_AMT            MCS251_XFR(0x7EF875UL)
#define DMA_I2C2T_DONEH          MCS251_XFR(0x7EF876UL)
#define DMA_I2C2T_DONE           MCS251_XFR(0x7EF877UL)
#define DMA_I2C2T_TXAH           MCS251_XFR(0x7EF878UL)
#define DMA_I2C2T_TXAL           MCS251_XFR(0x7EF879UL)
#define DMA_I2C2_CR              MCS251_XFR(0x7EF87BUL)
#define DMA_I2C2_ST1             MCS251_XFR(0x7EF87CUL)
#define DMA_I2C2_ST2             MCS251_XFR(0x7EF87DUL)
#define DMA_I2C2_ITVH            MCS251_XFR(0x7EF87EUL)
#define DMA_I2C2_ITVL            MCS251_XFR(0x7EF87FUL)
#define DMA_I2C2R_CFG            MCS251_XFR(0x7EF880UL)
#define DMA_I2C2R_CR             MCS251_XFR(0x7EF881UL)
#define DMA_I2C2R_STA            MCS251_XFR(0x7EF882UL)
#define DMA_I2C2R_AMTH           MCS251_XFR(0x7EF884UL)
#define DMA_I2C2R_AMT            MCS251_XFR(0x7EF885UL)
#define DMA_I2C2R_DONEH          MCS251_XFR(0x7EF886UL)
#define DMA_I2C2R_DONE           MCS251_XFR(0x7EF887UL)
#define DMA_I2C2R_RXAH           MCS251_XFR(0x7EF888UL)
#define DMA_I2C2R_RXAL           MCS251_XFR(0x7EF889UL)
#define DMA_I2S2T_CFG            MCS251_XFR(0x7EF890UL)
#define DMA_I2S2T_CR             MCS251_XFR(0x7EF891UL)
#define DMA_I2S2T_STA            MCS251_XFR(0x7EF892UL)
#define DMA_I2S2T_AMTH           MCS251_XFR(0x7EF894UL)
#define DMA_I2S2T_AMT            MCS251_XFR(0x7EF895UL)
#define DMA_I2S2T_DONEH          MCS251_XFR(0x7EF896UL)
#define DMA_I2S2T_DONE           MCS251_XFR(0x7EF897UL)
#define DMA_I2S2T_TXAH           MCS251_XFR(0x7EF898UL)
#define DMA_I2S2T_TXAL           MCS251_XFR(0x7EF899UL)
#define DMA_I2S2_ITVH            MCS251_XFR(0x7EF89EUL)
#define DMA_I2S2_ITVL            MCS251_XFR(0x7EF89FUL)
#define DMA_I2S2R_CFG            MCS251_XFR(0x7EF8A0UL)
#define DMA_I2S2R_CR             MCS251_XFR(0x7EF8A1UL)
#define DMA_I2S2R_STA            MCS251_XFR(0x7EF8A2UL)
#define DMA_I2S2R_AMTH           MCS251_XFR(0x7EF8A4UL)
#define DMA_I2S2R_AMT            MCS251_XFR(0x7EF8A5UL)
#define DMA_I2S2R_DONEH          MCS251_XFR(0x7EF8A6UL)
#define DMA_I2S2R_DONE           MCS251_XFR(0x7EF8A7UL)
#define DMA_I2S2R_RXAH           MCS251_XFR(0x7EF8A8UL)
#define DMA_I2S2R_RXAL           MCS251_XFR(0x7EF8A9UL)
#define PWMC_ETRPS               MCS251_XFR(0x7EF8B0UL)
#define PWMC_ENO                 MCS251_XFR(0x7EF8B1UL)
#define PWMC_PS                  MCS251_XFR(0x7EF8B2UL)
#define PWMC_IOAUX               MCS251_XFR(0x7EF8B3UL)
#define PWMD_ETRPS               MCS251_XFR(0x7EF8B4UL)
#define PWMD_ENO                 MCS251_XFR(0x7EF8B5UL)
#define PWMD_PS                  MCS251_XFR(0x7EF8B6UL)
#define PWMD_IOAUX               MCS251_XFR(0x7EF8B7UL)
#define PWMC_PS2                 MCS251_XFR(0x7EF8B8UL)
#define PWMC_RCRH                MCS251_XFR(0x7EF8B9UL)
#define PWMD_RCRH                MCS251_XFR(0x7EF8BAUL)
#define PWMC_CR1                 MCS251_XFR(0x7EF8C0UL)
#define PWMC_CR2                 MCS251_XFR(0x7EF8C1UL)
#define PWMC_SMCR                MCS251_XFR(0x7EF8C2UL)
#define PWMC_ETR                 MCS251_XFR(0x7EF8C3UL)
#define PWMC_IER                 MCS251_XFR(0x7EF8C4UL)
#define PWMC_SR1                 MCS251_XFR(0x7EF8C5UL)
#define PWMC_SR2                 MCS251_XFR(0x7EF8C6UL)
#define PWMC_EGR                 MCS251_XFR(0x7EF8C7UL)
#define PWMC_CCMR1               MCS251_XFR(0x7EF8C8UL)
#define PWMC_CCMR2               MCS251_XFR(0x7EF8C9UL)
#define PWMC_CCMR3               MCS251_XFR(0x7EF8CAUL)
#define PWMC_CCMR4               MCS251_XFR(0x7EF8CBUL)
#define PWMC_CCER1               MCS251_XFR(0x7EF8CCUL)
#define PWMC_CCER2               MCS251_XFR(0x7EF8CDUL)
#define PWMC_CNTRH               MCS251_XFR(0x7EF8CEUL)
#define PWMC_CNTRL               MCS251_XFR(0x7EF8CFUL)
#define PWMC_PSCRH               MCS251_XFR(0x7EF8D0UL)
#define PWMC_PSCRL               MCS251_XFR(0x7EF8D1UL)
#define PWMC_ARRH                MCS251_XFR(0x7EF8D2UL)
#define PWMC_ARRL                MCS251_XFR(0x7EF8D3UL)
#define PWMC_RCR                 MCS251_XFR(0x7EF8D4UL)
#define PWMC_CCR1H               MCS251_XFR(0x7EF8D5UL)
#define PWMC_CCR1L               MCS251_XFR(0x7EF8D6UL)
#define PWMC_CCR2H               MCS251_XFR(0x7EF8D7UL)
#define PWMC_CCR2L               MCS251_XFR(0x7EF8D8UL)
#define PWMC_CCR3H               MCS251_XFR(0x7EF8D9UL)
#define PWMC_CCR3L               MCS251_XFR(0x7EF8DAUL)
#define PWMC_CCR4H               MCS251_XFR(0x7EF8DBUL)
#define PWMC_CCR4L               MCS251_XFR(0x7EF8DCUL)
#define PWMC_BKR                 MCS251_XFR(0x7EF8DDUL)
#define PWMC_DTR                 MCS251_XFR(0x7EF8DEUL)
#define PWMC_OISR                MCS251_XFR(0x7EF8DFUL)
#define PWMD_CR1                 MCS251_XFR(0x7EF8E0UL)
#define PWMD_CR2                 MCS251_XFR(0x7EF8E1UL)
#define PWMD_SMCR                MCS251_XFR(0x7EF8E2UL)
#define PWMD_ETR                 MCS251_XFR(0x7EF8E3UL)
#define PWMD_IER                 MCS251_XFR(0x7EF8E4UL)
#define PWMD_SR1                 MCS251_XFR(0x7EF8E5UL)
#define PWMD_SR2                 MCS251_XFR(0x7EF8E6UL)
#define PWMD_EGR                 MCS251_XFR(0x7EF8E7UL)
#define PWMD_CCMR1               MCS251_XFR(0x7EF8E8UL)
#define PWMD_CCMR2               MCS251_XFR(0x7EF8E9UL)
#define PWMD_CCMR3               MCS251_XFR(0x7EF8EAUL)
#define PWMD_CCMR4               MCS251_XFR(0x7EF8EBUL)
#define PWMD_CCER1               MCS251_XFR(0x7EF8ECUL)
#define PWMD_CCER2               MCS251_XFR(0x7EF8EDUL)
#define PWMD_CNTRH               MCS251_XFR(0x7EF8EEUL)
#define PWMD_CNTRL               MCS251_XFR(0x7EF8EFUL)
#define PWMD_PSCRH               MCS251_XFR(0x7EF8F0UL)
#define PWMD_PSCRL               MCS251_XFR(0x7EF8F1UL)
#define PWMD_ARRH                MCS251_XFR(0x7EF8F2UL)
#define PWMD_ARRL                MCS251_XFR(0x7EF8F3UL)
#define PWMD_RCR                 MCS251_XFR(0x7EF8F4UL)
#define PWMD_CCR5H               MCS251_XFR(0x7EF8F5UL)
#define PWMD_CCR5L               MCS251_XFR(0x7EF8F6UL)
#define PWMD_CCR6H               MCS251_XFR(0x7EF8F7UL)
#define PWMD_CCR6L               MCS251_XFR(0x7EF8F8UL)
#define PWMD_CCR7H               MCS251_XFR(0x7EF8F9UL)
#define PWMD_CCR7L               MCS251_XFR(0x7EF8FAUL)
#define PWMD_CCR8H               MCS251_XFR(0x7EF8FBUL)
#define PWMD_CCR8L               MCS251_XFR(0x7EF8FCUL)
#define PWMD_BKR                 MCS251_XFR(0x7EF8FDUL)
#define PWMD_DTR                 MCS251_XFR(0x7EF8FEUL)
#define PWMD_OISR                MCS251_XFR(0x7EF8FFUL)
#define P8OUT                    MCS251_XFR(0x7EF700UL)
#define P9OUT                    MCS251_XFR(0x7EF701UL)
#define PAOUT                    MCS251_XFR(0x7EF702UL)
#define PBOUT                    MCS251_XFR(0x7EF703UL)
#define PCOUT                    MCS251_XFR(0x7EF704UL)
#define PDOUT                    MCS251_XFR(0x7EF705UL)
#define PEOUT                    MCS251_XFR(0x7EF706UL)
#define PFOUT                    MCS251_XFR(0x7EF707UL)
#define P8IN                     MCS251_XFR(0x7EF708UL)
#define P9IN                     MCS251_XFR(0x7EF709UL)
#define PAIN                     MCS251_XFR(0x7EF70AUL)
#define PBIN                     MCS251_XFR(0x7EF70BUL)
#define PCIN                     MCS251_XFR(0x7EF70CUL)
#define PDIN                     MCS251_XFR(0x7EF70DUL)
#define PEIN                     MCS251_XFR(0x7EF70EUL)
#define PFIN                     MCS251_XFR(0x7EF70FUL)
#define P8M0                     MCS251_XFR(0x7EF710UL)
#define P9M0                     MCS251_XFR(0x7EF711UL)
#define PAM0                     MCS251_XFR(0x7EF712UL)
#define PBM0                     MCS251_XFR(0x7EF713UL)
#define PCM0                     MCS251_XFR(0x7EF714UL)
#define PDM0                     MCS251_XFR(0x7EF715UL)
#define PEM0                     MCS251_XFR(0x7EF716UL)
#define PFM0                     MCS251_XFR(0x7EF717UL)
#define P8M1                     MCS251_XFR(0x7EF718UL)
#define P9M1                     MCS251_XFR(0x7EF719UL)
#define PAM1                     MCS251_XFR(0x7EF71AUL)
#define PBM1                     MCS251_XFR(0x7EF71BUL)
#define PCM1                     MCS251_XFR(0x7EF71CUL)
#define PDM1                     MCS251_XFR(0x7EF71DUL)
#define PEM1                     MCS251_XFR(0x7EF71EUL)
#define PFM1                     MCS251_XFR(0x7EF71FUL)
#define P8SETB                   MCS251_XFR(0x7EF720UL)
#define P9SETB                   MCS251_XFR(0x7EF721UL)
#define PASETB                   MCS251_XFR(0x7EF722UL)
#define PBSETB                   MCS251_XFR(0x7EF723UL)
#define PCSETB                   MCS251_XFR(0x7EF724UL)
#define PDSETB                   MCS251_XFR(0x7EF725UL)
#define PESETB                   MCS251_XFR(0x7EF726UL)
#define PFSETB                   MCS251_XFR(0x7EF727UL)
#define P8CLRB                   MCS251_XFR(0x7EF728UL)
#define P9CLRB                   MCS251_XFR(0x7EF729UL)
#define PACLRB                   MCS251_XFR(0x7EF72AUL)
#define PBCLRB                   MCS251_XFR(0x7EF72BUL)
#define PCCLRB                   MCS251_XFR(0x7EF72CUL)
#define PDCLRB                   MCS251_XFR(0x7EF72DUL)
#define PECLRB                   MCS251_XFR(0x7EF72EUL)
#define PFCLRB                   MCS251_XFR(0x7EF72FUL)
#define S5CON                    MCS251_XFR(0x7EF740UL)
#define S5BUF                    MCS251_XFR(0x7EF741UL)
#define S5INTR                   MCS251_XFR(0x7EF742UL)
#define UR5TOTE                  MCS251_XFR(0x7EF743UL)
#define UR5TOCR                  MCS251_XFR(0x7EF744UL)
#define UR5TOSR                  MCS251_XFR(0x7EF745UL)
#define UR5TOTH                  MCS251_XFR(0x7EF746UL)
#define UR5TOTL                  MCS251_XFR(0x7EF747UL)
#define USART5CR1                MCS251_XFR(0x7EF748UL)
#define USART5CR2                MCS251_XFR(0x7EF749UL)
#define USART5CR3                MCS251_XFR(0x7EF74AUL)
#define USART5CR4                MCS251_XFR(0x7EF74BUL)
#define USART5CR5                MCS251_XFR(0x7EF74CUL)
#define USART5GTR                MCS251_XFR(0x7EF74DUL)
#define USART5BR1                MCS251_XFR(0x7EF74EUL)
#define USART5BR2                MCS251_XFR(0x7EF74FUL)
#define DMA_UR5T_CFG             MCS251_XFR(0x7EF750UL)
#define DMA_UR5T_CR              MCS251_XFR(0x7EF751UL)
#define DMA_UR5T_STA             MCS251_XFR(0x7EF752UL)
#define DMA_UR5T_AMTH            MCS251_XFR(0x7EF754UL)
#define DMA_UR5T_AMT             MCS251_XFR(0x7EF755UL)
#define DMA_UR5T_DONEH           MCS251_XFR(0x7EF756UL)
#define DMA_UR5T_DONE            MCS251_XFR(0x7EF757UL)
#define DMA_UR5T_TXAH            MCS251_XFR(0x7EF758UL)
#define DMA_UR5T_TXAL            MCS251_XFR(0x7EF759UL)
#define DMA_UR5_ITVH             MCS251_XFR(0x7EF75EUL)
#define DMA_UR5_ITVL             MCS251_XFR(0x7EF75FUL)
#define DMA_UR5R_CFG             MCS251_XFR(0x7EF760UL)
#define DMA_UR5R_CR              MCS251_XFR(0x7EF761UL)
#define DMA_UR5R_STA             MCS251_XFR(0x7EF762UL)
#define DMA_UR5R_AMTH            MCS251_XFR(0x7EF764UL)
#define DMA_UR5R_AMT             MCS251_XFR(0x7EF765UL)
#define DMA_UR5R_DONEH           MCS251_XFR(0x7EF766UL)
#define DMA_UR5R_DONE            MCS251_XFR(0x7EF767UL)
#define DMA_UR5R_RXAH            MCS251_XFR(0x7EF768UL)
#define DMA_UR5R_RXAL            MCS251_XFR(0x7EF769UL)
#define S5CFG                    MCS251_XFR(0x7EF76DUL)
#define S5ADDR                   MCS251_XFR(0x7EF76EUL)
#define S5ADEN                   MCS251_XFR(0x7EF76FUL)
#define S6CON                    MCS251_XFR(0x7EF770UL)
#define S6BUF                    MCS251_XFR(0x7EF771UL)
#define S6INTR                   MCS251_XFR(0x7EF772UL)
#define UR6TOTE                  MCS251_XFR(0x7EF773UL)
#define UR6TOCR                  MCS251_XFR(0x7EF774UL)
#define UR6TOSR                  MCS251_XFR(0x7EF775UL)
#define UR6TOTH                  MCS251_XFR(0x7EF776UL)
#define UR6TOTL                  MCS251_XFR(0x7EF777UL)
#define USART6CR1                MCS251_XFR(0x7EF778UL)
#define USART6CR2                MCS251_XFR(0x7EF779UL)
#define USART6CR3                MCS251_XFR(0x7EF77AUL)
#define USART6CR4                MCS251_XFR(0x7EF77BUL)
#define USART6CR5                MCS251_XFR(0x7EF77CUL)
#define USART6GTR                MCS251_XFR(0x7EF77DUL)
#define USART6BR1                MCS251_XFR(0x7EF77EUL)
#define USART6BR2                MCS251_XFR(0x7EF77FUL)
#define DMA_UR6T_CFG             MCS251_XFR(0x7EF780UL)
#define DMA_UR6T_CR              MCS251_XFR(0x7EF781UL)
#define DMA_UR6T_STA             MCS251_XFR(0x7EF782UL)
#define DMA_UR6T_AMTH            MCS251_XFR(0x7EF784UL)
#define DMA_UR6T_AMT             MCS251_XFR(0x7EF785UL)
#define DMA_UR6T_DONEH           MCS251_XFR(0x7EF786UL)
#define DMA_UR6T_DONE            MCS251_XFR(0x7EF787UL)
#define DMA_UR6T_TXAH            MCS251_XFR(0x7EF788UL)
#define DMA_UR6T_TXAL            MCS251_XFR(0x7EF789UL)
#define DMA_UR6_ITVH             MCS251_XFR(0x7EF78EUL)
#define DMA_UR6_ITVL             MCS251_XFR(0x7EF78FUL)
#define DMA_UR6R_CFG             MCS251_XFR(0x7EF790UL)
#define DMA_UR6R_CR              MCS251_XFR(0x7EF791UL)
#define DMA_UR6R_STA             MCS251_XFR(0x7EF792UL)
#define DMA_UR6R_AMTH            MCS251_XFR(0x7EF794UL)
#define DMA_UR6R_AMT             MCS251_XFR(0x7EF795UL)
#define DMA_UR6R_DONEH           MCS251_XFR(0x7EF796UL)
#define DMA_UR6R_DONE            MCS251_XFR(0x7EF797UL)
#define DMA_UR6R_RXAH            MCS251_XFR(0x7EF798UL)
#define DMA_UR6R_RXAL            MCS251_XFR(0x7EF799UL)
#define S6CFG                    MCS251_XFR(0x7EF79DUL)
#define S6ADDR                   MCS251_XFR(0x7EF79EUL)
#define S6ADEN                   MCS251_XFR(0x7EF79FUL)
#define S7CON                    MCS251_XFR(0x7EF7A0UL)
#define S7BUF                    MCS251_XFR(0x7EF7A1UL)
#define S7INTR                   MCS251_XFR(0x7EF7A2UL)
#define UR7TOTE                  MCS251_XFR(0x7EF7A3UL)
#define UR7TOCR                  MCS251_XFR(0x7EF7A4UL)
#define UR7TOSR                  MCS251_XFR(0x7EF7A5UL)
#define UR7TOTH                  MCS251_XFR(0x7EF7A6UL)
#define UR7TOTL                  MCS251_XFR(0x7EF7A7UL)
#define USART7CR1                MCS251_XFR(0x7EF7A8UL)
#define USART7CR2                MCS251_XFR(0x7EF7A9UL)
#define USART7CR3                MCS251_XFR(0x7EF7AAUL)
#define USART7CR4                MCS251_XFR(0x7EF7ABUL)
#define USART7CR5                MCS251_XFR(0x7EF7ACUL)
#define USART7GTR                MCS251_XFR(0x7EF7ADUL)
#define USART7BR1                MCS251_XFR(0x7EF7AEUL)
#define USART7BR2                MCS251_XFR(0x7EF7AFUL)
#define DMA_UR7T_CFG             MCS251_XFR(0x7EF7B0UL)
#define DMA_UR7T_CR              MCS251_XFR(0x7EF7B1UL)
#define DMA_UR7T_STA             MCS251_XFR(0x7EF7B2UL)
#define DMA_UR7T_AMTH            MCS251_XFR(0x7EF7B4UL)
#define DMA_UR7T_AMT             MCS251_XFR(0x7EF7B5UL)
#define DMA_UR7T_DONEH           MCS251_XFR(0x7EF7B6UL)
#define DMA_UR7T_DONE            MCS251_XFR(0x7EF7B7UL)
#define DMA_UR7T_TXAH            MCS251_XFR(0x7EF7B8UL)
#define DMA_UR7T_TXAL            MCS251_XFR(0x7EF7B9UL)
#define DMA_UR7_ITVH             MCS251_XFR(0x7EF7BEUL)
#define DMA_UR7_ITVL             MCS251_XFR(0x7EF7BFUL)
#define DMA_UR7R_CFG             MCS251_XFR(0x7EF7C0UL)
#define DMA_UR7R_CR              MCS251_XFR(0x7EF7C1UL)
#define DMA_UR7R_STA             MCS251_XFR(0x7EF7C2UL)
#define DMA_UR7R_AMTH            MCS251_XFR(0x7EF7C4UL)
#define DMA_UR7R_AMT             MCS251_XFR(0x7EF7C5UL)
#define DMA_UR7R_DONEH           MCS251_XFR(0x7EF7C6UL)
#define DMA_UR7R_DONE            MCS251_XFR(0x7EF7C7UL)
#define DMA_UR7R_RXAH            MCS251_XFR(0x7EF7C8UL)
#define DMA_UR7R_RXAL            MCS251_XFR(0x7EF7C9UL)
#define S7CFG                    MCS251_XFR(0x7EF7CDUL)
#define S7ADDR                   MCS251_XFR(0x7EF7CEUL)
#define S7ADEN                   MCS251_XFR(0x7EF7CFUL)
#define S8CON                    MCS251_XFR(0x7EF7D0UL)
#define S8BUF                    MCS251_XFR(0x7EF7D1UL)
#define S8INTR                   MCS251_XFR(0x7EF7D2UL)
#define UR8TOTE                  MCS251_XFR(0x7EF7D3UL)
#define UR8TOCR                  MCS251_XFR(0x7EF7D4UL)
#define UR8TOSR                  MCS251_XFR(0x7EF7D5UL)
#define UR8TOTH                  MCS251_XFR(0x7EF7D6UL)
#define UR8TOTL                  MCS251_XFR(0x7EF7D7UL)
#define USART8CR1                MCS251_XFR(0x7EF7D8UL)
#define USART8CR2                MCS251_XFR(0x7EF7D9UL)
#define USART8CR3                MCS251_XFR(0x7EF7DAUL)
#define USART8CR4                MCS251_XFR(0x7EF7DBUL)
#define USART8CR5                MCS251_XFR(0x7EF7DCUL)
#define USART8GTR                MCS251_XFR(0x7EF7DDUL)
#define USART8BR1                MCS251_XFR(0x7EF7DEUL)
#define USART8BR2                MCS251_XFR(0x7EF7DFUL)
#define DMA_UR8T_CFG             MCS251_XFR(0x7EF7E0UL)
#define DMA_UR8T_CR              MCS251_XFR(0x7EF7E1UL)
#define DMA_UR8T_STA             MCS251_XFR(0x7EF7E2UL)
#define DMA_UR8T_AMTH            MCS251_XFR(0x7EF7E4UL)
#define DMA_UR8T_AMT             MCS251_XFR(0x7EF7E5UL)
#define DMA_UR8T_DONEH           MCS251_XFR(0x7EF7E6UL)
#define DMA_UR8T_DONE            MCS251_XFR(0x7EF7E7UL)
#define DMA_UR8T_TXAH            MCS251_XFR(0x7EF7E8UL)
#define DMA_UR8T_TXAL            MCS251_XFR(0x7EF7E9UL)
#define DMA_UR8_ITVH             MCS251_XFR(0x7EF7EEUL)
#define DMA_UR8_ITVL             MCS251_XFR(0x7EF7EFUL)
#define DMA_UR8R_CFG             MCS251_XFR(0x7EF7F0UL)
#define DMA_UR8R_CR              MCS251_XFR(0x7EF7F1UL)
#define DMA_UR8R_STA             MCS251_XFR(0x7EF7F2UL)
#define DMA_UR8R_AMTH            MCS251_XFR(0x7EF7F4UL)
#define DMA_UR8R_AMT             MCS251_XFR(0x7EF7F5UL)
#define DMA_UR8R_DONEH           MCS251_XFR(0x7EF7F6UL)
#define DMA_UR8R_DONE            MCS251_XFR(0x7EF7F7UL)
#define DMA_UR8R_RXAH            MCS251_XFR(0x7EF7F8UL)
#define DMA_UR8R_RXAL            MCS251_XFR(0x7EF7F9UL)
#define S8CFG                    MCS251_XFR(0x7EF7FDUL)
#define S8ADDR                   MCS251_XFR(0x7EF7FEUL)
#define S8ADEN                   MCS251_XFR(0x7EF7FFUL)
#define PGA1_CR1                 MCS251_XFR(0x7EF600UL)
#define PGA1_CR2                 MCS251_XFR(0x7EF601UL)
#define PGA2_CR1                 MCS251_XFR(0x7EF604UL)
#define PGA2_CR2                 MCS251_XFR(0x7EF605UL)
#define PGA3_CR1                 MCS251_XFR(0x7EF608UL)
#define PGA3_CR2                 MCS251_XFR(0x7EF609UL)
#define PGA4_CR1                 MCS251_XFR(0x7EF60CUL)
#define PGA4_CR2                 MCS251_XFR(0x7EF60DUL)
#define DAC1_CR                  MCS251_XFR(0x7EF610UL)
#define DAC1_STA                 MCS251_XFR(0x7EF611UL)
#define DAC1_DATH                MCS251_XFR(0x7EF612UL)
#define DAC1_DATL                MCS251_XFR(0x7EF613UL)
#define DAC1_DIVH                MCS251_XFR(0x7EF614UL)
#define DAC1_DIVL                MCS251_XFR(0x7EF615UL)
#define DMA_DAC1_CFG             MCS251_XFR(0x7EF618UL)
#define DMA_DAC1_CR              MCS251_XFR(0x7EF619UL)
#define DMA_DAC1_STA             MCS251_XFR(0x7EF61AUL)
#define DMA_DAC1_AMTH            MCS251_XFR(0x7EF61CUL)
#define DMA_DAC1_AMT             MCS251_XFR(0x7EF61DUL)
#define DMA_DAC1_DONEH           MCS251_XFR(0x7EF61EUL)
#define DMA_DAC1_DONE            MCS251_XFR(0x7EF61FUL)
#define DMA_DAC1_TXAH            MCS251_XFR(0x7EF620UL)
#define DMA_DAC1_TXAL            MCS251_XFR(0x7EF621UL)
#define DMA_DAC1_CR1             MCS251_XFR(0x7EF623UL)
#define DMA_DAC1_ITVH            MCS251_XFR(0x7EF626UL)
#define DMA_DAC1_ITVL            MCS251_XFR(0x7EF627UL)
#define DAC2_CR                  MCS251_XFR(0x7EF628UL)
#define DAC2_STA                 MCS251_XFR(0x7EF629UL)
#define DAC2_DATH                MCS251_XFR(0x7EF62AUL)
#define DAC2_DATL                MCS251_XFR(0x7EF62BUL)
#define DAC2_DIVH                MCS251_XFR(0x7EF62CUL)
#define DAC2_DIVL                MCS251_XFR(0x7EF62DUL)
#define DMA_DAC2_CFG             MCS251_XFR(0x7EF630UL)
#define DMA_DAC2_CR              MCS251_XFR(0x7EF631UL)
#define DMA_DAC2_STA             MCS251_XFR(0x7EF632UL)
#define DMA_DAC2_AMTH            MCS251_XFR(0x7EF634UL)
#define DMA_DAC2_AMT             MCS251_XFR(0x7EF635UL)
#define DMA_DAC2_DONEH           MCS251_XFR(0x7EF636UL)
#define DMA_DAC2_DONE            MCS251_XFR(0x7EF637UL)
#define DMA_DAC2_TXAH            MCS251_XFR(0x7EF638UL)
#define DMA_DAC2_TXAL            MCS251_XFR(0x7EF639UL)
#define DMA_DAC2_CR1             MCS251_XFR(0x7EF63BUL)
#define DMA_DAC2_ITVH            MCS251_XFR(0x7EF63EUL)
#define DMA_DAC2_ITVL            MCS251_XFR(0x7EF63FUL)
#define DMA_PWMET_CFG            MCS251_XFR(0x7EF668UL)
#define DMA_PWMET_CR             MCS251_XFR(0x7EF669UL)
#define DMA_PWMET_STA            MCS251_XFR(0x7EF66AUL)
#define DMA_PWMET_AMTH           MCS251_XFR(0x7EF66CUL)
#define DMA_PWMET_AMT            MCS251_XFR(0x7EF66DUL)
#define DMA_PWMET_DONEH          MCS251_XFR(0x7EF66EUL)
#define DMA_PWMET_DONE           MCS251_XFR(0x7EF66FUL)
#define DMA_PWMET_TXAH           MCS251_XFR(0x7EF670UL)
#define DMA_PWMET_TXAL           MCS251_XFR(0x7EF671UL)
#define DMA_PWME_ITVH            MCS251_XFR(0x7EF676UL)
#define DMA_PWME_ITVL            MCS251_XFR(0x7EF677UL)
#define DMA_PWMER_CFG            MCS251_XFR(0x7EF678UL)
#define DMA_PWMER_CR             MCS251_XFR(0x7EF679UL)
#define DMA_PWMER_STA            MCS251_XFR(0x7EF67AUL)
#define DMA_PWMER_AMTH           MCS251_XFR(0x7EF67CUL)
#define DMA_PWMER_AMT            MCS251_XFR(0x7EF67DUL)
#define DMA_PWMER_DONEH          MCS251_XFR(0x7EF67EUL)
#define DMA_PWMER_DONE           MCS251_XFR(0x7EF67FUL)
#define DMA_PWMER_RXAH           MCS251_XFR(0x7EF680UL)
#define DMA_PWMER_RXAL           MCS251_XFR(0x7EF681UL)
#define HSPWME_CFG               MCS251_XFR(0x7EF688UL)
#define HSPWME_ADR               MCS251_XFR(0x7EF689UL)
#define HSPWME_DAT               MCS251_XFR(0x7EF68AUL)
#define PWME_CLKDIV              MCS251_XFR(0x7EF68BUL)
#define HSPWMF_CFG               MCS251_XFR(0x7EF68CUL)
#define HSPWMF_ADR               MCS251_XFR(0x7EF68DUL)
#define HSPWMF_DAT               MCS251_XFR(0x7EF68EUL)
#define PWMF_CLKDIV              MCS251_XFR(0x7EF68FUL)
#define PWME_ENO2                MCS251_XFR(0x7EF690UL)
#define PWME_IOAUX2              MCS251_XFR(0x7EF691UL)
#define PWME_CR3                 MCS251_XFR(0x7EF692UL)
#define PWME_SR3                 MCS251_XFR(0x7EF693UL)
#define PWME_CCER3               MCS251_XFR(0x7EF694UL)
#define PWME_CCMR1X              MCS251_XFR(0x7EF698UL)
#define PWME_CCMR2X              MCS251_XFR(0x7EF699UL)
#define PWME_CCMR3X              MCS251_XFR(0x7EF69AUL)
#define PWME_CCMR4X              MCS251_XFR(0x7EF69BUL)
#define PWME_CCMR5               MCS251_XFR(0x7EF69CUL)
#define PWME_CCMR5X              MCS251_XFR(0x7EF69DUL)
#define PWME_CCMR6               MCS251_XFR(0x7EF69EUL)
#define PWME_CCMR6X              MCS251_XFR(0x7EF69FUL)
#define PWME_CCR5H               MCS251_XFR(0x7EF6A0UL)
#define PWME_CCR5L               MCS251_XFR(0x7EF6A1UL)
#define PWME_CCR5X               MCS251_XFR(0x7EF6A2UL)
#define PWME_CCR6H               MCS251_XFR(0x7EF6A3UL)
#define PWME_CCR6L               MCS251_XFR(0x7EF6A4UL)
#define PWME_DER                 MCS251_XFR(0x7EF6A8UL)
#define PWME_DBA                 MCS251_XFR(0x7EF6A9UL)
#define PWME_DBL                 MCS251_XFR(0x7EF6AAUL)
#define PWME_DMACR               MCS251_XFR(0x7EF6ABUL)
#define PWME_ETRPS               MCS251_XFR(0x7EF6B0UL)
#define PWME_ENO                 MCS251_XFR(0x7EF6B1UL)
#define PWME_PS                  MCS251_XFR(0x7EF6B2UL)
#define PWME_IOAUX               MCS251_XFR(0x7EF6B3UL)
#define PWMF_ETRPS               MCS251_XFR(0x7EF6B4UL)
#define PWMF_ENO                 MCS251_XFR(0x7EF6B5UL)
#define PWMF_PS                  MCS251_XFR(0x7EF6B6UL)
#define PWMF_IOAUX               MCS251_XFR(0x7EF6B7UL)
#define PWME_PS2                 MCS251_XFR(0x7EF6B8UL)
#define PWME_RCRH                MCS251_XFR(0x7EF6B9UL)
#define PWMF_RCRH                MCS251_XFR(0x7EF6BAUL)
#define PWME_CR1                 MCS251_XFR(0x7EF6C0UL)
#define PWME_CR2                 MCS251_XFR(0x7EF6C1UL)
#define PWME_SMCR                MCS251_XFR(0x7EF6C2UL)
#define PWME_ETR                 MCS251_XFR(0x7EF6C3UL)
#define PWME_IER                 MCS251_XFR(0x7EF6C4UL)
#define PWME_SR1                 MCS251_XFR(0x7EF6C5UL)
#define PWME_SR2                 MCS251_XFR(0x7EF6C6UL)
#define PWME_EGR                 MCS251_XFR(0x7EF6C7UL)
#define PWME_CCMR1               MCS251_XFR(0x7EF6C8UL)
#define PWME_CCMR2               MCS251_XFR(0x7EF6C9UL)
#define PWME_CCMR3               MCS251_XFR(0x7EF6CAUL)
#define PWME_CCMR4               MCS251_XFR(0x7EF6CBUL)
#define PWME_CCER1               MCS251_XFR(0x7EF6CCUL)
#define PWME_CCER2               MCS251_XFR(0x7EF6CDUL)
#define PWME_CNTRH               MCS251_XFR(0x7EF6CEUL)
#define PWME_CNTRL               MCS251_XFR(0x7EF6CFUL)
#define PWME_PSCRH               MCS251_XFR(0x7EF6D0UL)
#define PWME_PSCRL               MCS251_XFR(0x7EF6D1UL)
#define PWME_ARRH                MCS251_XFR(0x7EF6D2UL)
#define PWME_ARRL                MCS251_XFR(0x7EF6D3UL)
#define PWME_RCR                 MCS251_XFR(0x7EF6D4UL)
#define PWME_CCR1H               MCS251_XFR(0x7EF6D5UL)
#define PWME_CCR1L               MCS251_XFR(0x7EF6D6UL)
#define PWME_CCR2H               MCS251_XFR(0x7EF6D7UL)
#define PWME_CCR2L               MCS251_XFR(0x7EF6D8UL)
#define PWME_CCR3H               MCS251_XFR(0x7EF6D9UL)
#define PWME_CCR3L               MCS251_XFR(0x7EF6DAUL)
#define PWME_CCR4H               MCS251_XFR(0x7EF6DBUL)
#define PWME_CCR4L               MCS251_XFR(0x7EF6DCUL)
#define PWME_BKR                 MCS251_XFR(0x7EF6DDUL)
#define PWME_DTR                 MCS251_XFR(0x7EF6DEUL)
#define PWME_OISR                MCS251_XFR(0x7EF6DFUL)
#define PWMF_CR1                 MCS251_XFR(0x7EF6E0UL)
#define PWMF_CR2                 MCS251_XFR(0x7EF6E1UL)
#define PWMF_SMCR                MCS251_XFR(0x7EF6E2UL)
#define PWMF_ETR                 MCS251_XFR(0x7EF6E3UL)
#define PWMF_IER                 MCS251_XFR(0x7EF6E4UL)
#define PWMF_SR1                 MCS251_XFR(0x7EF6E5UL)
#define PWMF_SR2                 MCS251_XFR(0x7EF6E6UL)
#define PWMF_EGR                 MCS251_XFR(0x7EF6E7UL)
#define PWMF_CCMR1               MCS251_XFR(0x7EF6E8UL)
#define PWMF_CCMR2               MCS251_XFR(0x7EF6E9UL)
#define PWMF_CCMR3               MCS251_XFR(0x7EF6EAUL)
#define PWMF_CCMR4               MCS251_XFR(0x7EF6EBUL)
#define PWMF_CCER1               MCS251_XFR(0x7EF6ECUL)
#define PWMF_CCER2               MCS251_XFR(0x7EF6EDUL)
#define PWMF_CNTRH               MCS251_XFR(0x7EF6EEUL)
#define PWMF_CNTRL               MCS251_XFR(0x7EF6EFUL)
#define PWMF_PSCRH               MCS251_XFR(0x7EF6F0UL)
#define PWMF_PSCRL               MCS251_XFR(0x7EF6F1UL)
#define PWMF_ARRH                MCS251_XFR(0x7EF6F2UL)
#define PWMF_ARRL                MCS251_XFR(0x7EF6F3UL)
#define PWMF_RCR                 MCS251_XFR(0x7EF6F4UL)
#define PWMF_CCR5H               MCS251_XFR(0x7EF6F5UL)
#define PWMF_CCR5L               MCS251_XFR(0x7EF6F6UL)
#define PWMF_CCR6H               MCS251_XFR(0x7EF6F7UL)
#define PWMF_CCR6L               MCS251_XFR(0x7EF6F8UL)
#define PWMF_CCR7H               MCS251_XFR(0x7EF6F9UL)
#define PWMF_CCR7L               MCS251_XFR(0x7EF6FAUL)
#define PWMF_CCR8H               MCS251_XFR(0x7EF6FBUL)
#define PWMF_CCR8L               MCS251_XFR(0x7EF6FCUL)
#define PWMF_BKR                 MCS251_XFR(0x7EF6FDUL)
#define PWMF_DTR                 MCS251_XFR(0x7EF6FEUL)
#define PWMF_OISR                MCS251_XFR(0x7EF6FFUL)
#define P0OS                     MCS251_XFR(0x7EF500UL)
#define P1OS                     MCS251_XFR(0x7EF501UL)
#define P2OS                     MCS251_XFR(0x7EF502UL)
#define P3OS                     MCS251_XFR(0x7EF503UL)
#define P4OS                     MCS251_XFR(0x7EF504UL)
#define P5OS                     MCS251_XFR(0x7EF505UL)
#define P6OS                     MCS251_XFR(0x7EF506UL)
#define P7OS                     MCS251_XFR(0x7EF507UL)
#define P8OS                     MCS251_XFR(0x7EF508UL)
#define P9OS                     MCS251_XFR(0x7EF509UL)
#define PAOS                     MCS251_XFR(0x7EF50AUL)
#define PBOS                     MCS251_XFR(0x7EF50BUL)
#define PCOS                     MCS251_XFR(0x7EF50CUL)
#define PDOS                     MCS251_XFR(0x7EF50DUL)
#define PEOS                     MCS251_XFR(0x7EF50EUL)
#define PFOS                     MCS251_XFR(0x7EF50FUL)
#define P0OD                     MCS251_XFR(0x7EF510UL)
#define P1OD                     MCS251_XFR(0x7EF511UL)
#define P2OD                     MCS251_XFR(0x7EF512UL)
#define P3OD                     MCS251_XFR(0x7EF513UL)
#define P4OD                     MCS251_XFR(0x7EF514UL)
#define P5OD                     MCS251_XFR(0x7EF515UL)
#define P6OD                     MCS251_XFR(0x7EF516UL)
#define P7OD                     MCS251_XFR(0x7EF517UL)
#define P8OD                     MCS251_XFR(0x7EF518UL)
#define P9OD                     MCS251_XFR(0x7EF519UL)
#define PAOD                     MCS251_XFR(0x7EF51AUL)
#define PBOD                     MCS251_XFR(0x7EF51BUL)
#define PCOD                     MCS251_XFR(0x7EF51CUL)
#define PDOD                     MCS251_XFR(0x7EF51DUL)
#define PEOD                     MCS251_XFR(0x7EF51EUL)
#define PFOD                     MCS251_XFR(0x7EF51FUL)
#define DMA_CAN1T_CFG            MCS251_XFR(0x7EF520UL)
#define DMA_CAN1T_CR             MCS251_XFR(0x7EF521UL)
#define DMA_CAN1T_STA            MCS251_XFR(0x7EF522UL)
#define DMA_CAN1T_AMTH           MCS251_XFR(0x7EF524UL)
#define DMA_CAN1T_AMT            MCS251_XFR(0x7EF525UL)
#define DMA_CAN1T_DONEH          MCS251_XFR(0x7EF526UL)
#define DMA_CAN1T_DONE           MCS251_XFR(0x7EF527UL)
#define DMA_CAN1T_TXAH           MCS251_XFR(0x7EF528UL)
#define DMA_CAN1T_TXAL           MCS251_XFR(0x7EF529UL)
#define DMA_CAN1T_FRM            MCS251_XFR(0x7EF52BUL)
#define DMA_CAN1T_FCNTH          MCS251_XFR(0x7EF52CUL)
#define DMA_CAN1T_FCNTL          MCS251_XFR(0x7EF52DUL)
#define DMA_CAN1_ITVH            MCS251_XFR(0x7EF52EUL)
#define DMA_CAN1_ITVL            MCS251_XFR(0x7EF52FUL)
#define DMA_CAN1R_CFG            MCS251_XFR(0x7EF530UL)
#define DMA_CAN1R_CR             MCS251_XFR(0x7EF531UL)
#define DMA_CAN1R_STA            MCS251_XFR(0x7EF532UL)
#define CAN1_AUX_CR              MCS251_XFR(0x7EF533UL)
#define DMA_CAN1R_AMTH           MCS251_XFR(0x7EF534UL)
#define DMA_CAN1R_AMT            MCS251_XFR(0x7EF535UL)
#define DMA_CAN1R_DONEH          MCS251_XFR(0x7EF536UL)
#define DMA_CAN1R_DONE           MCS251_XFR(0x7EF537UL)
#define DMA_CAN1R_RXAH           MCS251_XFR(0x7EF538UL)
#define DMA_CAN1R_RXAL           MCS251_XFR(0x7EF539UL)
#define DMA_CAN1R_FRM            MCS251_XFR(0x7EF53BUL)
#define DMA_CAN1R_FCNTH          MCS251_XFR(0x7EF53CUL)
#define DMA_CAN1R_FCNTL          MCS251_XFR(0x7EF53DUL)
#define DMA_CAN2T_CFG            MCS251_XFR(0x7EF540UL)
#define DMA_CAN2T_CR             MCS251_XFR(0x7EF541UL)
#define DMA_CAN2T_STA            MCS251_XFR(0x7EF542UL)
#define DMA_CAN2T_AMTH           MCS251_XFR(0x7EF544UL)
#define DMA_CAN2T_AMT            MCS251_XFR(0x7EF545UL)
#define DMA_CAN2T_DONEH          MCS251_XFR(0x7EF546UL)
#define DMA_CAN2T_DONE           MCS251_XFR(0x7EF547UL)
#define DMA_CAN2T_TXAH           MCS251_XFR(0x7EF548UL)
#define DMA_CAN2T_TXAL           MCS251_XFR(0x7EF549UL)
#define DMA_CAN2T_FRM            MCS251_XFR(0x7EF54BUL)
#define DMA_CAN2T_FCNTH          MCS251_XFR(0x7EF54CUL)
#define DMA_CAN2T_FCNTL          MCS251_XFR(0x7EF54DUL)
#define DMA_CAN2_ITVH            MCS251_XFR(0x7EF54EUL)
#define DMA_CAN2_ITVL            MCS251_XFR(0x7EF54FUL)
#define DMA_CAN2R_CFG            MCS251_XFR(0x7EF550UL)
#define DMA_CAN2R_CR             MCS251_XFR(0x7EF551UL)
#define CAN2_AUX_CR              MCS251_XFR(0x7EF553UL)
#define DMA_CAN2R_STA            MCS251_XFR(0x7EF552UL)
#define DMA_CAN2R_AMTH           MCS251_XFR(0x7EF554UL)
#define DMA_CAN2R_AMT            MCS251_XFR(0x7EF555UL)
#define DMA_CAN2R_DONEH          MCS251_XFR(0x7EF556UL)
#define DMA_CAN2R_DONE           MCS251_XFR(0x7EF557UL)
#define DMA_CAN2R_RXAH           MCS251_XFR(0x7EF558UL)
#define DMA_CAN2R_RXAL           MCS251_XFR(0x7EF559UL)
#define DMA_CAN2R_FRM            MCS251_XFR(0x7EF55BUL)
#define DMA_CAN2R_FCNTH          MCS251_XFR(0x7EF55CUL)
#define DMA_CAN2R_FCNTL          MCS251_XFR(0x7EF55DUL)
#define IAP_DATA0                MCS251_XFR(0x7EF5F0UL)
#define IAP_DATA1                MCS251_XFR(0x7EF5F1UL)
#define IAP_DATA2                MCS251_XFR(0x7EF5F2UL)
#define IAP_DATA3                MCS251_XFR(0x7EF5F3UL)
#define IAP_DATA4                MCS251_XFR(0x7EF5F4UL)
#define IAP_DATA5                MCS251_XFR(0x7EF5F5UL)
#define IAP_DATA6                MCS251_XFR(0x7EF5F6UL)
#define IAP_DATA7                MCS251_XFR(0x7EF5F7UL)
#define IAP_DATA8                MCS251_XFR(0x7EF5F8UL)
#define IAP_DATA9                MCS251_XFR(0x7EF5F9UL)
#define IAP_DATAA                MCS251_XFR(0x7EF5FAUL)
#define IAP_DATAB                MCS251_XFR(0x7EF5FBUL)
#define IAP_DATAC                MCS251_XFR(0x7EF5FCUL)
#define IAP_DATAD                MCS251_XFR(0x7EF5FDUL)
#define IAP_DATAE                MCS251_XFR(0x7EF5FEUL)
#define IAP_DATAF                MCS251_XFR(0x7EF5FFUL)
#define CAN_CFG_STAT             MCS251_XFR(0x7EF4A0UL)
#define CAN_TCMD                 MCS251_XFR(0x7EF4A1UL)
#define CAN_TCTRL                MCS251_XFR(0x7EF4A2UL)
#define CAN_RCTRL                MCS251_XFR(0x7EF4A3UL)
#define CAN_TIE                  MCS251_XFR(0x7EF4A4UL)
#define CAN_RTIF                 MCS251_XFR(0x7EF4A5UL)
#define CAN_ERRINT               MCS251_XFR(0x7EF4A6UL)
#define CAN_LIMIT                MCS251_XFR(0x7EF4A7UL)
#define CAN_S_SEG1               MCS251_XFR(0x7EF4A8UL)
#define CAN_S_SEG2               MCS251_XFR(0x7EF4A9UL)
#define CAN_S_SJW                MCS251_XFR(0x7EF4AAUL)
#define CAN_S_PRESC              MCS251_XFR(0x7EF4ABUL)
#define CAN_F_SEG1               MCS251_XFR(0x7EF4ACUL)
#define CAN_F_SEG2               MCS251_XFR(0x7EF4ADUL)
#define CAN_F_SJW                MCS251_XFR(0x7EF4AEUL)
#define CAN_F_PRESC              MCS251_XFR(0x7EF4AFUL)
#define CAN_EALCAP               MCS251_XFR(0x7EF4B0UL)
#define CAN_TDC                  MCS251_XFR(0x7EF4B1UL)
#define CAN_RECNT                MCS251_XFR(0x7EF4B2UL)
#define CAN_TECNT                MCS251_XFR(0x7EF4B3UL)
#define CAN_ACFCTRL              MCS251_XFR(0x7EF4B4UL)
#define CAN_TIMECFG              MCS251_XFR(0x7EF4B5UL)
#define CAN1_ACFEN               MCS251_XFR(0x7EF4B6UL)
#define CAN_ACF_EN0              MCS251_XFR(0x7EF4B6UL)
#define CAN_ACF_EN1              MCS251_XFR(0x7EF4B7UL)
#define CAN_ACF0                 MCS251_XFR(0x7EF4B8UL)
#define CAN_ACF1                 MCS251_XFR(0x7EF4B9UL)
#define CAN_ACF2                 MCS251_XFR(0x7EF4BAUL)
#define CAN_ACF3                 MCS251_XFR(0x7EF4BBUL)
#define CAN_VER0                 MCS251_XFR(0x7EF4BCUL)
#define CAN_VER1                 MCS251_XFR(0x7EF4BDUL)
#define CAN_TBSLOT               MCS251_XFR(0x7EF4BEUL)
#define CAN_TTCFG                MCS251_XFR(0x7EF4BFUL)
#define CAN_REF_MSG0             MCS251_XFR(0x7EF4C0UL)
#define CAN_REF_MSG1             MCS251_XFR(0x7EF4C1UL)
#define CAN_REF_MSG2             MCS251_XFR(0x7EF4C2UL)
#define CAN_REF_MSG3             MCS251_XFR(0x7EF4C3UL)
#define CAN_TRIG_CFG0            MCS251_XFR(0x7EF4C4UL)
#define CAN_TRIG_CFG1            MCS251_XFR(0x7EF4C5UL)
#define CAN_TT_CFG0              MCS251_XFR(0x7EF4C6UL)
#define CAN_TT_CFG1              MCS251_XFR(0x7EF4C7UL)
#define CAN_TT_WTRIG0            MCS251_XFR(0x7EF4C8UL)
#define CAN_TT_WTRIG1            MCS251_XFR(0x7EF4C9UL)
#define P0IM2                    MCS251_XFR(0x7EF4E0UL)
#define P1IM2                    MCS251_XFR(0x7EF4E1UL)
#define P2IM2                    MCS251_XFR(0x7EF4E2UL)
#define P3IM2                    MCS251_XFR(0x7EF4E3UL)
#define P4IM2                    MCS251_XFR(0x7EF4E4UL)
#define P5IM2                    MCS251_XFR(0x7EF4E5UL)
#define P6IM2                    MCS251_XFR(0x7EF4E6UL)
#define P7IM2                    MCS251_XFR(0x7EF4E7UL)
#define P8IM2                    MCS251_XFR(0x7EF4E8UL)
#define P9IM2                    MCS251_XFR(0x7EF4E9UL)
#define PAIM2                    MCS251_XFR(0x7EF4EAUL)
#define PBIM2                    MCS251_XFR(0x7EF4EBUL)
#define PCIM2                    MCS251_XFR(0x7EF4ECUL)
#define PDIM2                    MCS251_XFR(0x7EF4EDUL)
#define PEIM2                    MCS251_XFR(0x7EF4EEUL)
#define PFIM2                    MCS251_XFR(0x7EF4EFUL)
#define CAN2_CFG_STAT            MCS251_XFR(0x7EF3A0UL)
#define CAN2_TCMD                MCS251_XFR(0x7EF3A1UL)
#define CAN2_TCTRL               MCS251_XFR(0x7EF3A2UL)
#define CAN2_RCTRL               MCS251_XFR(0x7EF3A3UL)
#define CAN2_TIE                 MCS251_XFR(0x7EF3A4UL)
#define CAN2_RTIF                MCS251_XFR(0x7EF3A5UL)
#define CAN2_ERRINT              MCS251_XFR(0x7EF3A6UL)
#define CAN2_LIMIT               MCS251_XFR(0x7EF3A7UL)
#define CAN2_S_SEG1              MCS251_XFR(0x7EF3A8UL)
#define CAN2_S_SEG2              MCS251_XFR(0x7EF3A9UL)
#define CAN2_S_SJW               MCS251_XFR(0x7EF3AAUL)
#define CAN2_S_PRESC             MCS251_XFR(0x7EF3ABUL)
#define CAN2_F_SEG1              MCS251_XFR(0x7EF3ACUL)
#define CAN2_F_SEG2              MCS251_XFR(0x7EF3ADUL)
#define CAN2_F_SJW               MCS251_XFR(0x7EF3AEUL)
#define CAN2_F_PRESC             MCS251_XFR(0x7EF3AFUL)
#define CAN2_EALCAP              MCS251_XFR(0x7EF3B0UL)
#define CAN2_TDC                 MCS251_XFR(0x7EF3B1UL)
#define CAN2_RECNT               MCS251_XFR(0x7EF3B2UL)
#define CAN2_TECNT               MCS251_XFR(0x7EF3B3UL)
#define CAN2_ACFCTRL             MCS251_XFR(0x7EF3B4UL)
#define CAN2_TIMECFG             MCS251_XFR(0x7EF3B5UL)
#define CAN2_ACF_EN0             MCS251_XFR(0x7EF3B6UL)
#define CAN2_ACF_EN1             MCS251_XFR(0x7EF3B7UL)
#define CAN2_ACF0                MCS251_XFR(0x7EF3B8UL)
#define CAN2_ACF1                MCS251_XFR(0x7EF3B9UL)
#define CAN2_ACF2                MCS251_XFR(0x7EF3BAUL)
#define CAN2_ACF3                MCS251_XFR(0x7EF3BBUL)
#define CAN2_VER0                MCS251_XFR(0x7EF3BCUL)
#define CAN2_VER1                MCS251_XFR(0x7EF3BDUL)
#define CAN2_TBSLOT              MCS251_XFR(0x7EF3BEUL)
#define CAN2_TTCFG               MCS251_XFR(0x7EF3BFUL)
#define CAN2_REF_MSG0            MCS251_XFR(0x7EF3C0UL)
#define CAN2_REF_MSG1            MCS251_XFR(0x7EF3C1UL)
#define CAN2_REF_MSG2            MCS251_XFR(0x7EF3C2UL)
#define CAN2_REF_MSG3            MCS251_XFR(0x7EF3C3UL)
#define CAN2_TRIG_CFG0           MCS251_XFR(0x7EF3C4UL)
#define CAN2_TRIG_CFG1           MCS251_XFR(0x7EF3C5UL)
#define CAN2_TT_CFG0             MCS251_XFR(0x7EF3C6UL)
#define CAN2_TT_CFG1             MCS251_XFR(0x7EF3C7UL)
#define CAN2_TT_WTRIG0           MCS251_XFR(0x7EF3C8UL)
#define CAN2_TT_WTRIG1           MCS251_XFR(0x7EF3C9UL)
/* Repaired STC32G144K246 XFR definitions (official header had trailing `))`).
 * 修复官方头中尾部多余 `))` 笔误的 STC32G144K246 XFR 定义。 */
#define CPU_CRR_CFG              MCS251_XFR(0x7EF4F0UL)
#define CPU_CRR_KEY0             MCS251_XFR(0x7EF4F1UL)
#define CPU_CRR_KEY1             MCS251_XFR(0x7EF4F2UL)
#define CPU_CRR_KEY2             MCS251_XFR(0x7EF4F3UL)
#define CRR_READ_SELH            MCS251_XFR(0x7EF4F4UL)
#define CRR_READ_SELL            MCS251_XFR(0x7EF4F5UL)
#define CRR_READ_DATA            MCS251_XFR(0x7EF4F6UL)
#define IO_WUP_PRD               MCS251_XFR(0x7EF4F8UL)
#define CPU_WUP_PRD              MCS251_XFR(0x7EF4F9UL)
#define CPU_STOP_CNTH            MCS251_XFR(0x7EF4FAUL)
#define CPU_STOP_CNTL            MCS251_XFR(0x7EF4FBUL)

/*===----------------------------------------------------------------------===*
 * Legal Interrupt Vector Numbers (109 vectors)
 * 合法硬件中断向量号表（共 109 个）
 * Synchronized 1:1 with llvm/include/llvm/BinaryFormat/MCS251ISR.h (G1 profile).
 * 与 llvm/include/llvm/BinaryFormat/MCS251ISR.h（G1 拓扑）1:1 严格对齐。
 * Reserved (7, 13, 22-23, 32-35, 81, 92-95, 100-101, 113) and System (14-15)
 * slots are rejected by the compiler and intentionally omitted.
 * 保留槽与系统槽会被编译器拒收，此处刻意省略。
 *===----------------------------------------------------------------------===*/
#define INT0_VECTOR              0    /* 0xFF0003 */
#define TMR0_VECTOR              1    /* 0xFF000B */
#define INT1_VECTOR              2    /* 0xFF0013 */
#define TMR1_VECTOR              3    /* 0xFF001B */
#define UART1_VECTOR             4    /* 0xFF0023 */
#define ADC_VECTOR               5    /* 0xFF002B */
#define LVD_VECTOR               6    /* 0xFF0033 */
#define UART2_VECTOR             8    /* 0xFF0043 */
#define SPI_VECTOR               9    /* 0xFF004B */
#define INT2_VECTOR              10   /* 0xFF0053 */
#define INT3_VECTOR              11   /* 0xFF005B */
#define TMR2_VECTOR              12   /* 0xFF0063 */
#define INT4_VECTOR              16   /* 0xFF0083 */
#define UART3_VECTOR             17   /* 0xFF008B */
#define UART4_VECTOR             18   /* 0xFF0093 */
#define TMR3_VECTOR              19   /* 0xFF009B */
#define TMR4_VECTOR              20   /* 0xFF00A3 */
#define CMP_VECTOR               21   /* 0xFF00AB */
#define I2C_VECTOR               24   /* 0xFF00C3 */
#define USB_VECTOR               25   /* 0xFF00CB */
#define PWMA_VECTOR              26   /* 0xFF00D3 */
#define PWMB_VECTOR              27   /* 0xFF00DB */
#define CAN1_VECTOR              28   /* 0xFF00E3 */
#define CAN2_VECTOR              29   /* 0xFF00EB */
#define LIN1_VECTOR              30   /* 0xFF00F3 */
#define LIN2_VECTOR              31   /* 0xFF00FB */
#define RTC_VECTOR               36   /* 0xFF0123 */
#define P0INT_VECTOR             37   /* 0xFF012B */
#define P1INT_VECTOR             38   /* 0xFF0133 */
#define P2INT_VECTOR             39   /* 0xFF013B */
#define P3INT_VECTOR             40   /* 0xFF0143 */
#define P4INT_VECTOR             41   /* 0xFF014B */
#define P5INT_VECTOR             42   /* 0xFF0153 */
#define P6INT_VECTOR             43   /* 0xFF015B */
#define P7INT_VECTOR             44   /* 0xFF0163 */
#define P8INT_VECTOR             45   /* 0xFF016B */
#define P9INT_VECTOR             46   /* 0xFF0173 */
#define DMA_M2M_VECTOR           47   /* 0xFF017B */
#define DMA_ADC_VECTOR           48   /* 0xFF0183 */
#define DMA_SPI_VECTOR           49   /* 0xFF018B */
#define DMA_UR1T_VECTOR          50   /* 0xFF0193 */
#define DMA_UR1R_VECTOR          51   /* 0xFF019B */
#define DMA_UR2T_VECTOR          52   /* 0xFF01A3 */
#define DMA_UR2R_VECTOR          53   /* 0xFF01AB */
#define DMA_UR3T_VECTOR          54   /* 0xFF01B3 */
#define DMA_UR3R_VECTOR          55   /* 0xFF01BB */
#define DMA_UR4T_VECTOR          56   /* 0xFF01C3 */
#define DMA_UR4R_VECTOR          57   /* 0xFF01CB */
#define DMA_LCM_VECTOR           58   /* 0xFF01D3 */
#define LCM_VECTOR               59   /* 0xFF01DB */
#define DMA_I2CT_VECTOR          60   /* 0xFF01E3 */
#define DMA_I2CR_VECTOR          61   /* 0xFF01EB */
#define I2S_VECTOR               62   /* 0xFF01F3 */
#define DMA_I2ST_VECTOR          63   /* 0xFF01FB */
#define DMA_I2SR_VECTOR          64   /* 0xFF0203 */
#define DMA_QSPI_VECTOR          65   /* 0xFF020B */
#define QSPI_VECTOR              66   /* 0xFF0213 */
#define TMR11_VECTOR             67   /* 0xFF021B */
#define DMA_I2C2T_VECTOR         68   /* 0xFF0223 */
#define DMA_I2C2R_VECTOR         69   /* 0xFF022B */
#define DMA_I2S2T_VECTOR         70   /* 0xFF0233 */
#define DMA_I2S2R_VECTOR         71   /* 0xFF023B */
#define DMA_PWMAT_VECTOR         72   /* 0xFF0243 */
#define DMA_PWMAR_VECTOR         73   /* 0xFF024B */
#define DMA_PWMCT_VECTOR         74   /* 0xFF0253 */
#define DMA_PWMCR_VECTOR         75   /* 0xFF025B */
#define DMA_ADC2_VECTOR          76   /* 0xFF0263 */
#define DMA_DAC_VECTOR           77   /* 0xFF026B */
#define DMA_DAC2_VECTOR          78   /* 0xFF0273 */
#define DMA_SPI2_VECTOR          79   /* 0xFF027B */
#define DMA_SPI3_VECTOR          80   /* 0xFF0283 */
#define DMA_UR5T_VECTOR          82   /* 0xFF0293 */
#define DMA_UR5R_VECTOR          83   /* 0xFF029B */
#define DMA_UR6T_VECTOR          84   /* 0xFF02A3 */
#define DMA_UR6R_VECTOR          85   /* 0xFF02AB */
#define DMA_UR7T_VECTOR          86   /* 0xFF02B3 */
#define DMA_UR7R_VECTOR          87   /* 0xFF02BB */
#define DMA_UR8T_VECTOR          88   /* 0xFF02C3 */
#define DMA_UR8R_VECTOR          89   /* 0xFF02CB */
#define PAINT_VECTOR             90   /* 0xFF02D3 */
#define PBINT_VECTOR             91   /* 0xFF02DB */
#define TMR5_TMR6_VECTOR         96   /* 0xFF0303 */
#define TMR7_TMR8_VECTOR         97   /* 0xFF030B */
#define TMR9_TMR10_VECTOR        98   /* 0xFF0313 */
#define TMR17_TMR18_VECTOR       99   /* 0xFF031B */
#define UART5_VECTOR             102  /* 0xFF0333 */
#define UART6_VECTOR             103  /* 0xFF033B */
#define UART7_VECTOR             104  /* 0xFF0343 */
#define UART8_VECTOR             105  /* 0xFF034B */
#define ADC2_VECTOR              106  /* 0xFF0353 */
#define DAC_VECTOR               107  /* 0xFF035B */
#define DAC2_VECTOR              108  /* 0xFF0363 */
#define I2C2_VECTOR              109  /* 0xFF036B */
#define I2S2_VECTOR              110  /* 0xFF0373 */
#define SPI2_VECTOR              111  /* 0xFF037B */
#define SPI3_VECTOR              112  /* 0xFF0383 */
#define CMP2_VECTOR              114  /* 0xFF0393 */
#define CMP3_VECTOR              115  /* 0xFF039B */
#define CMP4_VECTOR              116  /* 0xFF03A3 */
#define DMA_CANT_VECTOR          117  /* 0xFF03AB */
#define DMA_CANR_VECTOR          118  /* 0xFF03B3 */
#define DMA_CAN2T_VECTOR         119  /* 0xFF03BB */
#define DMA_CAN2R_VECTOR         120  /* 0xFF03C3 */
#define PWMC_VECTOR              121  /* 0xFF03CB */
#define PWMD_VECTOR              122  /* 0xFF03D3 */
#define PWME_VECTOR              123  /* 0xFF03DB */
#define PWMF_VECTOR              124  /* 0xFF03E3 */
#define DMA_PWMET_VECTOR         125  /* 0xFF03EB */
#define DMA_PWMER_VECTOR         126  /* 0xFF03F3 */

#endif /* __STC32G144K246_H */
