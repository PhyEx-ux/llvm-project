; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; A constant far-to-near cast is accepted only when the complete value fits in
; the 16-bit near representation; no bank truncation is permitted.
;
; CHECK: LLVM ERROR: MCS251: far-to-near constant address does not fit 16 bits

define ptr @reject_far_constant() {
  %near = addrspacecast ptr addrspace(9) inttoptr (i32 65536 to ptr addrspace(9)) to ptr
  ret ptr %near
}
