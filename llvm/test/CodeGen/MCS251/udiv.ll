; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj < %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj < %s -o %t.O2.rel

; Standard libcall lowering must use the SDCC-compatible family, exactly
; two underscores (the target's global prefix is applied once).
define i8 @div8(i8 %a, i8 %b) {
; CHECK-LABEL: _div8:
; CHECK: __divuint_PARM_2
; CHECK: ecall __divuint
; CHECK: eret
  %q = udiv i8 %a, %b
  ret i8 %q
}
define i16 @div16(i16 %a, i16 %b) {
; CHECK-LABEL: _div16:
; CHECK: __divuint_PARM_2
; CHECK: ecall __divuint
; CHECK: eret
  %q = udiv i16 %a, %b
  ret i16 %q
}
define i32 @div32(i32 %a, i32 %b) {
; CHECK-LABEL: _div32:
; CHECK: __divulong_PARM_2
; CHECK: ecall __divulong
; CHECK: eret
  %q = udiv i32 %a, %b
  ret i32 %q
}

; Independent libcalls in one DAG must not interleave static parameter
; stores; input and output values must also survive the call's regmask.
define i32 @div_twice(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: _div_twice:
; CHECK: ecall __divulong
; CHECK: ecall __divulong
; CHECK: eret
  %q0 = udiv i32 %a, %b
  %q1 = udiv i32 %b, %c
  %sum = add i32 %q0, %q1
  %result = add i32 %sum, %a
  ret i32 %result
}
define i8 @div8_one(i8 %a) {
; CHECK-LABEL: _div8_one:
; CHECK-NOT: ecall
; CHECK: eret
  %q = udiv i8 %a, 1
  ret i8 %q
}
define i16 @div16_max(i16 %a) {
; CHECK-LABEL: _div16_max:
; CHECK: eret
  %q = udiv i16 %a, 65535
  ret i16 %q
}
define i32 @div32_signbit(i32 %a) {
; CHECK-LABEL: _div32_signbit:
; CHECK-NOT: ecall
; CHECK: eret
  %q = udiv i32 %a, 2147483648
  ret i32 %q
}
define i32 @div32_max(i32 %a) {
; CHECK-LABEL: _div32_max:
; CHECK: eret
  %q = udiv i32 %a, 4294967295
  ret i32 %q
}
define i32 @div32_ten(i32 %a) {
; CHECK-LABEL: _div32_ten:
; CHECK: eret
  %q = udiv i32 %a, 10
  ret i32 %q
}
; Division by zero is undefined behavior (LangRef); no trap or helper
; contract is promised. CHECK-NOT: trap here is a codegen regression check
; for this fixture only, not a runtime behavior promise (division design v4
; §8.3).
define i32 @div32_zero(i32 %a) {
; CHECK-LABEL: _div32_zero:
; CHECK-NOT: trap
; CHECK: eret
  %q = udiv i32 %a, 0
  ret i32 %q
}
