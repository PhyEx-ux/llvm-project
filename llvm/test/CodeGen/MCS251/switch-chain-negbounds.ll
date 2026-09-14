; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; BRJT S1 matrix (design §3.1.3, G3): lower-bound handling on the comparison
; chain. Negative case values materialise as the 32-bit two-instruction form
; `mov dr4,#lo16 + movh dr4,#hi16` (bitwise pattern of the two's-complement
; value) and the eq compare stays sign-agnostic; a positive lower bound just
; shifts the immediates. No range header (`sub` + unsigned bound check) may
; appear: without jump tables every cluster is a plain eq chain.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @negm4(i32 noundef %x) {
; CHECK-LABEL: negm4:
; -4..-1: 0xfffc/0xffff, 0xfffd/0xffff, 0xfffe/0xffff, 0xffff/0xffff.
; O0:      mov dr4, #0xfffc
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0xfffd
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0xfffe
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0xffff
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2-DAG: mov dr4, #0xfffc
; O2-DAG: mov dr4, #0xfffd
; O2-DAG: mov dr4, #0xfffe
; O2-DAG: mov dr4, #0xffff
entry:
  switch i32 %x, label %default [
    i32 -4, label %c0
    i32 -3, label %c1
    i32 -2, label %c2
    i32 -1, label %c3
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

define void @poslow5(i32 noundef %x) {
; CHECK-LABEL: poslow5:
; 5..8: plain immediates, no movh.
; O0:      mov dr4, #0x0005
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0006
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0007
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0008
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; The mixed-bound form must not appear: no sub/xor header, no unsigned
; bound-compare branch feeding the dispatch.
; O0-NOT: sub dr0
; O2-DAG: mov dr4, #0x0005
; O2-DAG: mov dr4, #0x0006
; O2-DAG: mov dr4, #0x0007
; O2-DAG: mov dr4, #0x0008
entry:
  switch i32 %x, label %default [
    i32 5, label %c0
    i32 6, label %c1
    i32 7, label %c2
    i32 8, label %c3
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
