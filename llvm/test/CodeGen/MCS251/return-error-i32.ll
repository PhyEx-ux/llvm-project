; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; i32 return values are not supported by the minimal backend and must fail
; with a clear error instead of being silently miscompiled.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i32 @ret_i32() {
; CHECK: LLVM ERROR: minimal MCS251 backend only supports i8/i16/void return values
  ret i32 305419896
}
