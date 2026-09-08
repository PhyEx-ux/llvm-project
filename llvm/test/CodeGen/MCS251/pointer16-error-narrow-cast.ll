; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; Dynamic far-to-near truncation is not a dereferenceable pointer conversion.
; It needs the separately specified checked-conversion interface.
;
; CHECK: LLVM ERROR: MCS251: dynamic far-to-near address-space cast requires an explicit checked conversion

define ptr @reject_dynamic_narrow(ptr addrspace(9) %p) {
  %near = addrspacecast ptr addrspace(9) %p to ptr
  ret ptr %near
}
