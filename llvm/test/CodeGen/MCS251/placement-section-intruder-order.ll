; G11-B R1 (review 2026-09-16 §一): ORDER-INDEPENDENCE of the fixed-section
; guard, and the OBJECT face. Part A emits the intruding function BEFORE the
; placed function (functions are emitted in module order), part B drives an
; unplaced global object into the namespace through the explicit-section
; path (today's generic custom-section rejection fires only later, with a
; wording that hides the placement contract violation; the guard reports the
; real cause at the emission entry). Neither check consults registration
; state, so the intruder is rejected whether it is emitted before or after
; (or instead of) any placed entity.
;
; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/fn-first.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FNFIRST
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/obj-intruder.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OBJ
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/obj-only.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OBJ
;
; The lone intruder (no placed entity in the module at all) is rejected too:
; the guard is a namespace rule, not a collision rule.
; FNFIRST: LLVM ERROR: MCS251: function 'other' explicitly assigns fixed placement section '.mcu.fixed.fixed': .mcu.fixed.* sections are reserved for mcs251-place entities
; OBJ: LLVM ERROR: MCS251: global 'og' explicitly assigns fixed placement section '.mcu.fixed.og': .mcu.fixed.* sections are reserved for mcs251-place entities

;--- fn-first.ll
target triple = "mcs251-unknown-none"

define void @other() addrspace(4) section ".mcu.fixed.fixed" {
  ret void
}

define void @fixed() addrspace(4) #0 {
  ret void
}

!mcs251.signatures = !{!0, !1}
!0 = !{!"_fixed", i32 1, i32 0}
!1 = !{!"_other", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0xFC3000,code,function,owned,0" "mcs251-stable-symbol"="fixed" }

;--- obj-intruder.ll
target triple = "mcs251-unknown-none"

@og = global i32 7, section ".mcu.fixed.og"
@pg = global i32 9, align 1 #0

!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0x60,data,object,owned,0" "mcs251-stable-symbol"="pg" }

;--- obj-only.ll
target triple = "mcs251-unknown-none"

@og = global i32 7, section ".mcu.fixed.og"

!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 1, i32 0}
