; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -disable-verify -filetype=null %t/declaration.ll 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -disable-verify -filetype=null %t/alloca.ll 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -disable-verify -filetype=null %t/load-payload.ll 2>&1 | FileCheck %s
;
; The target verifier is a required pre-ISel pass, independent of LLVM's
; generic verifier. It traverses declarations, instruction result types and
; pointer payload types instead of waiting for an eventual memory use.
;
; CHECK: LLVM ERROR: MCS251 contract violation: unsupported pointer address space 5

;--- declaration.ll
@bad = external addrspace(5) global i8

define void @declaration_anchor() addrspace(4) {
  ret void
}

;--- alloca.ll
define void @bad_alloca() addrspace(4) {
  %p = alloca i8, addrspace(5)
  ret void
}

;--- load-payload.ll
define ptr addrspace(5) @bad_payload(ptr %slot) addrspace(4) {
  %p = load ptr addrspace(5), ptr %slot, align 1
  ret ptr addrspace(5) %p
}
