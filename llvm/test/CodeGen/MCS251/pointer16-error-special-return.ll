; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; AS6 direct-byte access does not imply a generic pointer return ABI.
;
; CHECK: LLVM ERROR: MCS251: pointer return address space has no ordinary ABI

define ptr addrspace(6) @reject_sfr_return() {
  ret ptr addrspace(6) null
}
