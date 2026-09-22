; G11-B R1 (review 2026-09-16 §一): the `.mcu.fixed.*` namespace is reserved
; for entities REGISTERED by the placement emitters. A function that reaches
; emission through the ordinary EXPLICIT section path
; (__attribute__((section(".mcu.fixed.fixed"))) / IR `section "...")`) enters
; the same section as the placed entity WITHOUT any NOTE record: two STT_FUNC
; symbols share one section and the span of one entity swallows the other's
; bytes. The fail-closed guard at the emission entry of every function
; rejects it regardless of emission order (see ...-order.ll for the reversed
; order and ...-zero-intruder.ll for the zero-size second FUNC variant).
;
; This is the review's repro shape: clang accepts the C source, llc used to
; accept the object (both exited 0 with _other at offset 1 of a 2-byte
; .mcu.fixed.fixed and NOTE.size=1 for _fixed).
;
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o /dev/null 2>&1 | FileCheck %s
; CHECK: LLVM ERROR: MCS251: function 'other' explicitly assigns fixed placement section '.mcu.fixed.fixed': .mcu.fixed.* sections are reserved for mcs251-place entities

target triple = "mcs251-unknown-none"

define void @fixed() addrspace(4) #0 {
  ret void
}

define void @other() addrspace(4) section ".mcu.fixed.fixed" {
  ret void
}

!mcs251.signatures = !{!0, !1}
!0 = !{!"_fixed", i32 1, i32 0}
!1 = !{!"_other", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0xFC3000,code,function,owned,0" "mcs251-stable-symbol"="fixed" }
