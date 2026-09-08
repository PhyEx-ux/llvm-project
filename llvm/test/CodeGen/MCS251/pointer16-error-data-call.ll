; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; AS0 is a 16-bit data pointer in Tiny. A source-level cast cannot turn it into
; a callable AS4 CODE pointer and discard the CODE bank/type contract.
;
; CHECK: LLVM ERROR: MCS251: unsupported address-space cast

define void @reject_data_pointer_call(ptr %data) addrspace(4) {
  %fn = addrspacecast ptr %data to ptr addrspace(4)
  call addrspace(4) void %fn()
  ret void
}
