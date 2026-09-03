; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; The ecall encoding takes a symbolic target address (resolved by the
; linker); calling through a register (function pointer) would need an
; indirect-call encoding that this phase does not model. Rejected explicitly.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define void @caller(ptr %f) {
; CHECK: LLVM ERROR: MCS251: indirect calls (function pointers) are not supported
  call void %f()
  ret void
}
