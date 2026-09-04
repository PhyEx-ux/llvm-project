; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Store side of loadstore-error-i32-load.ll: a separate file because a
; fatal error stops the run at the first offender.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define void @store32() {
; CHECK: LLVM ERROR: MCS251: only i8/i16 memory objects are supported (store)
  store volatile i32 305419896, ptr inttoptr (i16 48 to ptr)
  ret void
}
