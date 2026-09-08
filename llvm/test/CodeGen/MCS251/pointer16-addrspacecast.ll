; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Approved near RAM pointers zero-extend into far RAM containers. Same-width
; explicit near casts preserve the numeric address without changing RAM access
; semantics.

define ptr addrspace(9) @near_to_far(ptr addrspace(8) %p) {
; CHECK-LABEL: _near_to_far:
; CHECK: mov {{r[0-9]+}}, dpl
; CHECK: mov {{r[0-9]+}}, dph
; CHECK: mov b,
; CHECK: mov a,
; CHECK: eret
  %far = addrspacecast ptr addrspace(8) %p to ptr addrspace(9)
  ret ptr addrspace(9) %far
}

define i8 @near_same_width(ptr addrspace(1) %p) {
; CHECK-LABEL: _near_same_width:
; CHECK: mov {{r[0-9]+}}, @{{wr[0-9]+}}
  %edata = addrspacecast ptr addrspace(1) %p to ptr addrspace(8)
  %value = load volatile i8, ptr addrspace(8) %edata, align 1
  ret i8 %value
}

define ptr @constant_far_to_near() {
; CHECK-LABEL: _constant_far_to_near:
; CHECK: mov {{wr[0-9]+}}, #0x1234
; CHECK: mov dpl,
; CHECK: mov dph,
  %near = addrspacecast ptr addrspace(9) inttoptr (i32 4660 to ptr addrspace(9)) to ptr
  ret ptr %near
}
