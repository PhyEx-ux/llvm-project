; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; The ecall/eret ABI keeps no frame of its own (no return-address bookkeeping
; beyond the hardware stack), so a tail call -- replacing the caller's own
; return -- has no lowering here and is rejected explicitly.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

declare void @pv()

define void @tc() {
; CHECK: LLVM ERROR: MCS251: tail calls are not supported
  musttail call void @pv()
  ret void
}
