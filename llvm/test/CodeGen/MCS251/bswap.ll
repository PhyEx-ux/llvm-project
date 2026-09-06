; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %s -o %t.O2.rel

; Optimized C byte permutations become llvm.bswap; generic expansion must
; reach existing shifts/logical operations rather than an unselectable node.
declare i16 @llvm.bswap.i16(i16)
declare i32 @llvm.bswap.i32(i32)
define i16 @swap16(i16 %x) {
; CHECK-LABEL: _swap16:
; CHECK-NOT: ecall
; CHECK: eret
  %r = call i16 @llvm.bswap.i16(i16 %x)
  ret i16 %r
}
define i32 @swap32(i32 %x) {
; CHECK-LABEL: _swap32:
; CHECK-NOT: ecall
; CHECK: eret
  %r = call i32 @llvm.bswap.i32(i32 %x)
  ret i32 %r
}
define i32 @swap_low16(i32 %x) {
; CHECK-LABEL: _swap_low16:
; CHECK-NOT: ecall
; CHECK: eret
  %lo = trunc i32 %x to i16
  %r = call i16 @llvm.bswap.i16(i16 %lo)
  %wide = zext i16 %r to i32
  ret i32 %wide
}
