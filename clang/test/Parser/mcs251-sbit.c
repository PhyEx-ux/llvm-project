// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -ast-dump %s 2>&1 | FileCheck %s

// expected-no-diagnostics
//
// Positive forms of the old-style MCS-251 Keil `sbit` declaration:
//   sbit NAME = BIT_ADDR;            -- a plain bit address in 0-255
//   sbit NAME = SFR_BASE ^ INDEX;    -- bit-addressable SFR base plus index
//
// Only the initializer's `^` is positioning syntax; it is never ordinary XOR
// and never a hardware read.

// CHECK: VarDecl {{.*}} P33 'volatile __bit'
// CHECK: MCS251BitAddressAttr
sbit P33 = 0x24;

// Bit-addressable SFR base 0x88 with index 0..7.
sbit T0 = 0x88 ^ 0;
sbit T7 = 0x88 ^ 7;

// Bit address 0 is legal.
sbit B0 = 0;

// Non-zero indexes over a bit-addressable base.
sbit P3_2 = 0xB0 ^ 2;

// Macros and parenthesized constant expressions are accepted in either form.
#define BASE 0x88
#define IDX 3
sbit M1 = BASE ^ IDX;
sbit M2 = (0x90) ^ (1 + 1);
sbit M3 = (0x24 + 1);

// Same-address redeclarations are the same controlled reference.
sbit R1 = 0x88 ^ 4;
sbit R1 = 0x88 ^ 4;

// sbit declarations may be referenced as ordinary 0/1 bit values.
void use(void) {
  P33 = 1;
  P33 = 0;
  if (T0)
    P33 = 1;
  if (!P3_2)
    P33 = 0;
}
