; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Variadic functions are rejected by the minimal backend (same underlying
; limitation: extra arguments would need the SDCC static OSEG overlay slots).
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i8 @varargs(i8 %a, ...) {
; CHECK: LLVM ERROR: minimal MCS251 backend does not support variadic functions
  ret i8 %a
}
