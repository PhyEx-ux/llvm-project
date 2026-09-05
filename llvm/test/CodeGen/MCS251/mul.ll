; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj < %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj < %s -o %t.O2.rel

; Byte/word patterns expand fixed-operand pseudos before register allocation.
; Truncated MUL is modulo 2^N for both signed and unsigned operands.
define i8 @mul8(i8 %a, i8 %b) {
; CHECK-LABEL: _mul8:
; CHECK: mul ab
; CHECK: eret
  %p = mul i8 %a, %b
  ret i8 %p
}

define i16 @mul16(i16 %a, i16 %b) {
; CHECK-LABEL: _mul16:
; CHECK: mul wr12, wr8
; CHECK: eret
  %p = mul i16 %a, %b
  ret i16 %p
}

define i32 @mul32(i32 %a, i32 %b) {
; CHECK-LABEL: _mul32:
; CHECK-COUNT-3: mul wr12, wr8
; CHECK: eret
  %p = mul i32 %a, %b
  ret i32 %p
}

define i32 @mul32_constant(i32 %a) {
; CHECK-LABEL: _mul32_constant:
; CHECK: eret
  %p = mul i32 %a, 10
  ret i32 %p
}

; Retain both original arguments and several independent products across a
; call. This exercises DR12 aliases, spills and the call-preserved mask.
declare void @clobber()
define i32 @mul_pressure(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: _mul_pressure:
; CHECK: mul wr12, wr8
; CHECK: ecall _clobber
; CHECK: eret
  %ab = mul i32 %a, %b
  %bc = mul i32 %b, %c
  %ca = mul i32 %c, %a
  call void @clobber()
  %s0 = add i32 %ab, %bc
  %s1 = add i32 %s0, %ca
  %s2 = add i32 %s1, %a
  %s3 = add i32 %s2, %b
  %s4 = add i32 %s3, %c
  ret i32 %s4
}

; Constant boundaries must fold rather than create helpers/traps.
define i8 @mul8_zero(i8 %a) {
; CHECK-LABEL: _mul8_zero:
; CHECK-NOT: {{^[ \t]+mul }}
; CHECK: eret
  %p = mul i8 %a, 0
  ret i8 %p
}
define i16 @mul16_one(i16 %a) {
; CHECK-LABEL: _mul16_one:
; CHECK-NOT: {{^[ \t]+mul }}
; CHECK: eret
  %p = mul i16 %a, 1
  ret i16 %p
}
define i16 @mul16_max(i16 %a) {
; CHECK-LABEL: _mul16_max:
; CHECK: eret
  %p = mul i16 %a, 65535
  ret i16 %p
}
define i32 @mul32_signbit(i32 %a) {
; CHECK-LABEL: _mul32_signbit:
; CHECK: eret
  %p = mul i32 %a, 2147483648
  ret i32 %p
}
define i32 @mul32_max(i32 %a) {
; CHECK-LABEL: _mul32_max:
; CHECK: eret
  %p = mul i32 %a, 4294967295
  ret i32 %p
}
define i32 @mul32_signed(i32 %a) {
; CHECK-LABEL: _mul32_signed:
; CHECK: eret
  %p = mul i32 %a, -7
  ret i32 %p
}
