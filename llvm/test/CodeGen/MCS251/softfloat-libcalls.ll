; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/ops.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/ops.ll -o - | FileCheck %s --check-prefix=O2
;
; The connected binary32 arithmetic/conversion subset is emitted as helpers.
; The C-level names have one '_' here; MCS251's prefixer supplies the second.
; The binary operands use each helper's four-byte static _PARM_2 slot.
;
; O0-LABEL: _ops:
; O0: __addsf3_PARM_2
; O0: ecall __addsf3
; O0: __subsf3_PARM_2
; O0: ecall __subsf3
; O0: __mulsf3_PARM_2
; O0: ecall __mulsf3
; O0: __divsf3_PARM_2
; O0: ecall __divsf3
; O0: ecall __floatsisf
; O0: ecall __fixsfsi
; O0: eret
;
; O2-LABEL: _ops:
; O2: __addsf3_PARM_2
; O2: ecall __addsf3
; O2: __subsf3_PARM_2
; O2: ecall __subsf3
; O2: __mulsf3_PARM_2
; O2: ecall __mulsf3
; O2: __divsf3_PARM_2
; O2: ecall __divsf3
; O2: ecall __floatsisf
; O2: ecall __fixsfsi
; O2: eret

;--- ops.ll
define i32 @ops(float %a, float %b, i32 %i) {
  %add = fadd float %a, %b
  %sub = fsub float %add, %b
  %mul = fmul float %sub, %a
  %div = fdiv float %mul, %b
  %fromi = sitofp i32 %i to float
  %toi = fptosi float %fromi to i32
  %bits = bitcast float %div to i32
  %sum = add i32 %bits, %toi
  ret i32 %sum
}
