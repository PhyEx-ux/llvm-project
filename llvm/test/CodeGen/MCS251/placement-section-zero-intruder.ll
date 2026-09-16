; G11-B R1 (review 2026-09-16 §一): the ZERO-SIZE second FUNC variant. An
; unreachable body selects to zero instructions, so the intruder emits no
; bytes -- but it is still an unregistered STT_FUNC defined in the placed
; entity's section. NOTE.size and sh_size both stay 1, so no size comparison
; can detect it; only the emission-entry registration check can (design rev
; 6 froze exactly this: "零尺寸 FUNC/OBJECT 也必须计数").
;
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o /dev/null 2>&1 | FileCheck %s
; CHECK: LLVM ERROR: MCS251: function 'other' explicitly assigns fixed placement section '.mcu.fixed.fixed': .mcu.fixed.* sections are reserved for mcs251-place entities

target triple = "mcs251-unknown-none"

define void @fixed() addrspace(4) #0 {
  ret void
}

define void @other() addrspace(4) section ".mcu.fixed.fixed" {
  unreachable
}

!mcs251.signatures = !{!0, !1}
!0 = !{!"_fixed", i32 1, i32 0}
!1 = !{!"_other", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0xFC3000,code,function,owned,0" "mcs251-stable-symbol"="fixed" }
