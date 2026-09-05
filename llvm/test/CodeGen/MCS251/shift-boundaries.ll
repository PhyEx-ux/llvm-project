; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %s -o %t.O2.rel

; Keep all three directions covered at count 0, 1, width-1 and width.
; Counts >= width produce poison, NOT a saturating or masked shift. For a
; direct undefined scalar return this target chooses zero (Phase 14).
define i8 @zero8(i8 %x) {
; CHECK-LABEL: _zero8:
; CHECK-NOT: sll
; CHECK-NOT: srl
; CHECK-NOT: sra
; CHECK: eret
  %a = shl i8 %x, 0
  %b = lshr i8 %x, 0
  %c = ashr i8 %x, 0
  %d = or i8 %a, %b
  %r = or i8 %d, %c
  ret i8 %r
}
define i16 @zero16(i16 %x) {
; CHECK-LABEL: _zero16:
; CHECK-NOT: sll
; CHECK-NOT: srl
; CHECK-NOT: sra
; CHECK: eret
  %a = shl i16 %x, 0
  %b = lshr i16 %x, 0
  %c = ashr i16 %x, 0
  %d = or i16 %a, %b
  %r = or i16 %d, %c
  ret i16 %r
}
define i32 @zero32(i32 %x) {
; CHECK-LABEL: _zero32:
; CHECK-NOT: sll
; CHECK-NOT: srl
; CHECK-NOT: sra
; CHECK: eret
  %a = shl i32 %x, 0
  %b = lshr i32 %x, 0
  %c = ashr i32 %x, 0
  %d = or i32 %a, %b
  %r = or i32 %d, %c
  ret i32 %r
}
define i8 @one8(i8 %x) {
; CHECK-LABEL: _one8:
; CHECK: eret
  %a = shl i8 %x, 1
  %b = lshr i8 %x, 1
  %c = ashr i8 %x, 1
  %d = or i8 %a, %b
  %r = or i8 %d, %c
  ret i8 %r
}
define i16 @one16(i16 %x) {
; CHECK-LABEL: _one16:
; CHECK: eret
  %a = shl i16 %x, 1
  %b = lshr i16 %x, 1
  %c = ashr i16 %x, 1
  %d = or i16 %a, %b
  %r = or i16 %d, %c
  ret i16 %r
}
define i32 @one32(i32 %x) {
; CHECK-LABEL: _one32:
; CHECK: eret
  %a = shl i32 %x, 1
  %b = lshr i32 %x, 1
  %c = ashr i32 %x, 1
  %d = or i32 %a, %b
  %r = or i32 %d, %c
  ret i32 %r
}
define i8 @last8(i8 %x) {
; CHECK-LABEL: _last8:
; CHECK: eret
  %a = shl i8 %x, 7
  %b = lshr i8 %x, 7
  %c = ashr i8 %x, 7
  %d = or i8 %a, %b
  %r = or i8 %d, %c
  ret i8 %r
}
define i16 @last16(i16 %x) {
; CHECK-LABEL: _last16:
; CHECK: eret
  %a = shl i16 %x, 15
  %b = lshr i16 %x, 15
  %c = ashr i16 %x, 15
  %d = or i16 %a, %b
  %r = or i16 %d, %c
  ret i16 %r
}
define i32 @last32(i32 %x) {
; CHECK-LABEL: _last32:
; CHECK: eret
  %a = shl i32 %x, 31
  %b = lshr i32 %x, 31
  %c = ashr i32 %x, 31
  %d = or i32 %a, %b
  %r = or i32 %d, %c
  ret i32 %r
}
define i8 @poison8(i8 %x) {
; CHECK-LABEL: _poison8:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: eret
  %a = shl i8 %x, 8
  %b = lshr i8 %x, 9
  %c = ashr i8 %x, 255
  %d = or i8 %a, %b
  %r = or i8 %d, %c
  ret i8 %r
}
define i16 @poison16(i16 %x) {
; CHECK-LABEL: _poison16:
; CHECK: #0x0000
; CHECK: eret
  %a = shl i16 %x, 16
  %b = lshr i16 %x, 17
  %c = ashr i16 %x, 65535
  %d = or i16 %a, %b
  %r = or i16 %d, %c
  ret i16 %r
}
define i32 @poison32(i32 %x) {
; CHECK-LABEL: _poison32:
; CHECK: #0x0000
; CHECK: eret
  %a = shl i32 %x, 32
  %b = lshr i32 %x, 33
  %c = ashr i32 %x, 4294967295
  %d = or i32 %a, %b
  %r = or i32 %d, %c
  ret i32 %r
}
