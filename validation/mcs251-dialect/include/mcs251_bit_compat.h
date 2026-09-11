/*===---- mcs251_bit_compat.h - MCS-251 Keil bit-name compatibility ----===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions. See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/

//===----------------------------------------------------------------------===//
// BT06 thin compatibility header.
//
// GENERATED - DO NOT EDIT BY HAND.  Authoritative data:
//   validation/mcs251-dialect/bit-registers.json
// produced by tools/gen-bit-registers.py.  Regenerate this header with:
//   tools/gen-bit-compat-header.py
// In-tree consistency is checked by:
//   tools/gen-bit-registers.py --check-header include/mcs251_bit_compat.h
//
// Scope (BIT-TASK-BREAKDOWN.md BT06): expose only the auditable first safe
// bit-name set through the target builtin __builtin_mcs251_bit_lvalue(ICE).
// Compiler-managed register bits and RSTCFG/XFR control bits hard-error.
// Names whose base SFR is not bit-addressable hard-error too: no hardware
// bit address exists and byte-mask emulation is forbidden, so the official
// header's declaration is answered with an explicit rejection (BT06-1),
// never a silent drop.
// Every remaining official sbit is converted: the BT06-1 reclassification
// moved the peripheral control bits (TCON/SCON/IE/IP and the P3
// alternate-function aliases) into the converted set, so no
// not-in-first-slice bucket remains -- each official name is either
// mapped here or explicitly rejected (no silent downgrade;
// BIT-DECISION-20260911 P09).
//
// This header does NOT define a bare `bit`, `sfr`, or `sbit` keyword and
// does NOT emulate a bit as u8/_Bool or as a byte mask.  Keil `sbit`
// declarations are handled by the -fmcs251-keil frontend, not here.
//
// Approval status: the first safe SFR list is PM's call under
// BIT-DECISION-20260911 P10.  The names below are a CANDIDATE batch (68
// first-slice + 35 BT06-1 reclassified), not an approval.
//===----------------------------------------------------------------------===//

#ifndef __MCS251_BIT_COMPAT_H__
#define __MCS251_BIT_COMPAT_H__

// Internal-RAM bits have no Keil name.  b must be an integer constant
// expression in 0..127; the range is enforced at compile time: the
// char[...] operand is an ill-formed negative array size for any b
// outside 0..127 (a _Static_assert cannot appear in this expression
// context).  No addressable pointer is ever produced here.  Fixed-RAM
// backing-byte ownership is BT07 scope, not BT06.
#define MCS251_RAM_BIT(b) \
    __builtin_mcs251_bit_lvalue( \
        (b) + 0 * sizeof(char[(b) >= 0 && (b) <= 127 ? 1 : -1]))

// Reject helpers.  Each rejected name expands to one of these; using it
// anywhere is a hard compile-time error with a specific message.
#define MCS251_error_compiler_managed \
    _Pragma("GCC error \"MCS251: compiler-managed register bit is not available through the ordinary interface\"") MCS251_error_compiler_managed_marker
#define MCS251_error_non_bit_addressable \
    _Pragma("GCC error \"MCS251: base SFR is not bit-addressable, no hardware bit address exists and no byte-mask emulation is provided\"") MCS251_error_non_bit_addressable_marker
#define MCS251_error_rstcfg_restricted \
    _Pragma("GCC error \"MCS251: RSTCFG-related bit is forbidden (no bit-addressable base)\"") MCS251_error_rstcfg_restricted_marker
#define MCS251_error_xfr_control \
    _Pragma("GCC error \"MCS251: XFR-enable control bit is not available; enable XFR explicitly, do not access it as an ordinary bit\"") MCS251_error_xfr_control_marker

//===----------------------------------------------------------------------===//
// Converted candidate batch (103 names): GPIO P0..P7, T0/T1
// and UART1 control/status, IE/IP interrupt bits, and the P3
// alternate-function aliases (BT06-1 reclassification).  Mapped to the
// fixed-bit-address builtin.
//===----------------------------------------------------------------------===//
#define EADC   __builtin_mcs251_bit_lvalue(0xAD)
#define ELVD   __builtin_mcs251_bit_lvalue(0xAE)
#define ES     __builtin_mcs251_bit_lvalue(0xAC)
#define ET0    __builtin_mcs251_bit_lvalue(0xA9)
#define ET1    __builtin_mcs251_bit_lvalue(0xAB)
#define EX0    __builtin_mcs251_bit_lvalue(0xA8)
#define EX1    __builtin_mcs251_bit_lvalue(0xAA)
#define IE0    __builtin_mcs251_bit_lvalue(0x89)
#define IE1    __builtin_mcs251_bit_lvalue(0x8B)
#define INT0   __builtin_mcs251_bit_lvalue(0xB2)
#define INT1   __builtin_mcs251_bit_lvalue(0xB3)
#define IT0    __builtin_mcs251_bit_lvalue(0x88)
#define IT1    __builtin_mcs251_bit_lvalue(0x8A)
#define P00    __builtin_mcs251_bit_lvalue(0x80)
#define P01    __builtin_mcs251_bit_lvalue(0x81)
#define P02    __builtin_mcs251_bit_lvalue(0x82)
#define P03    __builtin_mcs251_bit_lvalue(0x83)
#define P04    __builtin_mcs251_bit_lvalue(0x84)
#define P05    __builtin_mcs251_bit_lvalue(0x85)
#define P06    __builtin_mcs251_bit_lvalue(0x86)
#define P07    __builtin_mcs251_bit_lvalue(0x87)
#define P10    __builtin_mcs251_bit_lvalue(0x90)
#define P11    __builtin_mcs251_bit_lvalue(0x91)
#define P12    __builtin_mcs251_bit_lvalue(0x92)
#define P13    __builtin_mcs251_bit_lvalue(0x93)
#define P14    __builtin_mcs251_bit_lvalue(0x94)
#define P15    __builtin_mcs251_bit_lvalue(0x95)
#define P16    __builtin_mcs251_bit_lvalue(0x96)
#define P17    __builtin_mcs251_bit_lvalue(0x97)
#define P20    __builtin_mcs251_bit_lvalue(0xA0)
#define P21    __builtin_mcs251_bit_lvalue(0xA1)
#define P22    __builtin_mcs251_bit_lvalue(0xA2)
#define P23    __builtin_mcs251_bit_lvalue(0xA3)
#define P24    __builtin_mcs251_bit_lvalue(0xA4)
#define P25    __builtin_mcs251_bit_lvalue(0xA5)
#define P26    __builtin_mcs251_bit_lvalue(0xA6)
#define P27    __builtin_mcs251_bit_lvalue(0xA7)
#define P30    __builtin_mcs251_bit_lvalue(0xB0)
#define P31    __builtin_mcs251_bit_lvalue(0xB1)
#define P32    __builtin_mcs251_bit_lvalue(0xB2)
#define P33    __builtin_mcs251_bit_lvalue(0xB3)
#define P34    __builtin_mcs251_bit_lvalue(0xB4)
#define P35    __builtin_mcs251_bit_lvalue(0xB5)
#define P36    __builtin_mcs251_bit_lvalue(0xB6)
#define P37    __builtin_mcs251_bit_lvalue(0xB7)
#define P40    __builtin_mcs251_bit_lvalue(0xC0)
#define P41    __builtin_mcs251_bit_lvalue(0xC1)
#define P42    __builtin_mcs251_bit_lvalue(0xC2)
#define P43    __builtin_mcs251_bit_lvalue(0xC3)
#define P44    __builtin_mcs251_bit_lvalue(0xC4)
#define P45    __builtin_mcs251_bit_lvalue(0xC5)
#define P46    __builtin_mcs251_bit_lvalue(0xC6)
#define P47    __builtin_mcs251_bit_lvalue(0xC7)
#define P50    __builtin_mcs251_bit_lvalue(0xC8)
#define P51    __builtin_mcs251_bit_lvalue(0xC9)
#define P52    __builtin_mcs251_bit_lvalue(0xCA)
#define P53    __builtin_mcs251_bit_lvalue(0xCB)
#define P54    __builtin_mcs251_bit_lvalue(0xCC)
#define P55    __builtin_mcs251_bit_lvalue(0xCD)
#define P56    __builtin_mcs251_bit_lvalue(0xCE)
#define P57    __builtin_mcs251_bit_lvalue(0xCF)
#define P60    __builtin_mcs251_bit_lvalue(0xE8)
#define P61    __builtin_mcs251_bit_lvalue(0xE9)
#define P62    __builtin_mcs251_bit_lvalue(0xEA)
#define P63    __builtin_mcs251_bit_lvalue(0xEB)
#define P64    __builtin_mcs251_bit_lvalue(0xEC)
#define P65    __builtin_mcs251_bit_lvalue(0xED)
#define P66    __builtin_mcs251_bit_lvalue(0xEE)
#define P67    __builtin_mcs251_bit_lvalue(0xEF)
#define P70    __builtin_mcs251_bit_lvalue(0xF8)
#define P71    __builtin_mcs251_bit_lvalue(0xF9)
#define P72    __builtin_mcs251_bit_lvalue(0xFA)
#define P73    __builtin_mcs251_bit_lvalue(0xFB)
#define P74    __builtin_mcs251_bit_lvalue(0xFC)
#define P75    __builtin_mcs251_bit_lvalue(0xFD)
#define P76    __builtin_mcs251_bit_lvalue(0xFE)
#define P77    __builtin_mcs251_bit_lvalue(0xFF)
#define PADC   __builtin_mcs251_bit_lvalue(0xBD)
#define PLVD   __builtin_mcs251_bit_lvalue(0xBE)
#define PPCA   __builtin_mcs251_bit_lvalue(0xBF)
#define PS     __builtin_mcs251_bit_lvalue(0xBC)
#define PT0    __builtin_mcs251_bit_lvalue(0xB9)
#define PT1    __builtin_mcs251_bit_lvalue(0xBB)
#define PX0    __builtin_mcs251_bit_lvalue(0xB8)
#define PX1    __builtin_mcs251_bit_lvalue(0xBA)
#define RB8    __builtin_mcs251_bit_lvalue(0x9A)
#define RD     __builtin_mcs251_bit_lvalue(0xB7)
#define REN    __builtin_mcs251_bit_lvalue(0x9C)
#define RI     __builtin_mcs251_bit_lvalue(0x98)
#define RXD    __builtin_mcs251_bit_lvalue(0xB0)
#define SM0    __builtin_mcs251_bit_lvalue(0x9F)
#define SM1    __builtin_mcs251_bit_lvalue(0x9E)
#define SM2    __builtin_mcs251_bit_lvalue(0x9D)
#define T0     __builtin_mcs251_bit_lvalue(0xB4)
#define T1     __builtin_mcs251_bit_lvalue(0xB5)
#define TB8    __builtin_mcs251_bit_lvalue(0x9B)
#define TF0    __builtin_mcs251_bit_lvalue(0x8D)
#define TF1    __builtin_mcs251_bit_lvalue(0x8F)
#define TI     __builtin_mcs251_bit_lvalue(0x99)
#define TR0    __builtin_mcs251_bit_lvalue(0x8C)
#define TR1    __builtin_mcs251_bit_lvalue(0x8E)
#define TXD    __builtin_mcs251_bit_lvalue(0xB1)
#define WR     __builtin_mcs251_bit_lvalue(0xB6)

//===----------------------------------------------------------------------===//
// Restricted (30 names): any use is a hard error.
//===----------------------------------------------------------------------===//
#define AC     MCS251_error_compiler_managed
#define ACC0   MCS251_error_compiler_managed
#define ACC1   MCS251_error_compiler_managed
#define ACC2   MCS251_error_compiler_managed
#define ACC3   MCS251_error_compiler_managed
#define ACC4   MCS251_error_compiler_managed
#define ACC5   MCS251_error_compiler_managed
#define ACC6   MCS251_error_compiler_managed
#define ACC7   MCS251_error_compiler_managed
#define B0     MCS251_error_compiler_managed
#define B1     MCS251_error_compiler_managed
#define B2     MCS251_error_compiler_managed
#define B3     MCS251_error_compiler_managed
#define B4     MCS251_error_compiler_managed
#define B5     MCS251_error_compiler_managed
#define B6     MCS251_error_compiler_managed
#define B7     MCS251_error_compiler_managed
#define CY     MCS251_error_compiler_managed
#define EA     MCS251_error_compiler_managed
#define EAXFR  MCS251_error_xfr_control
#define ENLVR  MCS251_error_rstcfg_restricted
#define F0     MCS251_error_compiler_managed
#define F1     MCS251_error_compiler_managed
#define N      MCS251_error_compiler_managed
#define OV     MCS251_error_compiler_managed
#define P      MCS251_error_compiler_managed
#define P54RST MCS251_error_rstcfg_restricted
#define RS0    MCS251_error_compiler_managed
#define RS1    MCS251_error_compiler_managed
#define Z      MCS251_error_compiler_managed

//===----------------------------------------------------------------------===//
// Rejected: non-bit-addressable base (215 names), the
// declaration from the official header is answered with an explicit
// rejection.  The base SFR address is not a multiple of 8, so no
// hardware bit address exists and byte-mask emulation is forbidden.
//===----------------------------------------------------------------------===//
// ADC_EPWMT: ADC_CONTR^4 -- base ADC_CONTR (0xBC) is not bit-addressable
#define ADC_EPWMT MCS251_error_non_bit_addressable
// ADC_FLAG: ADC_CONTR^5 -- base ADC_CONTR (0xBC) is not bit-addressable
#define ADC_FLAG MCS251_error_non_bit_addressable
// ADC_POWER: ADC_CONTR^7 -- base ADC_CONTR (0xBC) is not bit-addressable
#define ADC_POWER MCS251_error_non_bit_addressable
// ADC_START: ADC_CONTR^6 -- base ADC_CONTR (0xBC) is not bit-addressable
#define ADC_START MCS251_error_non_bit_addressable
// CAN2EN: AUXR2^2 -- base AUXR2 (0x97) is not bit-addressable
#define CAN2EN MCS251_error_non_bit_addressable
// CAN2IE: CANICR^5 -- base CANICR (0xF1) is not bit-addressable
#define CAN2IE MCS251_error_non_bit_addressable
// CAN2IF: CANICR^6 -- base CANICR (0xF1) is not bit-addressable
#define CAN2IF MCS251_error_non_bit_addressable
// CAN2_S0: P_SW3^0 -- base P_SW3 (0xBB) is not bit-addressable
#define CAN2_S0 MCS251_error_non_bit_addressable
// CAN2_S1: P_SW3^1 -- base P_SW3 (0xBB) is not bit-addressable
#define CAN2_S1 MCS251_error_non_bit_addressable
// CANEDIN: AUXR2^3 -- base AUXR2 (0x97) is not bit-addressable
#define CANEDIN MCS251_error_non_bit_addressable
// CANEN: AUXR2^1 -- base AUXR2 (0x97) is not bit-addressable
#define CANEN  MCS251_error_non_bit_addressable
// CANIE: CANICR^1 -- base CANICR (0xF1) is not bit-addressable
#define CANIE  MCS251_error_non_bit_addressable
// CANIF: CANICR^2 -- base CANICR (0xF1) is not bit-addressable
#define CANIF  MCS251_error_non_bit_addressable
// CANSEL: AUXR2^3 -- base AUXR2 (0x97) is not bit-addressable
#define CANSEL MCS251_error_non_bit_addressable
// CAN_S0: P_SW1^4 -- base P_SW1 (0xA2) is not bit-addressable
#define CAN_S0 MCS251_error_non_bit_addressable
// CAN_S1: P_SW1^5 -- base P_SW1 (0xA2) is not bit-addressable
#define CAN_S1 MCS251_error_non_bit_addressable
// CLR_WDT: WDT_CONTR^4 -- base WDT_CONTR (0xC1) is not bit-addressable
#define CLR_WDT MCS251_error_non_bit_addressable
// CMD_FAIL: IAP_CONTR^4 -- base IAP_CONTR (0xC7) is not bit-addressable
#define CMD_FAIL MCS251_error_non_bit_addressable
// CMPEN: CMPCR1^7 -- base CMPCR1 (0xE6) is not bit-addressable
#define CMPEN  MCS251_error_non_bit_addressable
// CMPIF: CMPCR1^6 -- base CMPCR1 (0xE6) is not bit-addressable
#define CMPIF  MCS251_error_non_bit_addressable
// CMPOE: CMPCR1^1 -- base CMPCR1 (0xE6) is not bit-addressable
#define CMPOE  MCS251_error_non_bit_addressable
// CMPO_S: P_SW2^3 -- base P_SW2 (0xBA) is not bit-addressable
#define CMPO_S MCS251_error_non_bit_addressable
// CMPRES: CMPCR1^0 -- base CMPCR1 (0xE6) is not bit-addressable
#define CMPRES MCS251_error_non_bit_addressable
// CPHA: SPCTL^2 -- base SPCTL (0xCE) is not bit-addressable
#define CPHA   MCS251_error_non_bit_addressable
// CPOL: SPCTL^3 -- base SPCTL (0xCE) is not bit-addressable
#define CPOL   MCS251_error_non_bit_addressable
// CPUMODE: AUXR2^6 -- base AUXR2 (0x97) is not bit-addressable
#define CPUMODE MCS251_error_non_bit_addressable
// DFREC: USBCON^2 -- base USBCON (0xF4) is not bit-addressable
#define DFREC  MCS251_error_non_bit_addressable
// DISFLT: CMPCR2^6 -- base CMPCR2 (0xE7) is not bit-addressable
#define DISFLT MCS251_error_non_bit_addressable
// DM: USBCON^0 -- base USBCON (0xF4) is not bit-addressable
#define DM     MCS251_error_non_bit_addressable
// DORD: SPCTL^5 -- base SPCTL (0xCE) is not bit-addressable
#define DORD   MCS251_error_non_bit_addressable
// DP: USBCON^1 -- base USBCON (0xF4) is not bit-addressable
#define DP     MCS251_error_non_bit_addressable
// ENUSB: USBCON^7 -- base USBCON (0xF4) is not bit-addressable
#define ENUSB  MCS251_error_non_bit_addressable
// ENUSBRST: USBCON^6 -- base USBCON (0xF4) is not bit-addressable
#define ENUSBRST MCS251_error_non_bit_addressable
// EN_WDT: WDT_CONTR^5 -- base WDT_CONTR (0xC1) is not bit-addressable
#define EN_WDT MCS251_error_non_bit_addressable
// ES2: IE2^0 -- base IE2 (0xAF) is not bit-addressable
#define ES2    MCS251_error_non_bit_addressable
// ES3: IE2^3 -- base IE2 (0xAF) is not bit-addressable
#define ES3    MCS251_error_non_bit_addressable
// ES4: IE2^4 -- base IE2 (0xAF) is not bit-addressable
#define ES4    MCS251_error_non_bit_addressable
// ESPI: IE2^1 -- base IE2 (0xAF) is not bit-addressable
#define ESPI   MCS251_error_non_bit_addressable
// ET2: IE2^2 -- base IE2 (0xAF) is not bit-addressable
#define ET2    MCS251_error_non_bit_addressable
// ET3: IE2^5 -- base IE2 (0xAF) is not bit-addressable
#define ET3    MCS251_error_non_bit_addressable
// ET4: IE2^6 -- base IE2 (0xAF) is not bit-addressable
#define ET4    MCS251_error_non_bit_addressable
// EUSB: IE2^7 -- base IE2 (0xAF) is not bit-addressable
#define EUSB   MCS251_error_non_bit_addressable
// EX2: INTCLKO^4 -- base INTCLKO (0x8F) is not bit-addressable
#define EX2    MCS251_error_non_bit_addressable
// EX3: INTCLKO^5 -- base INTCLKO (0x8F) is not bit-addressable
#define EX3    MCS251_error_non_bit_addressable
// EX4: INTCLKO^6 -- base INTCLKO (0x8F) is not bit-addressable
#define EX4    MCS251_error_non_bit_addressable
// EXTRAM: AUXR^1 -- base AUXR (0x8E) is not bit-addressable
#define EXTRAM MCS251_error_non_bit_addressable
// GF0: PCON^2 -- base PCON (0x87) is not bit-addressable
#define GF0    MCS251_error_non_bit_addressable
// GF1: PCON^3 -- base PCON (0x87) is not bit-addressable
#define GF1    MCS251_error_non_bit_addressable
// HIRCSEL0: IRCBAND^0 -- base IRCBAND (0x9D) is not bit-addressable
#define HIRCSEL0 MCS251_error_non_bit_addressable
// HIRCSEL1: IRCBAND^1 -- base IRCBAND (0x9D) is not bit-addressable
#define HIRCSEL1 MCS251_error_non_bit_addressable
// I2C_S0: P_SW2^4 -- base P_SW2 (0xBA) is not bit-addressable
#define I2C_S0 MCS251_error_non_bit_addressable
// I2C_S1: P_SW2^5 -- base P_SW2 (0xBA) is not bit-addressable
#define I2C_S1 MCS251_error_non_bit_addressable
// I2S_S0: P_SW3^6 -- base P_SW3 (0xBB) is not bit-addressable
#define I2S_S0 MCS251_error_non_bit_addressable
// I2S_S1: P_SW3^7 -- base P_SW3 (0xBB) is not bit-addressable
#define I2S_S1 MCS251_error_non_bit_addressable
// IAPEN: IAP_CONTR^7 -- base IAP_CONTR (0xC7) is not bit-addressable
#define IAPEN  MCS251_error_non_bit_addressable
// IDL: PCON^0 -- base PCON (0x87) is not bit-addressable
#define IDL    MCS251_error_non_bit_addressable
// IDL_WDT: WDT_CONTR^3 -- base WDT_CONTR (0xC1) is not bit-addressable
#define IDL_WDT MCS251_error_non_bit_addressable
// INT2IF: AUXINTIF^4 -- base AUXINTIF (0xEF) is not bit-addressable
#define INT2IF MCS251_error_non_bit_addressable
// INT3IF: AUXINTIF^5 -- base AUXINTIF (0xEF) is not bit-addressable
#define INT3IF MCS251_error_non_bit_addressable
// INT4IF: AUXINTIF^6 -- base AUXINTIF (0xEF) is not bit-addressable
#define INT4IF MCS251_error_non_bit_addressable
// INVCMPO: CMPCR2^7 -- base CMPCR2 (0xE7) is not bit-addressable
#define INVCMPO MCS251_error_non_bit_addressable
// LIN2EN: AUXR2^4 -- base AUXR2 (0x97) is not bit-addressable
#define LIN2EN MCS251_error_non_bit_addressable
// LINEN: AUXR2^0 -- base AUXR2 (0x97) is not bit-addressable
#define LINEN  MCS251_error_non_bit_addressable
// LINIE: LINICR^1 -- base LINICR (0xF9) is not bit-addressable
#define LINIE  MCS251_error_non_bit_addressable
// LINIF: LINICR^2 -- base LINICR (0xF9) is not bit-addressable
#define LINIF  MCS251_error_non_bit_addressable
// LINSEL: AUXR2^5 -- base AUXR2 (0x97) is not bit-addressable
#define LINSEL MCS251_error_non_bit_addressable
// LIN_S0: P_SW1^0 -- base P_SW1 (0xA2) is not bit-addressable
#define LIN_S0 MCS251_error_non_bit_addressable
// LIN_S1: P_SW1^1 -- base P_SW1 (0xA2) is not bit-addressable
#define LIN_S1 MCS251_error_non_bit_addressable
// LVDF: PCON^5 -- base PCON (0x87) is not bit-addressable
#define LVDF   MCS251_error_non_bit_addressable
// MSTR: SPCTL^4 -- base SPCTL (0xCE) is not bit-addressable
#define MSTR   MCS251_error_non_bit_addressable
// NIE: CMPCR1^4 -- base CMPCR1 (0xE6) is not bit-addressable
#define NIE    MCS251_error_non_bit_addressable
// PADCH: IPH^5 -- base IPH (0xB7) is not bit-addressable
#define PADCH  MCS251_error_non_bit_addressable
// PCAN2H: CANICR^7 -- base CANICR (0xF1) is not bit-addressable
#define PCAN2H MCS251_error_non_bit_addressable
// PCAN2L: CANICR^4 -- base CANICR (0xF1) is not bit-addressable
#define PCAN2L MCS251_error_non_bit_addressable
// PCANH: CANICR^3 -- base CANICR (0xF1) is not bit-addressable
#define PCANH  MCS251_error_non_bit_addressable
// PCANL: CANICR^0 -- base CANICR (0xF1) is not bit-addressable
#define PCANL  MCS251_error_non_bit_addressable
// PCMP: IP2^5 -- base IP2 (0xB5) is not bit-addressable
#define PCMP   MCS251_error_non_bit_addressable
// PCMPH: IP2H^5 -- base IP2H (0xB6) is not bit-addressable
#define PCMPH  MCS251_error_non_bit_addressable
// PD: PCON^1 -- base PCON (0x87) is not bit-addressable
#define PD     MCS251_error_non_bit_addressable
// PDEN: USBCON^3 -- base USBCON (0xF4) is not bit-addressable
#define PDEN   MCS251_error_non_bit_addressable
// PI2C: IP2^6 -- base IP2 (0xB5) is not bit-addressable
#define PI2C   MCS251_error_non_bit_addressable
// PI2C2: IP3^5 -- base IP3 (0xDF) is not bit-addressable
#define PI2C2  MCS251_error_non_bit_addressable
// PI2C2H: IP3H^5 -- base IP3H (0xEE) is not bit-addressable
#define PI2C2H MCS251_error_non_bit_addressable
// PI2CH: IP2H^6 -- base IP2H (0xB6) is not bit-addressable
#define PI2CH  MCS251_error_non_bit_addressable
// PI2S: IP3^3 -- base IP3 (0xDF) is not bit-addressable
#define PI2S   MCS251_error_non_bit_addressable
// PI2S2: IP3^4 -- base IP3 (0xDF) is not bit-addressable
#define PI2S2  MCS251_error_non_bit_addressable
// PI2S2H: IP3H^4 -- base IP3H (0xEE) is not bit-addressable
#define PI2S2H MCS251_error_non_bit_addressable
// PI2SH: IP3H^3 -- base IP3H (0xEE) is not bit-addressable
#define PI2SH  MCS251_error_non_bit_addressable
// PIE: CMPCR1^5 -- base CMPCR1 (0xE6) is not bit-addressable
#define PIE    MCS251_error_non_bit_addressable
// PLINH: LINICR^3 -- base LINICR (0xF9) is not bit-addressable
#define PLINH  MCS251_error_non_bit_addressable
// PLINL: LINICR^0 -- base LINICR (0xF9) is not bit-addressable
#define PLINL  MCS251_error_non_bit_addressable
// PLVDH: IPH^6 -- base IPH (0xB7) is not bit-addressable
#define PLVDH  MCS251_error_non_bit_addressable
// POF: PCON^4 -- base PCON (0x87) is not bit-addressable
#define POF    MCS251_error_non_bit_addressable
// PPCAH: IPH^7 -- base IPH (0xB7) is not bit-addressable
#define PPCAH  MCS251_error_non_bit_addressable
// PPWMA: IP2^2 -- base IP2 (0xB5) is not bit-addressable
#define PPWMA  MCS251_error_non_bit_addressable
// PPWMAH: IP2H^2 -- base IP2H (0xB6) is not bit-addressable
#define PPWMAH MCS251_error_non_bit_addressable
// PPWMB: IP2^3 -- base IP2 (0xB5) is not bit-addressable
#define PPWMB  MCS251_error_non_bit_addressable
// PPWMBH: IP2H^3 -- base IP2H (0xB6) is not bit-addressable
#define PPWMBH MCS251_error_non_bit_addressable
// PPWMC: IP3^6 -- base IP3 (0xDF) is not bit-addressable
#define PPWMC  MCS251_error_non_bit_addressable
// PPWMCH: IP3H^6 -- base IP3H (0xEE) is not bit-addressable
#define PPWMCH MCS251_error_non_bit_addressable
// PPWMD: IP3^7 -- base IP3 (0xDF) is not bit-addressable
#define PPWMD  MCS251_error_non_bit_addressable
// PPWMDH: IP3H^7 -- base IP3H (0xEE) is not bit-addressable
#define PPWMDH MCS251_error_non_bit_addressable
// PPWME: IP4^0 -- base IP4 (0xF2) is not bit-addressable
#define PPWME  MCS251_error_non_bit_addressable
// PPWMEH: IP4H^0 -- base IP4H (0xF3) is not bit-addressable
#define PPWMEH MCS251_error_non_bit_addressable
// PPWMF: IP4^1 -- base IP4 (0xF2) is not bit-addressable
#define PPWMF  MCS251_error_non_bit_addressable
// PPWMFH: IP4H^1 -- base IP4H (0xF3) is not bit-addressable
#define PPWMFH MCS251_error_non_bit_addressable
// PRTC: IP3^2 -- base IP3 (0xDF) is not bit-addressable
#define PRTC   MCS251_error_non_bit_addressable
// PRTCH: IP3H^2 -- base IP3H (0xEE) is not bit-addressable
#define PRTCH  MCS251_error_non_bit_addressable
// PS2: IP2^0 -- base IP2 (0xB5) is not bit-addressable
#define PS2    MCS251_error_non_bit_addressable
// PS2H: IP2H^0 -- base IP2H (0xB6) is not bit-addressable
#define PS2H   MCS251_error_non_bit_addressable
// PS2M: USBCON^5 -- base USBCON (0xF4) is not bit-addressable
#define PS2M   MCS251_error_non_bit_addressable
// PS3: IP3^0 -- base IP3 (0xDF) is not bit-addressable
#define PS3    MCS251_error_non_bit_addressable
// PS3H: IP3H^0 -- base IP3H (0xEE) is not bit-addressable
#define PS3H   MCS251_error_non_bit_addressable
// PS4: IP3^1 -- base IP3 (0xDF) is not bit-addressable
#define PS4    MCS251_error_non_bit_addressable
// PS4H: IP3H^1 -- base IP3H (0xEE) is not bit-addressable
#define PS4H   MCS251_error_non_bit_addressable
// PSH: IPH^4 -- base IPH (0xB7) is not bit-addressable
#define PSH    MCS251_error_non_bit_addressable
// PSPI: IP2^1 -- base IP2 (0xB5) is not bit-addressable
#define PSPI   MCS251_error_non_bit_addressable
// PSPIH: IP2H^1 -- base IP2H (0xB6) is not bit-addressable
#define PSPIH  MCS251_error_non_bit_addressable
// PT0H: IPH^1 -- base IPH (0xB7) is not bit-addressable
#define PT0H   MCS251_error_non_bit_addressable
// PT1H: IPH^3 -- base IPH (0xB7) is not bit-addressable
#define PT1H   MCS251_error_non_bit_addressable
// PUEN: USBCON^4 -- base USBCON (0xF4) is not bit-addressable
#define PUEN   MCS251_error_non_bit_addressable
// PUSB: IP2^7 -- base IP2 (0xB5) is not bit-addressable
#define PUSB   MCS251_error_non_bit_addressable
// PUSBH: IP2H^7 -- base IP2H (0xB6) is not bit-addressable
#define PUSBH  MCS251_error_non_bit_addressable
// PX0H: IPH^0 -- base IPH (0xB7) is not bit-addressable
#define PX0H   MCS251_error_non_bit_addressable
// PX1H: IPH^2 -- base IPH (0xB7) is not bit-addressable
#define PX1H   MCS251_error_non_bit_addressable
// PX4: IP2^4 -- base IP2 (0xB5) is not bit-addressable
#define PX4    MCS251_error_non_bit_addressable
// PX4H: IP2H^4 -- base IP2H (0xB6) is not bit-addressable
#define PX4H   MCS251_error_non_bit_addressable
// QSPI_S0: P_SW4^0 -- base P_SW4 (0xBF) is not bit-addressable
#define QSPI_S0 MCS251_error_non_bit_addressable
// QSPI_S1: P_SW4^1 -- base P_SW4 (0xBF) is not bit-addressable
#define QSPI_S1 MCS251_error_non_bit_addressable
// RAMEXE: CKCON^7 -- base CKCON (0xEA) is not bit-addressable
#define RAMEXE MCS251_error_non_bit_addressable
// RAMTINY: AUXR2^7 -- base AUXR2 (0x97) is not bit-addressable
#define RAMTINY MCS251_error_non_bit_addressable
// RESFMT: ADCCFG^5 -- base ADCCFG (0xDE) is not bit-addressable
#define RESFMT MCS251_error_non_bit_addressable
// S1BRT: AUXR^0 -- base AUXR (0x8E) is not bit-addressable
#define S1BRT  MCS251_error_non_bit_addressable
// S1M0x6: AUXR^5 -- base AUXR (0x8E) is not bit-addressable
#define S1M0x6 MCS251_error_non_bit_addressable
// S1SPI_S0: P_SW3^2 -- base P_SW3 (0xBB) is not bit-addressable
#define S1SPI_S0 MCS251_error_non_bit_addressable
// S1SPI_S1: P_SW3^3 -- base P_SW3 (0xBB) is not bit-addressable
#define S1SPI_S1 MCS251_error_non_bit_addressable
// S1_S0: P_SW1^6 -- base P_SW1 (0xA2) is not bit-addressable
#define S1_S0  MCS251_error_non_bit_addressable
// S1_S1: P_SW1^7 -- base P_SW1 (0xA2) is not bit-addressable
#define S1_S1  MCS251_error_non_bit_addressable
// S2RB8: S2CON^2 -- base S2CON (0x9A) is not bit-addressable
#define S2RB8  MCS251_error_non_bit_addressable
// S2REN: S2CON^4 -- base S2CON (0x9A) is not bit-addressable
#define S2REN  MCS251_error_non_bit_addressable
// S2RI: S2CON^0 -- base S2CON (0x9A) is not bit-addressable
#define S2RI   MCS251_error_non_bit_addressable
// S2SM0: S2CON^7 -- base S2CON (0x9A) is not bit-addressable
#define S2SM0  MCS251_error_non_bit_addressable
// S2SM1: S2CON^6 -- base S2CON (0x9A) is not bit-addressable
#define S2SM1  MCS251_error_non_bit_addressable
// S2SM2: S2CON^5 -- base S2CON (0x9A) is not bit-addressable
#define S2SM2  MCS251_error_non_bit_addressable
// S2SPI_S0: P_SW3^4 -- base P_SW3 (0xBB) is not bit-addressable
#define S2SPI_S0 MCS251_error_non_bit_addressable
// S2SPI_S1: P_SW3^5 -- base P_SW3 (0xBB) is not bit-addressable
#define S2SPI_S1 MCS251_error_non_bit_addressable
// S2TB8: S2CON^3 -- base S2CON (0x9A) is not bit-addressable
#define S2TB8  MCS251_error_non_bit_addressable
// S2TI: S2CON^1 -- base S2CON (0x9A) is not bit-addressable
#define S2TI   MCS251_error_non_bit_addressable
// S2_S: P_SW2^0 -- base P_SW2 (0xBA) is not bit-addressable
#define S2_S   MCS251_error_non_bit_addressable
// S3RB8: S3CON^2 -- base S3CON (0xAC) is not bit-addressable
#define S3RB8  MCS251_error_non_bit_addressable
// S3REN: S3CON^4 -- base S3CON (0xAC) is not bit-addressable
#define S3REN  MCS251_error_non_bit_addressable
// S3RI: S3CON^0 -- base S3CON (0xAC) is not bit-addressable
#define S3RI   MCS251_error_non_bit_addressable
// S3SM0: S3CON^7 -- base S3CON (0xAC) is not bit-addressable
#define S3SM0  MCS251_error_non_bit_addressable
// S3SM2: S3CON^5 -- base S3CON (0xAC) is not bit-addressable
#define S3SM2  MCS251_error_non_bit_addressable
// S3ST3: S3CON^6 -- base S3CON (0xAC) is not bit-addressable
#define S3ST3  MCS251_error_non_bit_addressable
// S3TB8: S3CON^3 -- base S3CON (0xAC) is not bit-addressable
#define S3TB8  MCS251_error_non_bit_addressable
// S3TI: S3CON^1 -- base S3CON (0xAC) is not bit-addressable
#define S3TI   MCS251_error_non_bit_addressable
// S3_S: P_SW2^1 -- base P_SW2 (0xBA) is not bit-addressable
#define S3_S   MCS251_error_non_bit_addressable
// S4RB8: S4CON^2 -- base S4CON (0xFD) is not bit-addressable
#define S4RB8  MCS251_error_non_bit_addressable
// S4REN: S4CON^4 -- base S4CON (0xFD) is not bit-addressable
#define S4REN  MCS251_error_non_bit_addressable
// S4RI: S4CON^0 -- base S4CON (0xFD) is not bit-addressable
#define S4RI   MCS251_error_non_bit_addressable
// S4SM0: S4CON^7 -- base S4CON (0xFD) is not bit-addressable
#define S4SM0  MCS251_error_non_bit_addressable
// S4SM2: S4CON^5 -- base S4CON (0xFD) is not bit-addressable
#define S4SM2  MCS251_error_non_bit_addressable
// S4ST4: S4CON^6 -- base S4CON (0xFD) is not bit-addressable
#define S4ST4  MCS251_error_non_bit_addressable
// S4TB8: S4CON^3 -- base S4CON (0xFD) is not bit-addressable
#define S4TB8  MCS251_error_non_bit_addressable
// S4TI: S4CON^1 -- base S4CON (0xFD) is not bit-addressable
#define S4TI   MCS251_error_non_bit_addressable
// S4_S: P_SW2^2 -- base P_SW2 (0xBA) is not bit-addressable
#define S4_S   MCS251_error_non_bit_addressable
// SMOD: PCON^7 -- base PCON (0x87) is not bit-addressable
#define SMOD   MCS251_error_non_bit_addressable
// SMOD0: PCON^6 -- base PCON (0x87) is not bit-addressable
#define SMOD0  MCS251_error_non_bit_addressable
// SPEN: SPCTL^6 -- base SPCTL (0xCE) is not bit-addressable
#define SPEN   MCS251_error_non_bit_addressable
// SPIF: SPSTAT^7 -- base SPSTAT (0xCD) is not bit-addressable
#define SPIF   MCS251_error_non_bit_addressable
// SPI_S0: P_SW1^2 -- base P_SW1 (0xA2) is not bit-addressable
#define SPI_S0 MCS251_error_non_bit_addressable
// SPI_S1: P_SW1^3 -- base P_SW1 (0xA2) is not bit-addressable
#define SPI_S1 MCS251_error_non_bit_addressable
// SPR0: SPCTL^0 -- base SPCTL (0xCE) is not bit-addressable
#define SPR0   MCS251_error_non_bit_addressable
// SPR1: SPCTL^1 -- base SPCTL (0xCE) is not bit-addressable
#define SPR1   MCS251_error_non_bit_addressable
// SSIG: SPCTL^7 -- base SPCTL (0xCE) is not bit-addressable
#define SSIG   MCS251_error_non_bit_addressable
// SWBS: IAP_CONTR^6 -- base IAP_CONTR (0xC7) is not bit-addressable
#define SWBS   MCS251_error_non_bit_addressable
// SWBS2: IAP_CONTR^3 -- base IAP_CONTR (0xC7) is not bit-addressable
#define SWBS2  MCS251_error_non_bit_addressable
// SWRST: IAP_CONTR^5 -- base IAP_CONTR (0xC7) is not bit-addressable
#define SWRST  MCS251_error_non_bit_addressable
// T0CLKO: INTCLKO^0 -- base INTCLKO (0x8F) is not bit-addressable
#define T0CLKO MCS251_error_non_bit_addressable
// T0_CT: TMOD^2 -- base TMOD (0x89) is not bit-addressable
#define T0_CT  MCS251_error_non_bit_addressable
// T0_GATE: TMOD^3 -- base TMOD (0x89) is not bit-addressable
#define T0_GATE MCS251_error_non_bit_addressable
// T0_M0: TMOD^0 -- base TMOD (0x89) is not bit-addressable
#define T0_M0  MCS251_error_non_bit_addressable
// T0_M1: TMOD^1 -- base TMOD (0x89) is not bit-addressable
#define T0_M1  MCS251_error_non_bit_addressable
// T0x12: AUXR^7 -- base AUXR (0x8E) is not bit-addressable
#define T0x12  MCS251_error_non_bit_addressable
// T1CLKO: INTCLKO^1 -- base INTCLKO (0x8F) is not bit-addressable
#define T1CLKO MCS251_error_non_bit_addressable
// T1_CT: TMOD^6 -- base TMOD (0x89) is not bit-addressable
#define T1_CT  MCS251_error_non_bit_addressable
// T1_GATE: TMOD^7 -- base TMOD (0x89) is not bit-addressable
#define T1_GATE MCS251_error_non_bit_addressable
// T1_M0: TMOD^4 -- base TMOD (0x89) is not bit-addressable
#define T1_M0  MCS251_error_non_bit_addressable
// T1_M1: TMOD^5 -- base TMOD (0x89) is not bit-addressable
#define T1_M1  MCS251_error_non_bit_addressable
// T1x12: AUXR^6 -- base AUXR (0x8E) is not bit-addressable
#define T1x12  MCS251_error_non_bit_addressable
// T2CLKO: INTCLKO^2 -- base INTCLKO (0x8F) is not bit-addressable
#define T2CLKO MCS251_error_non_bit_addressable
// T2CT: AUXR^3 -- base AUXR (0x8E) is not bit-addressable
#define T2CT   MCS251_error_non_bit_addressable
// T2IF: AUXINTIF^0 -- base AUXINTIF (0xEF) is not bit-addressable
#define T2IF   MCS251_error_non_bit_addressable
// T2R: AUXR^4 -- base AUXR (0x8E) is not bit-addressable
#define T2R    MCS251_error_non_bit_addressable
// T2_CT: AUXR^3 -- base AUXR (0x8E) is not bit-addressable
#define T2_CT  MCS251_error_non_bit_addressable
// T2x12: AUXR^2 -- base AUXR (0x8E) is not bit-addressable
#define T2x12  MCS251_error_non_bit_addressable
// T3CLKO: T4T3M^0 -- base T4T3M (0xDD) is not bit-addressable
#define T3CLKO MCS251_error_non_bit_addressable
// T3CT: T4T3M^2 -- base T4T3M (0xDD) is not bit-addressable
#define T3CT   MCS251_error_non_bit_addressable
// T3IF: AUXINTIF^1 -- base AUXINTIF (0xEF) is not bit-addressable
#define T3IF   MCS251_error_non_bit_addressable
// T3R: T4T3M^3 -- base T4T3M (0xDD) is not bit-addressable
#define T3R    MCS251_error_non_bit_addressable
// T3_CT: T4T3M^2 -- base T4T3M (0xDD) is not bit-addressable
#define T3_CT  MCS251_error_non_bit_addressable
// T3x12: T4T3M^1 -- base T4T3M (0xDD) is not bit-addressable
#define T3x12  MCS251_error_non_bit_addressable
// T4CLKO: T4T3M^4 -- base T4T3M (0xDD) is not bit-addressable
#define T4CLKO MCS251_error_non_bit_addressable
// T4CT: T4T3M^6 -- base T4T3M (0xDD) is not bit-addressable
#define T4CT   MCS251_error_non_bit_addressable
// T4IF: AUXINTIF^2 -- base AUXINTIF (0xEF) is not bit-addressable
#define T4IF   MCS251_error_non_bit_addressable
// T4R: T4T3M^7 -- base T4T3M (0xDD) is not bit-addressable
#define T4R    MCS251_error_non_bit_addressable
// T4_CT: T4T3M^6 -- base T4T3M (0xDD) is not bit-addressable
#define T4_CT  MCS251_error_non_bit_addressable
// T4x12: T4T3M^5 -- base T4T3M (0xDD) is not bit-addressable
#define T4x12  MCS251_error_non_bit_addressable
// USBCKS: IRCBAND^7 -- base IRCBAND (0x9D) is not bit-addressable
#define USBCKS MCS251_error_non_bit_addressable
// USBCKS2: IRCBAND^6 -- base IRCBAND (0x9D) is not bit-addressable
#define USBCKS2 MCS251_error_non_bit_addressable
// USBRST: USBCON^6 -- base USBCON (0xF4) is not bit-addressable
#define USBRST MCS251_error_non_bit_addressable
// WCOL: SPSTAT^6 -- base SPSTAT (0xCD) is not bit-addressable
#define WCOL   MCS251_error_non_bit_addressable
// WDT_FLAG: WDT_CONTR^7 -- base WDT_CONTR (0xC1) is not bit-addressable
#define WDT_FLAG MCS251_error_non_bit_addressable
// WKTEN: WKTCH^7 -- base WKTCH (0xAB) is not bit-addressable
#define WKTEN  MCS251_error_non_bit_addressable

#endif /* __MCS251_BIT_COMPAT_H__ */
