; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s -o - | FileCheck %s --check-prefix=O2
;
; FNEG is intentionally an ABI helper call, never the generic sign-bit XOR.
;
; O0-LABEL: _neg:
; O0-NOT: xrl
; O0: ecall __negsf2
; O0: eret
;
; O2-LABEL: _neg:
; O2-NOT: xrl
; O2: ecall __negsf2
; O2: eret

define float @neg(float %a) {
  %r = fneg float %a
  ret float %r
}
