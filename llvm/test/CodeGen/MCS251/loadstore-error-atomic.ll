; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Atomic memory operations are rejected: the MCS-251 has no atomic
; instructions and no interrupt-disabling sequence is modelled.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i8 @atomic_load(ptr %p) {
; CHECK: LLVM ERROR: MCS251: atomic memory operations are not supported
  %v = load atomic i8, ptr %p seq_cst, align 1
  ret i8 %v
}
