; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/abi.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/abi.ll -o - | FileCheck %s --check-prefix=O2
;
; The first f32 argument/result is the i32 payload in DPL:DPH:B:A.  A second
; f32 argument is passed on the stack (4 bytes, big-endian, no static _PARM_2).
;
; O0-NOT: _take2_PARM_2
; O0-LABEL: _take2:
; O0: mov {{r[0-9]+}}, @dr60-0x0006
; O0: eret
; O0-LABEL: _call_add:
; O0: inc spx, #0x4
; O0: mov dpl,
; O0: mov dph,
; O0: mov b,
; O0: mov a,
; O0: ecall _take2
; O0: dec spx, #0x4
; O0: eret
;
; O2-NOT: _take2_PARM_2
; O2-LABEL: _take2:
; O2: mov {{r[0-9]+}}, @dr60-0x0006
; O2: eret
; O2-LABEL: _call_add:
; O2: inc spx, #0x4
; O2: mov dpl,
; O2: mov dph,
; O2: mov b,
; O2: mov a,
; O2: ecall _take2
; O2: dec spx, #0x4
; O2: eret

;--- abi.ll
define float @take2(float %a, float %b) noinline {
  ret float %b
}

define float @call_add(float %a, float %b) {
  %r = call float @take2(float %a, float %b)
  ret float %r
}
