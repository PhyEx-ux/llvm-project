; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null %t/as0.ll 2>&1 | FileCheck %s --check-prefix=AS0
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null %t/as3.ll 2>&1 | FileCheck %s --check-prefix=AS3
;
; An explicitly non-CODE function is never a direct ecall target under v2,
; even when its pointer representation is also i32.  The contract verifier
; rejects it before ISel can manufacture a target GlobalAddress.
;
; AS0: LLVM ERROR: MCS251 contract violation: direct call target function is not in the configured CODE address space
; AS3: LLVM ERROR: MCS251 contract violation: direct call target function is not in the configured CODE address space

;--- as0.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

declare void @as0() addrspace(0)

define void @caller_as0() addrspace(4) {
  call addrspace(0) void @as0()
  ret void
}

;--- as3.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

declare void @as3() addrspace(3)

define void @caller_as3() addrspace(4) {
  call addrspace(3) void @as3()
  ret void
}
