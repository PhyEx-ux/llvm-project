; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s

; Former rejection: Phase 11 materialises SETCC and lowers non-icmp BRCOND.
; Keep the xor of two independent conditions to exercise both paths.

define i8 @brcond_xor(i8 %x) {
; CHECK-LABEL: brcond_xor:
; CHECK: cmp
; CHECK: cmp
; CHECK: eret
entry:
  %c1 = icmp eq i8 %x, 5
  %c2 = icmp ult i8 %x, 100
  %v = xor i1 %c1, %c2
  br i1 %v, label %a, label %b
a:
  ret i8 1
b:
  ret i8 0
}
