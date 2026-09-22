; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; Width alone cannot make an AS0 data address callable in the v2 contract. The
; compatibility ABI uses program AS0 and is covered by call-error-indirect.ll;
; v2 uses AS4 and rejects this same-width AS0 callee before selecting ECALLr.
;
; CHECK: LLVM ERROR: MCS251: indirect call target is not in the configured CODE address space

define void @reject_as0_data_call(ptr %data) addrspace(4) {
  call addrspace(0) void %data()
  ret void
}
