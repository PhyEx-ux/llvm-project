; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NEAR
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NEAR
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,3,1 -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=INVALID-NEAR
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,3,16,8,1 -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=INVALID-VERSION
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,1,1 -filetype=null < %s 2>&1 | FileCheck %s --check-prefix=INVALID-COMPAT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=obj < %s -o %t.rel 2>&1 | FileCheck %s --check-prefix=OBJECT-GATE
; RUN: test ! -s %t.rel
;
; The two legal 16-bit AS0 contracts share one executable DataLayout. Placement
; is an independent numeric policy, while pointer arithmetic and alloca types
; come from p0:16 with a 16-bit index.
;
; INVALID-NEAR: LLVM ERROR: invalid -mcs251-memory-contract
; INVALID-VERSION: LLVM ERROR: invalid -mcs251-memory-contract
; INVALID-COMPAT: LLVM ERROR: invalid -mcs251-memory-contract
; OBJECT-GATE: MCS251 16-bit pointer ABI cannot emit relocatable objects until the v2 ABI attributes and linker compatibility gate are implemented
; NEAR: .mcs251_v2_nonobject
; NEAR-NOT: .optsdcc

define i16 @pointer_size() {
; NEAR-LABEL: _pointer_size:
; NEAR: mov {{wr[0-9]+}}, #0x0002
  %p = getelementptr ptr, ptr null, i16 1
  %n = ptrtoint ptr %p to i16
  ret i16 %n
}
