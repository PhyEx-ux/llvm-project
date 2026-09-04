; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Phase 8: i32 memory objects are rejected. i32 is a legal type in this
; backend (GPR32, so the type legalizer cannot silently split the access)
; but no instruction loads 32 bits.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

@g32 = global i32 1

define void @load32() {
; CHECK: LLVM ERROR: MCS251: only i8/i16 memory objects are supported (load)
  %v = load volatile i32, ptr @g32
  ret void
}
