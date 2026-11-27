; RUN: llc -mtriple=mcs251 -O1 -function-sections -data-sections -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readelf -S %t.o | FileCheck %s

; CHECK-DAG: .text._live_fn
; CHECK-DAG: .text._dead_fn
; CHECK-DAG: .mcs251.dseg._live_g
; CHECK-DAG: .mcs251.xinit._live_g
; CHECK-DAG: .mcs251.dseg._dead_g
; CHECK-DAG: .mcs251.xinit._dead_g
; CHECK-DAG: .rodata._live_ro
; CHECK-DAG: .rodata._dead_ro
; CHECK-DAG: .mcs251.XSEG._live_x
; CHECK-DAG: .mcs251.xdata_init._live_x
; CHECK-DAG: .mcs251.XSEG._dead_x
; CHECK-DAG: .mcs251.xdata_init._dead_x

target triple = "mcs251-unknown-none"

@live_g = global i16 11, align 1
@dead_g = global i16 22, align 1
@live_ro = constant i16 33, align 1
@dead_ro = constant i16 44, align 1
@live_x = addrspace(3) global i16 55, align 1
@dead_x = addrspace(3) global i16 66, align 1

define i16 @live_fn() {
  %a = load i16, ptr @live_g, align 1
  %b = load i16, ptr @live_ro, align 1
  %c = load i16, ptr addrspace(3) @live_x, align 1
  %s1 = add i16 %a, %b
  %s2 = add i16 %s1, %c
  ret i16 %s2
}

define i16 @dead_fn() {
  %a = load i16, ptr @dead_g, align 1
  %b = load i16, ptr @dead_ro, align 1
  %c = load i16, ptr addrspace(3) @dead_x, align 1
  %s1 = add i16 %a, %b
  %s2 = add i16 %s1, %c
  ret i16 %s2
}

!mcs251.signatures = !{!0, !1}
!0 = !{!"_live_fn", i32 1, i32 0}
!1 = !{!"_dead_fn", i32 1, i32 0}
