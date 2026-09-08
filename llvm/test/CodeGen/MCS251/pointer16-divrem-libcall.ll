; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Target-generated direct libcalls are symbols, not indirect AS0 addresses.
; Tiny and XTiny must rebuild all eight i16/i32 div/rem helpers as 32-bit AS4
; CODE callees even though getPointerTy() for ordinary data is i16.

; CHECK-DAG: ecall __divuint
; CHECK-DAG: ecall __divulong
; CHECK-DAG: ecall __divsint
; CHECK-DAG: ecall __divslong
; CHECK-DAG: ecall __moduint
; CHECK-DAG: ecall __modulong
; CHECK-DAG: ecall __modsint
; CHECK-DAG: ecall __modslong

define i16 @udiv16(i16 %a, i16 %b) addrspace(4) {
  %r = udiv i16 %a, %b
  ret i16 %r
}
define i32 @udiv32(i32 %a, i32 %b) addrspace(4) {
  %r = udiv i32 %a, %b
  ret i32 %r
}
define i16 @sdiv16(i16 %a, i16 %b) addrspace(4) {
  %r = sdiv i16 %a, %b
  ret i16 %r
}
define i32 @sdiv32(i32 %a, i32 %b) addrspace(4) {
  %r = sdiv i32 %a, %b
  ret i32 %r
}
define i16 @urem16(i16 %a, i16 %b) addrspace(4) {
  %r = urem i16 %a, %b
  ret i16 %r
}
define i32 @urem32(i32 %a, i32 %b) addrspace(4) {
  %r = urem i32 %a, %b
  ret i32 %r
}
define i16 @srem16(i16 %a, i16 %b) addrspace(4) {
  %r = srem i16 %a, %b
  ret i16 %r
}
define i32 @srem32(i32 %a, i32 %b) addrspace(4) {
  %r = srem i32 %a, %b
  ret i32 %r
}
