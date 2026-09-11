; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s
;
; X2: AS4 (CODE) stores stay fail-closed. CODE is read-only by the space's
; definition (DESIGN B.1); the 24-bit DR channel implemented for AS4 loads is
; deliberately not given a write form. (report_fatal_error aborts, hence
; --crash; lit pipelines are pipefail.)

define void @st4(ptr addrspace(4) %p, i8 %v) {
; CHECK: LLVM ERROR: MCS251: store to CODE (address space 4) is not permitted; CODE is read-only
  store i8 %v, ptr addrspace(4) %p
  ret void
}
