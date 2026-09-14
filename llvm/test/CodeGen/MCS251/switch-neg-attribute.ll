; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; BRJT S1 (design §4 G5): the "no-jump-tables" attribute gate keeps its
; semantics unchanged after the target gate closed. The attribute and the
; target-level default now take the intersection of two closed gates: the
; function lowers through the exact same comparison chain a non-attributed
; function uses (see switch-chain-matrix.ll for the same shape without the
; attribute), and once `-mcs251-jump-tables` opts in, this attribute still
; forces the chain for this function (areJTsAllowed checks the attribute
; before consulting the action table).

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @forced_chain(i32 noundef %x) nounwind "no-jump-tables"="true" {
; CHECK-LABEL: forced_chain:
; O0:      mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0003
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2:      mov dr4, #0x0003
; O2-NEXT: cmp dr0, dr4
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}
