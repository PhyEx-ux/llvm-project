/*===---- stc32g.h - STC32G (STC32G12K128) MCU register & vector definitions ---===*
 * STC32G（STC32G12K128 / 8K64 / 12K64）寄存器与中断向量定义头文件
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Target MCU / 目标芯片: STC32G12K128 / STC32G8K64 / STC32G12K64 series
 * Designed for LLVM/Clang MCS-251 (default v2 ELF memory contract).
 * 面向 LLVM/Clang MCS-251 工具链设计（默认 v2 ELF 内存契约）。
 *===-----------------------------------------------------------------------===*/

#ifndef __STC32G_H
#define __STC32G_H

#include <mcs251_intrins.h>

/*===----------------------------------------------------------------------===*
 * On-chip memory map constants (for linker flags & placement assertions)
 * 片上存储空间常量（供链接参数配置与静态地址断言使用）
 *===----------------------------------------------------------------------===*/
#ifndef STC32_FLASH_BASE
#define STC32_FLASH_BASE         0xFE0000UL     /* 128 KiB on-chip Flash base / 128KB 片上 Flash 基址 */
#endif
#ifndef STC32_FLASH_SIZE
#define STC32_FLASH_SIZE         0x020000UL     /* 128 KiB on-chip Flash size / 128KB 片上 Flash 容量 */
#endif
#ifndef STC32_IRAM_SIZE
#define STC32_IRAM_SIZE          0x0080U        /* 128 B direct internal RAM / 128B 直接寻址内部 RAM */
#endif
#ifndef STC32_EDATA_END
#define STC32_EDATA_END          0x0FFFU        /* 4 KiB EDATA upper bound / 4KB 扩展内部 RAM 上界 */
#endif
#ifndef STC32_XDATA_BASE
#define STC32_XDATA_BASE         0x010000UL     /* XDATA base address / 片上 XDATA 基址 */
#endif
#ifndef STC32_XDATA_SIZE
#define STC32_XDATA_SIZE         0x002000UL     /* 8 KiB on-chip XDATA / 8KB 片上 XDATA 容量 */
#endif

/*===----------------------------------------------------------------------===*
 * Direct-address SFRs (0x80..0xFE, 116 registers)
 * 直寻址特殊功能寄存器 SFR（0x80..0xFE，共 116 个）
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
#define V33TRIM                  MCS251_SFR(0xA3U)
#define BGTRIM                   MCS251_SFR(0xA5U)
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
#define USBCLK                   MCS251_SFR(0xDCU)
#define T4T3M                    MCS251_SFR(0xDDU)
#define ADCCFG                   MCS251_SFR(0xDEU)
#define IP3                      MCS251_SFR(0xDFU)
#define ACC                      MCS251_SFR(0xE0U)
#define P7M1                     MCS251_SFR(0xE1U)
#define P7M0                     MCS251_SFR(0xE2U)
#define DPS                      MCS251_SFR(0xE3U)
#define DPL1                     MCS251_SFR(0xE4U)
#define DPH1                     MCS251_SFR(0xE5U)
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
#define CANICR                   MCS251_SFR(0xF1U)
#define USBCON                   MCS251_SFR(0xF4U)
#define IAP_TPS                  MCS251_SFR(0xF5U)
#define IAP_ADDRE                MCS251_SFR(0xF6U)
#define ICHECR                   MCS251_SFR(0xF7U)
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
 * Hardware bit-addressable SFR bits (103 bits, C mode only)
 * 硬件可位寻址 SFR 位定义（共 103 个，仅 C 模式可用）
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
/* IP (0xB8) */
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
/* P3 (0xB0) */
#define RXD                      MCS251_SFR_BIT(0xB0U) /* P3.0 */
#define TXD                      MCS251_SFR_BIT(0xB1U) /* P3.1 */
#define INT0                     MCS251_SFR_BIT(0xB2U) /* P3.2 */
#define INT1                     MCS251_SFR_BIT(0xB3U) /* P3.3 */
#define T0                       MCS251_SFR_BIT(0xB4U) /* P3.4 */
#define T1                       MCS251_SFR_BIT(0xB5U) /* P3.5 */
#define WR                       MCS251_SFR_BIT(0xB6U) /* P3.6 */
#define RD                       MCS251_SFR_BIT(0xB7U) /* P3.7 */
#endif /* !__cplusplus */

