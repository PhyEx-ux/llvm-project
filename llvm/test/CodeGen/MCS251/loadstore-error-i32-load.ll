; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
;
; Former rejection: four byte loads now implement i32 and pointer objects.
@g32 = external global i32

define i32 @load32() {
; CHECK-LABEL: load32:
; CHECK: .db 0x7e,
; CHECK: (g32) >> 16
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0001
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0002
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0003
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK: mov b,
; CHECK: mov a,
  %v = load volatile i32, ptr @g32, align 1
  ret i32 %v
}
