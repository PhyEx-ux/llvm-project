; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; BRJT S1 matrix (design S3.1.3, G3): the default clause only changes the
; default BLOCK's body, never the dispatch structure. Both variants below use
; the same 4-case dense switch (over the upstream min-jump-table-entries
; threshold): the compare chain is isomorphic, and the empty default is just
; the epilogue where the non-empty default carries its own volatile store.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

; Empty default: the final miss falls into the bare epilogue (at -O0 the
; spilled frame is popped first).
define void @emptydef(i32 noundef %x) {
; CHECK-LABEL: emptydef:
; CHECK:      mov dr4, #0x0000
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; CHECK:      mov dr4, #0x0001
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; CHECK:      mov dr4, #0x0002
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; CHECK:      mov dr4, #0x0003
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: dec spx, #0x4
; O0-NEXT: eret
; O2: eret
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
  ret void
}

; Non-empty default: identical dispatch chain; the all-misses exit carries
; the default store (99 = 0x63) before returning.
define void @nonemptydef(i32 noundef %x) {
; CHECK-LABEL: nonemptydef:
; CHECK:      mov dr4, #0x0000
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; CHECK:      mov dr4, #0x0001
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; CHECK:      mov dr4, #0x0002
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; CHECK:      mov dr4, #0x0003
; CHECK-NEXT: cmp dr0, dr4
; CHECK-NEXT: jne {{\.LBB[0-9_]+}}
; CHECK-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov r{{[0-9]+}}, #0x63
; O2: eret
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
  store volatile i32 99, ptr @glob, align 1
  ret void
}
