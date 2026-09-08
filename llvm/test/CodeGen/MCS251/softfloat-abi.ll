; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/abi.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/abi.ll -o - | FileCheck %s --check-prefix=O2
;
; The first f32 argument/result is the i32 payload in DPL:DPH:B:A.  A second
; f32 argument uses a four-byte big-endian static slot; only the conventional
; target prefix is added to the C-level function name.
;
; O0: .globl _take2_PARM_2
; O0: _take2_PARM_2:
; O0: .ds 4
; O0-LABEL: _call_add:
; O0: (_take2_PARM_2) >> 8, (_take2_PARM_2)
; O0: mov dpl,
; O0: mov dph,
; O0: mov b,
; O0: mov a,
; O0: ecall _take2
; O0: eret
;
; O2: .globl _take2_PARM_2
; O2: _take2_PARM_2:
; O2: .ds 4
; O2-LABEL: _call_add:
; O2: (_take2_PARM_2) >> 8, (_take2_PARM_2)
; O2: mov dpl,
; O2: mov dph,
; O2: mov b,
; O2: mov a,
; O2: ecall _take2
; O2: eret

;--- abi.ll
define float @take2(float %a, float %b) noinline {
  ret float %b
}

define float @call_add(float %a, float %b) {
  %r = call float @take2(float %a, float %b)
  ret float %r
}
