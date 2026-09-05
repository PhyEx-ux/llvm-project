; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %s -o %t.O2.rel

; Zero must bypass the loop. The value-defining decrement precedes the
; branch so FastRA can spill it safely. The conditional target is the nearby
; trampoline, not the potentially spill-expanded loop header.
define i8 @shl8(i8 %x, i8 %n) {
; CHECK-LABEL: _shl8:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: je
; CHECK: sll r
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
; CHECK-NEXT: ejmp
  %r = shl i8 %x, %n
  ret i8 %r
}
define i8 @lshr8(i8 %x, i8 %n) {
; CHECK-LABEL: _lshr8:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: srl r
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = lshr i8 %x, %n
  ret i8 %r
}
define i8 @ashr8(i8 %x, i8 %n) {
; CHECK-LABEL: _ashr8:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: sra r
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = ashr i8 %x, %n
  ret i8 %r
}
define i16 @shl16(i16 %x, i16 %n) {
; CHECK-LABEL: _shl16:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: sll wr
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = shl i16 %x, %n
  ret i16 %r
}
define i16 @lshr16(i16 %x, i16 %n) {
; CHECK-LABEL: _lshr16:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: srl wr
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = lshr i16 %x, %n
  ret i16 %r
}
define i16 @ashr16(i16 %x, i16 %n) {
; CHECK-LABEL: _ashr16:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: sra wr
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = ashr i16 %x, %n
  ret i16 %r
}
define i32 @shl32(i32 %x, i32 %n) {
; CHECK-LABEL: _shl32:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: add dr[[D:[0-9]+]], dr[[D]]
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = shl i32 %x, %n
  ret i32 %r
}
define i32 @lshr32(i32 %x, i32 %n) {
; CHECK-LABEL: _lshr32:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: srl wr
; CHECK-NEXT: mov a, #0x00
; CHECK-NEXT: rrc a
; CHECK-NEXT: srl wr
; CHECK-NEXT: orl r{{[0-9]+}}, a
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = lshr i32 %x, %n
  ret i32 %r
}
define i32 @ashr32(i32 %x, i32 %n) {
; CHECK-LABEL: _ashr32:
; CHECK: cmp r{{[0-7]}}, #0x00
; CHECK: sra wr
; CHECK-NEXT: mov a, #0x00
; CHECK-NEXT: rrc a
; CHECK-NEXT: srl wr
; CHECK-NEXT: orl r{{[0-9]+}}, a
; CHECK: sub r{{[0-7]}}, #0x01
; CHECK: jne
  %r = ashr i32 %x, %n
  ret i32 %r
}

; The last byte OR's Z flag is not an i32 zero test (80000001>>1 gives
; 40000000 with that flag set). Consumers must emit a new comparison.
define i8 @shift_is_zero(i32 %x, i32 %n) {
; CHECK-LABEL: _shift_is_zero:
; CHECK-DAG: orl r{{[0-9]+}}, a
; CHECK-DAG: cmp dr
  %v = lshr i32 %x, %n
  %c = icmp eq i32 %v, 0
  %r = zext i1 %c to i8
  ret i8 %r
}

; Values/counts must survive two independently lowered loops and a call.
declare void @clobber()
define i32 @shift_pressure(i32 %x, i32 %y, i32 %n) {
; CHECK-LABEL: _shift_pressure:
; CHECK-DAG: sub r{{[0-7]}}, #0x01
; CHECK-DAG: sub r{{[0-7]}}, #0x01
; CHECK-DAG: ecall _clobber
  %a = shl i32 %x, %n
  %b = ashr i32 %y, %n
  call void @clobber()
  %c = add i32 %a, %b
  %d = xor i32 %c, %x
  %e = xor i32 %d, %y
  ret i32 %e
}
