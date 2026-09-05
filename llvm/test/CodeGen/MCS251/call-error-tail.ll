; RUN: not llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -filetype=obj < %s 2>&1 | FileCheck %s

; Unlike optional tail hints, musttail cannot fall back to ecall/eret.
; Reject it explicitly with exit 1 rather than a compiler crash.

declare void @pv()

define void @tc() {
; CHECK: LLVM ERROR: MCS251: musttail calls are not supported
  musttail call void @pv()
  ret void
}
