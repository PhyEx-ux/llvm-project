; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -verify-machineinstrs < %s | FileCheck %s
;
; Under the stack-based reentrant ABI, multi-argument indirect calls with
; 16-bit near pointers pass continuation arguments on the stack.
;
; CHECK-LABEL: _reject_indirect_slots:
; CHECK: inc spx, #0x2
; CHECK: mov @dr60-0x0001,
; CHECK: mov @dr60,
; CHECK: ecall @dr{{[0-9]+}}
; CHECK: dec spx, #0x2
; CHECK: eret

define void @reject_indirect_slots(ptr addrspace(4) %fn) addrspace(4) {
  call addrspace(4) void %fn(i8 1, ptr null)
  ret void
}
