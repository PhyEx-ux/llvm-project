; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 9 + CSR: values live across calls are preserved either in callee-saved
; registers (WR8 / DR12, saved in the prologue and restored in the epilogue) or
; spilled to the frame when CSR capacity is exhausted.

define i8 @sum2() {
; CHECK-LABEL: _sum2:
; CHECK: inc spx, #0x2
; CHECK: mov @dr60-0x0001, wr8
; CHECK: ecall _f8a
; CHECK: mov r8, dpl
; CHECK: ecall _f8a
; CHECK: add
; CHECK: mov wr8, @dr60-0x0001
; CHECK: dec spx, #0x2
; CHECK: eret
entry:
  %x = call i8 @f8a()
  %y = call i8 @f8a()
  %z = add i8 %x, %y
  ret i8 %z
}

declare i8 @f8a()
declare i16 @f16a()

; An i16 value live across a call stays in callee-saved WR8, whose prologue
; save/restore uses ONE word-granular access pair (`mov @dr60-0x0001, wr8` /
; `mov wr8, @dr60-0x0001`).
define i16 @sum16() {
; CHECK-LABEL: _sum16:
; CHECK: inc spx, #0x2
; CHECK: mov @dr60-0x0001, wr8
; CHECK: ecall _f16a
; CHECK: ecall _f16a
; CHECK: add
; CHECK: mov wr8, @dr60-0x0001
; CHECK: dec spx, #0x2
; CHECK: eret
entry:
  %x = call i16 @f16a()
  %y = call i16 @f16a()
  %z = add i16 %x, %y
  ret i16 %z
}

; Register pressure: three live values across interleaved calls use both CSRs
; (WR8 and DR12's WR12/WR14 halves) saved in the prologue and restored in the
; epilogue.
define i16 @pressure() {
; CHECK-LABEL: _pressure:
; CHECK: mov @dr60-0x0003, wr12
; CHECK: mov @dr60-0x0001, wr14
; CHECK: mov @dr60-0x0005, wr8
; CHECK: ecall _f16a
; CHECK: ecall _f16a
; CHECK: add
; CHECK: ecall _f16a
; CHECK: add
; CHECK: mov wr8, @dr60-0x0005
; CHECK: mov wr12, @dr60-0x0003
; CHECK: mov wr14, @dr60-0x0001
; CHECK: eret
entry:
  %x = call i16 @f16a()
  %y = call i16 @f16a()
  %z = call i16 @f16a()
  %s1 = add i16 %x, %y
  %s2 = add i16 %s1, %z
  ret i16 %s2
}