/*===----------------------------------------------------------------------===*
 * Bitmask constants for non-bit-addressable control SFRs (176 masks)
 * 非位寻址控制 SFR 的具名位掩码常量（共 176 个）
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
/* AUXR (0x8E) */
#define AUXR_T0x12                   ((unsigned char)(1U << 7)) /* 0x80 */
#define AUXR_T1x12                   ((unsigned char)(1U << 6)) /* 0x40 */
#define AUXR_S1M0x6                  ((unsigned char)(1U << 5)) /* 0x20 */
#define AUXR_T2R                     ((unsigned char)(1U << 4)) /* 0x10 */
#define AUXR_T2CT                    ((unsigned char)(1U << 3)) /* 0x08 */
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
/* WDT_CONTR (0xC1) */
#define WDT_CONTR_WDT_FLAG           ((unsigned char)(1U << 7)) /* 0x80 */
#define WDT_CONTR_EN_WDT             ((unsigned char)(1U << 5)) /* 0x20 */
#define WDT_CONTR_CLR_WDT            ((unsigned char)(1U << 4)) /* 0x10 */
#define WDT_CONTR_IDL_WDT            ((unsigned char)(1U << 3)) /* 0x08 */
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
#define T4T3M_T4CT                   ((unsigned char)(1U << 6)) /* 0x40 */
#define T4T3M_T4x12                  ((unsigned char)(1U << 5)) /* 0x20 */
#define T4T3M_T4CLKO                 ((unsigned char)(1U << 4)) /* 0x10 */
#define T4T3M_T3R                    ((unsigned char)(1U << 3)) /* 0x08 */
#define T4T3M_T3CT                   ((unsigned char)(1U << 2)) /* 0x04 */
#define T4T3M_T3x12                  ((unsigned char)(1U << 1)) /* 0x02 */
#define T4T3M_T3CLKO                 ((unsigned char)(1U << 0)) /* 0x01 */
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
/* CANICR (0xF1) */
#define CANICR_PCAN2H                ((unsigned char)(1U << 7)) /* 0x80 */
#define CANICR_CAN2IF                ((unsigned char)(1U << 6)) /* 0x40 */
#define CANICR_CAN2IE                ((unsigned char)(1U << 5)) /* 0x20 */
#define CANICR_PCAN2L                ((unsigned char)(1U << 4)) /* 0x10 */
#define CANICR_PCANH                 ((unsigned char)(1U << 3)) /* 0x08 */
#define CANICR_CANIF                 ((unsigned char)(1U << 2)) /* 0x04 */
#define CANICR_CANIE                 ((unsigned char)(1U << 1)) /* 0x02 */
#define CANICR_PCANL                 ((unsigned char)(1U << 0)) /* 0x01 */
/* USBCON (0xF4) */
#define USBCON_ENUSB                 ((unsigned char)(1U << 7)) /* 0x80 */
#define USBCON_USBRST                ((unsigned char)(1U << 6)) /* 0x40 */
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
 * Extended SFRs (XFR, physical window 0x7E0000..0x7EFFFF, 418 registers)
 * 扩展功能寄存器 XFR（物理窗口 0x7E0000..0x7EFFFF，共 418 个）
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
#define IRC48ATRIM               MCS251_XFR(0x7EFE09UL)
#define IRC48BTRIM               MCS251_XFR(0x7EFE0AUL)
#define HSCLKDIV                 MCS251_XFR(0x7EFE0BUL)
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
#define LCMIFCFG                 MCS251_XFR(0x7EFE50UL)
#define LCMIFCFG2                MCS251_XFR(0x7EFE51UL)
#define LCMIFCR                  MCS251_XFR(0x7EFE52UL)
#define LCMIFSTA                 MCS251_XFR(0x7EFE53UL)
#define LCMIFDATL                MCS251_XFR(0x7EFE54UL)
#define LCMIFDATH                MCS251_XFR(0x7EFE55UL)
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
#define YEAR                     MCS251_XFR(0x7EFE70UL)
#define MONTH                    MCS251_XFR(0x7EFE71UL)
#define DAY                      MCS251_XFR(0x7EFE72UL)
#define HOUR                     MCS251_XFR(0x7EFE73UL)
#define MIN                      MCS251_XFR(0x7EFE74UL)
#define SEC                      MCS251_XFR(0x7EFE75UL)
#define SSEC                     MCS251_XFR(0x7EFE76UL)
#define I2CCFG                   MCS251_XFR(0x7EFE80UL)
#define I2CMSCR                  MCS251_XFR(0x7EFE81UL)
#define I2CMSST                  MCS251_XFR(0x7EFE82UL)
#define I2CSLCR                  MCS251_XFR(0x7EFE83UL)
#define I2CSLST                  MCS251_XFR(0x7EFE84UL)
#define I2CSLADR                 MCS251_XFR(0x7EFE85UL)
#define I2CTXD                   MCS251_XFR(0x7EFE86UL)
#define I2CRXD                   MCS251_XFR(0x7EFE87UL)
#define I2CMSAUX                 MCS251_XFR(0x7EFE88UL)
#define SPFUNC                   MCS251_XFR(0x7EFE98UL)
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
#define T3T4PS                   MCS251_XFR(0x7EFEACUL)
#define ADCEXCFG                 MCS251_XFR(0x7EFEADUL)
#define CMPEXCFG                 MCS251_XFR(0x7EFEAEUL)
#define PWMA_ETRPS               MCS251_XFR(0x7EFEB0UL)
#define PWMA_ENO                 MCS251_XFR(0x7EFEB1UL)
#define PWMA_PS                  MCS251_XFR(0x7EFEB2UL)
#define PWMA_IOAUX               MCS251_XFR(0x7EFEB3UL)
#define PWMB_ETRPS               MCS251_XFR(0x7EFEB4UL)
#define PWMB_ENO                 MCS251_XFR(0x7EFEB5UL)
#define PWMB_PS                  MCS251_XFR(0x7EFEB6UL)
#define PWMB_IOAUX               MCS251_XFR(0x7EFEB7UL)
#define CANAR                    MCS251_XFR(0x7EFEBBUL)
#define CANDR                    MCS251_XFR(0x7EFEBCUL)
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
#define PWMB_CCR1H               MCS251_XFR(0x7EFEF5UL)
#define PWMB_CCR1L               MCS251_XFR(0x7EFEF6UL)
#define PWMB_CCR2H               MCS251_XFR(0x7EFEF7UL)
#define PWMB_CCR2L               MCS251_XFR(0x7EFEF8UL)
#define PWMB_CCR3H               MCS251_XFR(0x7EFEF9UL)
#define PWMB_CCR3L               MCS251_XFR(0x7EFEFAUL)
#define PWMB_CCR4H               MCS251_XFR(0x7EFEFBUL)
#define PWMB_CCR4L               MCS251_XFR(0x7EFEFCUL)
#define PWMB_BKR                 MCS251_XFR(0x7EFEFDUL)
#define PWMB_DTR                 MCS251_XFR(0x7EFEFEUL)
#define PWMB_OISR                MCS251_XFR(0x7EFEFFUL)
#define PWM2_OISR                MCS251_XFR(0x7EFEFFUL)
#define P0INTE                   MCS251_XFR(0x7EFD00UL)
#define P1INTE                   MCS251_XFR(0x7EFD01UL)
#define P2INTE                   MCS251_XFR(0x7EFD02UL)
#define P3INTE                   MCS251_XFR(0x7EFD03UL)
#define P4INTE                   MCS251_XFR(0x7EFD04UL)
#define P5INTE                   MCS251_XFR(0x7EFD05UL)
#define P6INTE                   MCS251_XFR(0x7EFD06UL)
#define P7INTE                   MCS251_XFR(0x7EFD07UL)
#define P0INTF                   MCS251_XFR(0x7EFD10UL)
#define P1INTF                   MCS251_XFR(0x7EFD11UL)
#define P2INTF                   MCS251_XFR(0x7EFD12UL)
#define P3INTF                   MCS251_XFR(0x7EFD13UL)
#define P4INTF                   MCS251_XFR(0x7EFD14UL)
#define P5INTF                   MCS251_XFR(0x7EFD15UL)
#define P6INTF                   MCS251_XFR(0x7EFD16UL)
#define P7INTF                   MCS251_XFR(0x7EFD17UL)
#define P0IM0                    MCS251_XFR(0x7EFD20UL)
#define P1IM0                    MCS251_XFR(0x7EFD21UL)
#define P2IM0                    MCS251_XFR(0x7EFD22UL)
#define P3IM0                    MCS251_XFR(0x7EFD23UL)
#define P4IM0                    MCS251_XFR(0x7EFD24UL)
#define P5IM0                    MCS251_XFR(0x7EFD25UL)
#define P6IM0                    MCS251_XFR(0x7EFD26UL)
#define P7IM0                    MCS251_XFR(0x7EFD27UL)
#define P0IM1                    MCS251_XFR(0x7EFD30UL)
#define P1IM1                    MCS251_XFR(0x7EFD31UL)
#define P2IM1                    MCS251_XFR(0x7EFD32UL)
#define P3IM1                    MCS251_XFR(0x7EFD33UL)
#define P4IM1                    MCS251_XFR(0x7EFD34UL)
#define P5IM1                    MCS251_XFR(0x7EFD35UL)
#define P6IM1                    MCS251_XFR(0x7EFD36UL)
#define P7IM1                    MCS251_XFR(0x7EFD37UL)
#define P0WKUE                   MCS251_XFR(0x7EFD40UL)
#define P1WKUE                   MCS251_XFR(0x7EFD41UL)
#define P2WKUE                   MCS251_XFR(0x7EFD42UL)
#define P3WKUE                   MCS251_XFR(0x7EFD43UL)
#define P4WKUE                   MCS251_XFR(0x7EFD44UL)
#define P5WKUE                   MCS251_XFR(0x7EFD45UL)
#define P6WKUE                   MCS251_XFR(0x7EFD46UL)
#define P7WKUE                   MCS251_XFR(0x7EFD47UL)
#define PIN_IP                   MCS251_XFR(0x7EFD60UL)
#define PIN_IPH                  MCS251_XFR(0x7EFD61UL)
#define S2CFG                    MCS251_XFR(0x7EFDB4UL)
#define S2ADDR                   MCS251_XFR(0x7EFDB5UL)
#define S2ADEN                   MCS251_XFR(0x7EFDB6UL)
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
#define HSPWMA_CFG               MCS251_XFR(0x7EFBF0UL)
#define HSPWMA_ADR               MCS251_XFR(0x7EFBF1UL)
#define HSPWMA_DAT               MCS251_XFR(0x7EFBF2UL)
#define HSPWMB_CFG               MCS251_XFR(0x7EFBF4UL)
#define HSPWMB_ADR               MCS251_XFR(0x7EFBF5UL)
#define HSPWMB_DAT               MCS251_XFR(0x7EFBF6UL)
#define HSSPI_CFG                MCS251_XFR(0x7EFBF8UL)
#define HSSPI_CFG2               MCS251_XFR(0x7EFBF9UL)
#define HSSPI_STA                MCS251_XFR(0x7EFBFAUL)
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
#define DMA_ADC_RXAH             MCS251_XFR(0x7EFA17UL)
#define DMA_ADC_RXAL             MCS251_XFR(0x7EFA18UL)
#define DMA_ADC_CFG2             MCS251_XFR(0x7EFA19UL)
#define DMA_ADC_CHSW0            MCS251_XFR(0x7EFA1AUL)
#define DMA_ADC_CHSW1            MCS251_XFR(0x7EFA1BUL)
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
#define DMA_M2M_AMTH             MCS251_XFR(0x7EFA80UL)
#define DMA_M2M_DONEH            MCS251_XFR(0x7EFA81UL)
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

/*===----------------------------------------------------------------------===*
 * Legal Interrupt Vector Numbers (51 vectors)
 * 合法硬件中断向量号表（共 51 个）
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

#endif /* __STC32G_H */
