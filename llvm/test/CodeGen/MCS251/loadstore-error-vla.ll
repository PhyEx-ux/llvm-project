; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Variable-length allocas (VLA) lower to DYNAMIC_STACKALLOC, which needs
; stack manipulation that arrives with Phase 9. Separate file from
; loadstore-error-alloca.ll (static alloca) because a fatal error stops the
; run at the first offender.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i8 @vla_load(i8 %n) {
; CHECK: LLVM ERROR: MCS251: variable-length allocas need the stack (arrives with Phase 9)
  %buf = alloca i8, i8 %n
  %v = load i8, ptr %buf
  ret i8 %v
}
