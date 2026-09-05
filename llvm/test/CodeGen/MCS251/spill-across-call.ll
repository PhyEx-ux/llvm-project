; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 9: values live across calls spill to the frame. Every GPR is
; caller-saved in the SDCC MCS-251 ABI, so the first call's result must be
; parked in a frame slot before the second call -- previously a hard error
; ("register spilling is not implemented"), now a normal @dr60 access.

define i8 @sum2() {
; CHECK-LABEL: _sum2:
; CHECK: inc spx, #0x1
; CHECK: ecall _f8a
; CHECK: mov @dr60, r{{[0-9]+}}
; CHECK: ecall _f8a
; CHECK: mov r{{[0-9]+}}, @dr60
; CHECK: add
; CHECK: dec spx, #0x1
; CHECK: eret
entry:
  %x = call i8 @f8a()
  %y = call i8 @f8a()
  %z = add i8 %x, %y
  ret i8 %z
}

declare i8 @f8a()
declare i16 @f16a()

; An i16 value live across a call spills as ONE word-granular access pair
; (mov @dr60+dis,wr / mov wr,@dr60+dis) -- the single-instruction WR
; displaced form, never two byte accesses.
define i16 @sum16() {
; CHECK-LABEL: _sum16:
; CHECK: inc spx, #0x2
; CHECK: ecall _f16a
; CHECK: mov @dr60-0x0001, wr{{[0-9]+}}
; CHECK: ecall _f16a
; CHECK: mov wr{{[0-9]+}}, @dr60-0x0001
; CHECK: add
; CHECK: dec spx, #0x2
; CHECK: eret
entry:
  %x = call i16 @f16a()
  %y = call i16 @f16a()
  %z = add i16 %x, %y
  ret i16 %z
}

; Register pressure: three live values across interleaved calls reuse the
; frame slot (spill/reload between the calls, two adds at the end).
define i16 @pressure() {
; CHECK-LABEL: _pressure:
; CHECK: ecall _f16a
; CHECK: mov @dr60-0x0001, wr{{[0-9]+}}
; CHECK: ecall _f16a
; CHECK: mov wr{{[0-9]+}}, @dr60-0x0001
; CHECK: add
; CHECK: ecall _f16a
; CHECK: mov wr{{[0-9]+}}, @dr60-0x0001
; CHECK: add
; CHECK: eret
entry:
  %x = call i16 @f16a()
  %y = call i16 @f16a()
  %z = call i16 @f16a()
  %s1 = add i16 %x, %y
  %s2 = add i16 %s1, %z
  ret i16 %s2
}
