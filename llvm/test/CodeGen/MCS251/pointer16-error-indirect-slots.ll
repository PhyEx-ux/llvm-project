; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; Opening named 2-byte pointer slots does not invent a slot owner for an
; indirect multi-argument call.
;
; CHECK: LLVM ERROR: MCS251 contract violation: multi-argument indirect calls are not supported (static parameter slots require a named callee); call the function directly, or pass at most one argument through the function pointer

define void @reject_indirect_slots(ptr addrspace(4) %fn) addrspace(4) {
  call addrspace(4) void %fn(i8 1, ptr null)
  ret void
}
