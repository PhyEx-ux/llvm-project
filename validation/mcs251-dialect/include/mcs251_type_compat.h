/*===---- mcs251_type_compat.h - MCS-251 Keil DEF.H type compat ----------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions. See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/

//===----------------------------------------------------------------------===//
// BT06 X4 extension: thin type/macro compatibility layer for the official
// STC32G144K246 demo corpus common header COMM/DEF.H (the BYTE/WORD family
// and the register helper macros used by the non-USB demos).
//
// GENERATED - DO NOT EDIT BY HAND.  Authoritative source: the sha256-pinned
// official DEF.H plus the frozen disposition table inside
//   validation/mcs251-dialect/tools/gen-type-compat-header.py
// which also writes the full ledger type-compat.json.  Regenerate with:
//   tools/gen-type-compat-header.py
// In-tree consistency is checked by:
//   tools/gen-type-compat-header.py --check-header include/mcs251_type_compat.h
//
// Scope (XDATA-CODE-SLICE-TASK.md X4: "BT06 compat header extension, only
// what the non-USB demos need").  Every DEF.H name is exactly one of:
//   mapped    - emitted below (widths preserved: WORD/INT map to short,
//               Keil C251 int is 16-bit, this target's int is 32-bit;
//               BOOL maps to the real `bit` type -- P09 design 6.5.6 flip
//               2026-09-16, the bit-object support chain passed);
//   external  - owned by a standard header (the stdint-shaped names);
//               never emitted here, include <stdint.h> for them;
//   rejected  - a precise compile-time error (none in the table today;
//               the former BOOL freeze was lifted by the P09 flip).
//
// This header does NOT define the bare `bit`/`sbit` keywords (frontend,
// -fmcs251-keil), does NOT emit SFR names (use the sfr-convert generated
// stc32g144k246-as6.h) and does NOT emit sbit names (use
// mcs251_bit_compat.h).  Macro definitions carry #ifndef guards: the layer
// is opt-in and must never break an including translation unit.
//===----------------------------------------------------------------------===*/

#ifndef __MCS251_TYPE_COMPAT_H__
#define __MCS251_TYPE_COMPAT_H__

//===----------------------------------------------------------------------===//
// Mapped types (19): widths preserved against Keil C251.
//===----------------------------------------------------------------------===//

typedef bit BOOL;  // DEF.H line 6: official `bit`; P09 6.5.6 flip 2026-09-16 (support chain passed, identity gate fixed)
typedef unsigned char BYTE;  // DEF.H line 8: official unsigned char
typedef unsigned short WORD;  // DEF.H line 9: Keil C251 int is 16-bit, this target's int is 32-bit; unsigned short preserves the 16-bit width
typedef unsigned long DWORD;  // DEF.H line 10: 32-bit in both worlds
typedef signed char CHAR;  // DEF.H line 12: official signed char; plain char's signedness is implementation-defined, the official type is not
typedef signed short INT;  // DEF.H line 13: Keil C251 int is 16-bit, this target's int is 32-bit; signed short preserves the 16-bit width
typedef signed long LONG;  // DEF.H line 14: 32-bit in both worlds
typedef unsigned char uint8;  // DEF.H line 24: official exact
typedef unsigned short uint16;  // DEF.H line 25: 16-bit width preserved (see WORD)
typedef unsigned long uint32;  // DEF.H line 26: official exact
typedef signed char int8;  // DEF.H line 28: official exact
typedef signed short int16;  // DEF.H line 29: 16-bit width preserved (see INT)
typedef signed long int32;  // DEF.H line 30: official exact
typedef unsigned char u8;  // DEF.H line 32: official exact
typedef unsigned short u16;  // DEF.H line 33: 16-bit width preserved (see WORD)
typedef unsigned long u32;  // DEF.H line 34: official exact
typedef signed char s8;  // DEF.H line 36: official exact
typedef signed short s16;  // DEF.H line 37: 16-bit width preserved (see INT)
typedef signed long s32;  // DEF.H line 38: official exact

//===----------------------------------------------------------------------===//
// Rejected (0): precise errors, never silent emulations.
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// External ownership (6): C99 <stdint.h> provides these;
// this header never redefines them.
//===----------------------------------------------------------------------===//
//
// uint8_t: C99 <stdint.h> owns this name; include <stdint.h> (DEF.H line 16)
// uint16_t: C99 <stdint.h> owns this name; include <stdint.h> (DEF.H line 17)
// uint32_t: C99 <stdint.h> owns this name; include <stdint.h> (DEF.H line 18)
// int8_t: C99 <stdint.h> owns this name; include <stdint.h> (DEF.H line 20)
// int16_t: C99 <stdint.h> owns this name; include <stdint.h> (DEF.H line 21)
// int32_t: C99 <stdint.h> owns this name; include <stdint.h> (DEF.H line 22)

