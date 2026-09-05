; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 12, Step 1: llc emits complete ASxxxx (sdas251) dialect modules.
; The module header must identify the compilation unit the way SDCC does and
; every ELF-flavored directive must be absent: sdas251 rejects .text,
; .section, .p2align, .align, .type, .size, .ident, .file, ...
; The module name is the sanitized stem of the source file name.

source_filename = "asxxxx-header.c"

define void @sample_fn(i8 %a) {
entry:
  %t = icmp eq i8 %a, 1
  br i1 %t, label %yes, label %no

yes:
  ret void

no:
  ret void
}

; CHECK-NOT: {{^[[:space:]]*\.(text|section|p2align|align|type|size|ident|space|long|short|quad|zero|fill|file|weak|end)([[:space:]]|$)}}
; CHECK: .module asxxxx_header
; CHECK: .source
; CHECK: .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
; CHECK: .area CSEG (CODE)
; CHECK-NOT: {{^[[:space:]]*\.(text|section|p2align|align|type|size|ident|space|long|short|quad|zero|fill|file|weak|end)([[:space:]]|$)}}
; CHECK: .globl _sample_fn
; CHECK: {{^}}_sample_fn:
; CHECK: .LBB0_
; CHECK: eret
; CHECK-NOT: {{^[[:space:]]*\.(text|section|p2align|align|type|size|ident|space|long|short|quad|zero|fill|file|weak|end)([[:space:]]|$)}}
