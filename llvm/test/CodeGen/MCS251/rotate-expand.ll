; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %s -o %t.O2.rel

; Rotate/funnel operations are expanded by generic legalization, not marked
; Legal. Unlike plain shifts, counts are modulo width, including zero/width.
declare i8 @llvm.fshl.i8(i8, i8, i8)
declare i16 @llvm.fshl.i16(i16, i16, i16)
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i8 @llvm.fshr.i8(i8, i8, i8)
declare i16 @llvm.fshr.i16(i16, i16, i16)
declare i32 @llvm.fshr.i32(i32, i32, i32)
define i8 @rol8(i8 %x, i8 %n) {
; CHECK-LABEL: _rol8:
; CHECK: sll r
  %r = call i8 @llvm.fshl.i8(i8 %x, i8 %x, i8 %n)
  ret i8 %r
}
define i16 @rol16(i16 %x, i16 %n) {
; CHECK-LABEL: _rol16:
; CHECK: sll wr
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 %n)
  ret i16 %r
}
define i32 @rol32(i32 %x, i32 %n) {
; CHECK-LABEL: _rol32:
; CHECK: add dr
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n)
  ret i32 %r
}
define i8 @ror8(i8 %x, i8 %n) {
; CHECK-LABEL: _ror8:
; CHECK: srl r
  %r = call i8 @llvm.fshr.i8(i8 %x, i8 %x, i8 %n)
  ret i8 %r
}
define i16 @ror16(i16 %x, i16 %n) {
; CHECK-LABEL: _ror16:
; CHECK: srl wr
  %r = call i16 @llvm.fshr.i16(i16 %x, i16 %x, i16 %n)
  ret i16 %r
}
define i32 @ror32(i32 %x, i32 %n) {
; CHECK-LABEL: _ror32:
; CHECK: rrc a
  %r = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 %n)
  ret i32 %r
}