//===----------------------------------------------------------------------===//
// Mapped macros (50): official bodies, #ifndef guards
// added (the layer is opt-in).
//===----------------------------------------------------------------------===//

#ifndef NULL
#define NULL  0
#endif
#ifndef LOW
#define LOW  0
#endif
#ifndef HIGH
#define HIGH  1
#endif
#ifndef FALSE
#define FALSE  0
#endif
#ifndef TRUE
#define TRUE  1
#endif
#ifndef DISABLE
#define DISABLE  0
#endif
#ifndef ENABLE
#define ENABLE  1
#endif
#ifndef min
#define min(a, b)  ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)  ((a) > (b) ? (a) : (b))
#endif
#ifndef LOBYTE
#define LOBYTE(n)  ((BYTE)(n))
#endif
#ifndef HIBYTE
#define HIBYTE(n)  ((BYTE)(((WORD)(n) >> 8) & 0xff))
#endif
#ifndef LOWORD
#define LOWORD(n)  ((WORD)(n))
#endif
#ifndef HIWORD
#define HIWORD(n)  ((WORD)(((DWORD)(n) >> 16) & 0xffff))
#endif
#ifndef MAKEWORD
#define MAKEWORD(l, h)  ((WORD)(((BYTE)(l)) | ((WORD)((BYTE)(h))) << 8))
#endif
#ifndef MAKELONG
#define MAKELONG(l, h)  ((DWORD)(((WORD)(l)) | ((DWORD)((WORD)(h))) << 16))
#endif
#ifndef BYTE0
#define BYTE0(n)  LOBYTE(n)
#endif
#ifndef BYTE1
#define BYTE1(n)  HIBYTE(n)
#endif
#ifndef BYTE2
#define BYTE2(n)  LOBYTE(HIWORD(n))
#endif
#ifndef BYTE3
#define BYTE3(n)  HIBYTE(HIWORD(n))
#endif
#ifndef WORD0
#define WORD0(n)  LOWORD(n)
#endif
#ifndef WORD2
#define WORD2(n)  HIWORD(n)
#endif
#ifndef BIT0
#define BIT0  0x01
#endif
#ifndef BIT1
#define BIT1  0x02
#endif
#ifndef BIT2
#define BIT2  0x04
#endif
#ifndef BIT3
#define BIT3  0x08
#endif
#ifndef BIT4
#define BIT4  0x10
#endif
#ifndef BIT5
#define BIT5  0x20
#endif
#ifndef BIT6
#define BIT6  0x40
#endif
#ifndef BIT7
#define BIT7  0x80
#endif
#ifndef BIT
#define BIT(b)  (BIT##b)
#endif
#ifndef BIT_LN
#define BIT_LN  0x0f
#endif
#ifndef BIT_HN
#define BIT_HN  0xf0
#endif
#ifndef BIT_ALL
#define BIT_ALL  0xff
#endif
#ifndef PIN_0
#define PIN_0  BIT0
#endif
#ifndef PIN_1
#define PIN_1  BIT1
#endif
#ifndef PIN_2
#define PIN_2  BIT2
#endif
#ifndef PIN_3
#define PIN_3  BIT3
#endif
#ifndef PIN_4
#define PIN_4  BIT4
#endif
#ifndef PIN_5
#define PIN_5  BIT5
#endif
#ifndef PIN_6
#define PIN_6  BIT6
#endif
#ifndef PIN_7
#define PIN_7  BIT7
#endif
#ifndef PIN_ALL
#define PIN_ALL  BIT_ALL
#endif
#ifndef CLR_REG_BIT
#define CLR_REG_BIT(r, b)  ((r) &= ~(b))
#endif
#ifndef SET_REG_BIT
#define SET_REG_BIT(r, b)  ((r) |= (b))
#endif
#ifndef CPL_REG_BIT
#define CPL_REG_BIT(r, b)  ((r) ^= (b))
#endif
#ifndef READ_REG_BIT
#define READ_REG_BIT(r, b)  ((r) & (b))
#endif
#ifndef READ_REG
#define READ_REG(r)  (r)
#endif
#ifndef WRITE_REG
#define WRITE_REG(r, v)  ((r) = (v))
#endif
#ifndef CLR_REG
#define CLR_REG(r)  ((r) = 0)
#endif
#ifndef MODIFY_REG
#define MODIFY_REG(r, clr, set)  ((r) = (((r) & ~(clr)) | ((set) & (clr))))
#endif

#endif /* __MCS251_TYPE_COMPAT_H__ */
