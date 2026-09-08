; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/cmp.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/cmp.ll -o - | FileCheck %s --check-prefix=O2
;
; SETCC must use TargetLowering::softenSetCCOperands, rather than a target
; SETCC LibCall action.  The generic predicate decomposition chooses the
; connected compare helpers and their static slots.
;
; O0-LABEL: _predicates:
; O0: __eqsf2_PARM_2
; O0: ecall __eqsf2
; O0: __lesf2_PARM_2
; O0: ecall __lesf2
; O0: __gtsf2_PARM_2
; O0: ecall __gtsf2
; O0: __unordsf2_PARM_2
; O0: ecall __unordsf2
; O0: eret
;
; O2-LABEL: _predicates:
; O2: __eqsf2_PARM_2
; O2: ecall __eqsf2
; O2: __gtsf2_PARM_2
; O2: ecall __gtsf2
; O2: __unordsf2_PARM_2
; O2: ecall __unordsf2
; O2: __lesf2_PARM_2
; O2: ecall __lesf2
; O2: eret

;--- cmp.ll
define i32 @predicates(float %a, float %b) {
  %eq = fcmp oeq float %a, %b
  %le = fcmp ole float %a, %b
  %gt = fcmp ogt float %a, %b
  %uno = fcmp uno float %a, %b
  %x = zext i1 %eq to i32
  %y = zext i1 %le to i32
  %z = zext i1 %gt to i32
  %u = zext i1 %uno to i32
  %xy = add i32 %x, %y
  %zu = add i32 %z, %u
  %r = add i32 %xy, %zu
  ret i32 %r
}
