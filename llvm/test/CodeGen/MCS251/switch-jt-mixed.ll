; RUN: llc -mtriple=mcs251 -O2 -mcs251-jump-tables < %s -o - | FileCheck %s --check-prefixes=CHECK,JT
; RUN: llc -mtriple=mcs251 -O0 < %s -o - | FileCheck %s --check-prefixes=CHECK,NOJT
;
; BRJT S3 (design §3.2.5.2): mixed dispatch.  Upstream case-cluster
; partitioning (-O1+) splits {0,1,2,3,100} into a dense segment {0..3}
; (jump-tabled: N=4, Range=4, density 100%) and a sparse case 100 that stays
; on the eq comparison chain.  Both exits must be correct inside their own
; cluster; no table covers the sparse value.  At -O0 there is no
; partitioning and the whole range 0..100 (101 entries) exceeds E3, so the
; whole switch falls back to the chain -- the NOJT run pins that fallback.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @mixed(i32 noundef %x) {
; CHECK-LABEL: mixed:
; Dense segment: JT header (bound 3) + dispatch.
; JT:      mov dr4, #0x0003
; JT:      cmp dr0, dr4
; JT-NEXT: jle {{\.LBB[0-9_]+}}
; JT-NEXT: ejmp {{\.LBB[0-9_]+}}
; JT:      mov dptr, #.LJTI
; JT-NEXT: jmp @a+dptr
; Sparse case: plain eq compare with 100, on the chain.
; JT:      mov dr4, #0x0064
; JT-NEXT: cmp dr0, dr4
; JT-NEXT: jne {{\.LBB[0-9_]+}}
; JT-NEXT: ejmp {{\.LBB[0-9_]+}}
; No table entry may target the sparse case's block from the column: the
; column holds exactly 4 entries (4 * 3 = 12 bytes between the column label
; and the next label; pinned at the object layer by the J16 count in
; switch-jt-ljmp-table.ll).
; Flag off: whole-switch chain fallback.
; NOJT-NOT: {{\.LJTI}}
; NOJT-NOT: jmp @a+dptr
; NOJT: mov dr4, #0x0064
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 100, label %c100
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
c100:
  store volatile i32 100, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}
