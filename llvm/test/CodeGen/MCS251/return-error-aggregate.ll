; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Aggregate return values are not supported by the minimal backend.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define { i8, i8 } @ret_aggregate() {
; CHECK: LLVM ERROR: minimal MCS251 backend only supports zero or one i8/i16/i32 return value; multi-value returns are not supported
  ret { i8, i8 } { i8 1, i8 2 }
}
