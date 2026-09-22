; G11-B: the emitter-side single-entity invariant of a `.mcu.fixed.*`
; section (design rev 7 §3.2, "each fixed entity gets a dedicated section"):
; a second placed entity resolving to an already-claimed fixed-section name
; is an internal invariant violation and fails loudly with the frozen
; "section disagrees with placement NOTE" wording instead of letting one
; entity's span swallow the other's bytes (the anti-merge guard; the
; linker-side count check is G11-C's loadFile/mergePlacement face).
;
; Here two distinct globals carry the SAME stable symbol "clash" (only a
; malformed producer or a hand-edited IR can build this; Sema's
; cross-TU/stable-symbol rules reject it at the source level).

; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o /dev/null 2>&1 | FileCheck %s
; CHECK: LLVM ERROR: MCS251: section .mcu.fixed.clash disagrees with placement NOTE for _b

target triple = "mcs251-unknown-none"

@a = global i32 1, align 1 #0
@b = global i32 2, align 1 #0

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 2, i32 0}

attributes #0 = { "mcs251-place"="0x80,data,object,owned,0" "mcs251-stable-symbol"="clash" }
