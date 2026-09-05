; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
;
; Former rejection: each byte of i32 is stored most significant first.
define void @store32() {
; CHECK-LABEL: store32:
; CHECK: mov [[B0:r[0-9]+]], #0x12
; CHECK: mov @dr{{[0-9]+}}, [[B0]]
; CHECK: mov [[B1:r[0-9]+]], #0x34
; CHECK: mov @dr{{[0-9]+}}+0x0001, [[B1]]
; CHECK: mov [[B2:r[0-9]+]], #0x56
; CHECK: mov @dr{{[0-9]+}}+0x0002, [[B2]]
; CHECK: mov [[B3:r[0-9]+]], #0x78
; CHECK: mov @dr{{[0-9]+}}+0x0003, [[B3]]
  store volatile i32 305419896, ptr inttoptr (i32 48 to ptr), align 1
  ret void
}
