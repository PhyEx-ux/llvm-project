; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 4: single-argument ABI, measured on hardware (SDCC 4.6.0 mcs251 +
; sdas251 V05.50.4 + QEMU stc32g144k246): the first i8 argument arrives in
; dpl and the first i16 argument in the dpl:dph pair (dpl = low byte), the
; same locations the return-value ABI uses. Functions with two or more
; arguments are rejected by a separate crash test (args-error-multi.ll).

define void @no_args() {
; CHECK-LABEL: no_args:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    eret
; CHECK-NEXT:  ; -- End function
  ret void
}

define i8 @id8(i8 %a) {
; The argument is live-in in dpl and returned in dpl. The value therefore
; passes straight through and the register coalescer removes the whole
; phys->virt->phys copy chain (dpl is reserved, so coalescing into it is
; always legal): an empty body is the correct and optimal lowering.
; CHECK-LABEL: id8:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    eret
; CHECK-NEXT:  ; -- End function
  ret i8 %a
}

define i16 @id16(i16 %a) {
; Same dptr pass-through as id8: nothing survives coalescing.
; CHECK-LABEL: id16:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    eret
; CHECK-NEXT:  ; -- End function
  ret i16 %a
}

define i16 @inc16(i16 %a) {
; CHECK-LABEL: inc16:
; CHECK:         mov r{{[0-9]+}}, dpl
; CHECK:         mov r{{[0-9]+}}, dph
; CHECK:         add wr{{[0-9]+}}, #0x0001
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         mov dph, r{{[0-9]+}}
; CHECK:         eret
  %t = add i16 %a, 1
  ret i16 %t
}

define i8 @mask8(i8 %a) {
; CHECK-LABEL: mask8:
; CHECK:         mov [[R:r[0-9]+]], dpl
; CHECK:         anl [[R]], #0x0f
; CHECK:         mov dpl, [[R]]
; CHECK:         eret
  %t = and i8 %a, 15
  ret i8 %t
}
