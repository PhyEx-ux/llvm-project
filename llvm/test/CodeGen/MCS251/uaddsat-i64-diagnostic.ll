; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/dyn-uaddsat.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=I64SAT
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/dyn-uaddsat.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=I64SAT
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/dyn-sqrt.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FSQRT
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/const-uaddsat.ll -o - | FileCheck %s --check-prefix=FOLD
; I64SAT: LLVM ERROR: MCS251 contract violation: i64 intrinsic operation is not yet implemented; wide-integer runtime is not connected
; FSQRT: LLVM ERROR: MCS251 contract violation: f32/f64 intrinsic operation is not yet implemented; soft-float runtime is not connected

; RC-7 (P2): intrinsic rejection diagnostics must classify by operand kind.
; llvm.uadd.sat.i64 is wide-integer arithmetic, so it must report the i64
; wide-integer diagnostic -- before the fix it was lumped into the float
; bucket and reported a bogus "f32/f64 intrinsic operation ... soft-float
; runtime" message. The f32 intrinsic diagnostic is pinned here as the
; contrast case, and the all-constant uadd.sat still folds (7 + 3 = 10).

;--- dyn-uaddsat.ll
declare i64 @llvm.uadd.sat.i64(i64, i64)

define i32 @f(i32 %x) {
  %a = zext i32 %x to i64
  %b = call i64 @llvm.uadd.sat.i64(i64 %a, i64 3)
  %c = trunc i64 %b to i32
  ret i32 %c
}

;--- dyn-sqrt.ll
declare float @llvm.sqrt.f32(float)

define float @h(float %x) {
  %r = call float @llvm.sqrt.f32(float %x)
  ret float %r
}

;--- const-uaddsat.ll
declare i64 @llvm.uadd.sat.i64(i64, i64)

define i32 @g() {
  %b = call i64 @llvm.uadd.sat.i64(i64 7, i64 3)
  %c = trunc i64 %b to i32
  ret i32 %c
}
; FOLD-LABEL: _g:
; FOLD: mov wr0, #0x000a
; FOLD: eret
