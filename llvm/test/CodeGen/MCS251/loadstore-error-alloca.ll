; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Phase 8: stack (alloca) objects are rejected -- the frame arrives with
; Phase 9. A static alloca in the entry block reaches the load/store
; lowering as a frame-index pointer. (report_fatal_error aborts, hence
; --crash; lit pipelines are pipefail.)

define i8 @alloca_load() {
; CHECK: LLVM ERROR: MCS251: stack objects (alloca) require the frame, which arrives with Phase 9
  %buf = alloca i8
  store i8 1, ptr %buf
  %v = load i8, ptr %buf
  ret i8 %v
}
