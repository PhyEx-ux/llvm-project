; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; AS6 is SFR direct-byte state, not an ordinary near data pointer ABI.
;
; CHECK: LLVM ERROR: MCS251: pointer parameter address space has no ordinary register/static-slot ABI

define void @reject_sfr_parameter(ptr addrspace(6) %p) {
  ret void
}
