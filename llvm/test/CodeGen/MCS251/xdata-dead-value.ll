; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
;
; Alice's dead-value probe, promoted to a permanent lit test (X2 follow-up):
; the chain-threaded MOVX sequence must (1) survive for a volatile load whose
; value is unused, (2) disappear entirely with a dead NON-volatile load --
; no orphan DPXL/dptr setup may remain, and (3) issue one full sequence per
; volatile access when the same location is read twice.

define void @dead(ptr addrspace(3) %p) {
; CHECK-LABEL: _dead:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NOT:     movx
  %v = load volatile i8, ptr addrspace(3) %p
  ret void
}

define void @dead_nonvolatile(ptr addrspace(3) %p) {
; CHECK-LABEL: _dead_nonvolatile:
; CHECK-NOT:     mov 0x84
; CHECK-NOT:     movx
; CHECK:         eret
  %v = load i8, ptr addrspace(3) %p
  ret void
}

define i8 @twice(ptr addrspace(3) %p) {
; CHECK-LABEL: _twice:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
  %v = load volatile i8, ptr addrspace(3) %p
  %w = load volatile i8, ptr addrspace(3) %p
  %x = xor i8 %v, %w
  ret i8 %x
}
