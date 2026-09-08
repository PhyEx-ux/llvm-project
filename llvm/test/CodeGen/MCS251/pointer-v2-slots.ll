; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj < %s 2>&1 | FileCheck %s --check-prefix=OBJECT-GATE
;
; OBJECT-GATE: LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity
;
; Static pointer slots are a v2 extension. Their payload width follows the full
; pointer type, not the physical region where the slot or pointee is placed.

define void @default_pointer_slot(i8 %tag, ptr %p) {
; CHECK: .globl _default_pointer_slot_PARM_2
; CHECK: _default_pointer_slot_PARM_2:
; CHECK-NEXT: .ds 4
; CHECK-LABEL: _default_pointer_slot:
  ret void
}

define void @near_pointer_slot(i8 %tag, ptr addrspace(8) %p) {
; CHECK: .globl _near_pointer_slot_PARM_2
; CHECK: _near_pointer_slot_PARM_2:
; CHECK-NEXT: .ds 2
; CHECK-LABEL: _near_pointer_slot:
  ret void
}
