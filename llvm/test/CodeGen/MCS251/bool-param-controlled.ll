; WP5 A1: the `_Bool` parameter admission is a controlled widening. Exactly
; i1 joins the accepted scalar set (as the VT = i8 / ArgVT = i1 legalized
; pair); every other sub-byte or non-{8,16,32} integer width stays rejected by
; the same `checkParameterType`/`checkParameter` gates, and the marker checks
; (PartOffset/Split/ByVal/...) are untouched. This test pins that boundary so
; a later "accept any width" change cannot slip through silently.
;
; Pinned to the v1 compatibility contract, matching oseg-errors.ll.

; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i1.ll
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i2.ll 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i3.ll 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i4.ll 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i7.ll 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i24.ll 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t/i64.ll 2>&1 | FileCheck %s --check-prefix=TYPE
;
; TYPE: LLVM ERROR: MCS251: arguments must be unsplit i8/i16/i32 scalars

;--- i1.ll
; The one newly accepted width: an i1 formal parameter lowers without error.
target triple = "mcs251"
define void @f(i1 zeroext %b) {
  ret void
}

;--- i2.ll
target triple = "mcs251"
define void @f(i2 zeroext %b) {
  ret void
}

;--- i3.ll
target triple = "mcs251"
define void @f(i3 zeroext %b) {
  ret void
}

;--- i4.ll
target triple = "mcs251"
define void @f(i4 zeroext %b) {
  ret void
}

;--- i7.ll
target triple = "mcs251"
define void @f(i7 zeroext %b) {
  ret void
}

;--- i24.ll
target triple = "mcs251"
define void @f(i24 zeroext %b) {
  ret void
}

;--- i64.ll
target triple = "mcs251"
define void @f(i64 zeroext %b) {
  ret void
}
