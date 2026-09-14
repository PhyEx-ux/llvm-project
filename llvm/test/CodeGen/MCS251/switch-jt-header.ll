; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables < %s -o - | FileCheck %s --check-prefixes=CHECK,JT
; RUN: llc -mtriple=mcs251 -O2 -mcs251-jump-tables < %s -o - | FileCheck %s --check-prefixes=CHECK,JT
; RUN: llc -mtriple=mcs251 -O0 < %s -o - | FileCheck %s --check-prefixes=NOJT
;
; BRJT S3 (design §3.2.5.1, H2 structure leg): the upstream JT header builds
; the index as `cond - Low` (performed in the condition's width) and guards
; the range with an unsigned compare against High-Low; out-of-range goes to
; the default.  Negative lower bounds are exact without any target-side
; handling: `sub x, -3` folds to `add x, 3`, and the folded index [0,3] is
; again unsigned.  Both cases then dispatch through `mov dptr,#jt; jmp
; @a+dptr` over a 4-entry ljmp column.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

; Non-zero lower bound 5..8: header subtracts 5, bound compare against 3.
define void @low5_jt(i32 noundef %x) {
; CHECK-LABEL: low5_jt:
; JT:      mov dr4, #0x0005
; JT-NEXT: sub dr0, dr4
; JT:      mov dr4, #0x0003
; JT-NEXT: cmp dr0, dr4
; JT-NEXT: jle [[SKIP:\.LBB[0-9_]+]]
; JT-NEXT: ejmp [[DEF:\.LBB[0-9_]+]]
; JT-NOT:  sub dr0
; JT:      mov dptr, #.LJTI
; JT-NEXT: jmp @a+dptr
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

; Negative lower bound -3..0: `sub x, -3` folds to `add x, 3`; bound compare
; still 3; dispatch unchanged.
define void @neglow_jt(i32 noundef %x) {
; CHECK-LABEL: neglow_jt:
; JT:      mov dr4, #0x0003
; JT:      add dr0, dr4
; JT:      cmp dr0, dr4
; JT-NEXT: jle [[SKIP:\.LBB[0-9_]+]]
; JT-NEXT: ejmp [[DEF:\.LBB[0-9_]+]]
; JT:      mov dptr, #.LJTI
; JT-NEXT: jmp @a+dptr
entry:
  switch i32 %x, label %default [
    i32 -3, label %c0
    i32 -2, label %c1
    i32 -1, label %c2
    i32 0, label %c3
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

; With the flag off both switches are plain eq chains: no header arithmetic,
; no bound compare feeding a table, no table.
; NOJT-NOT: {{\.LJTI}}
; NOJT-NOT: jmp @a+dptr
; NOJT: mov dr4, #0x0005
; NOJT: cmp dr0, dr4
