; G7 S1' (PM ruling 2026-09-15, G7-FLOAT-DESIGN-draft.md §6 D1): the unsigned
; i32 <-> f32 pair is connected alongside the signed pair, and the narrow
; (i8/i16) integer forms are accepted WITHOUT a narrow helper of their own:
; the generic soft-float legalizer zero/sign-extends the source to i32 before
; the call (SoftenFloatRes_XINT_TO_FP) and truncates the i32 result back
; (findFPToIntLibcall + TRUNCATE).  So every integer width must emit the i32
; helper name and never a width-suffixed variant.
;
; The signed pair keeps its own helper names, proving the two families do not
; alias each other on the same payload width.
;
; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/conv.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/conv.ll -o - | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -mcs251-object-format=elf -filetype=obj %t/obj.ll -o %t.o
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=SYM
; RUN: llvm-strings %t.o | FileCheck %s --check-prefix=TAG28

; The one-argument helpers are recorded with the registered helper ABI: an UND
; global per symbol, and the name embedded in the Tag 28 signature payload.
; SYM: Name: __floatunsisf
; SYM: Name: __fixunssfsi
; TAG28-DAG: __floatunsisf
; TAG28-DAG: __fixunssfsi

; O0-LABEL: _u2f_i8:
; O0: ecall __floatunsisf
; O0: eret
; O0-LABEL: _u2f_i16:
; O0: ecall __floatunsisf
; O0: eret
; O0-LABEL: _u2f_i32:
; O0: ecall __floatunsisf
; O0: eret
; O0-LABEL: _f2u_i8:
; O0: ecall __fixunssfsi
; O0: eret
; O0-LABEL: _f2u_i16:
; O0: ecall __fixunssfsi
; O0: eret
; O0-LABEL: _f2u_i32:
; O0: ecall __fixunssfsi
; O0: eret
; O0-LABEL: _s2f_i32:
; O0: ecall __floatsisf
; O0: eret
; O0-LABEL: _f2s_i32:
; O0: ecall __fixsfsi
; O0: eret

; O2-LABEL: _u2f_i8:
; O2: ecall __floatunsisf
; O2: eret
; O2-LABEL: _u2f_i16:
; O2: ecall __floatunsisf
; O2: eret
; O2-LABEL: _u2f_i32:
; O2: ecall __floatunsisf
; O2: eret
; O2-LABEL: _f2u_i8:
; O2: ecall __fixunssfsi
; O2: eret
; O2-LABEL: _f2u_i16:
; O2: ecall __fixunssfsi
; O2: eret
; O2-LABEL: _f2u_i32:
; O2: ecall __fixunssfsi
; O2: eret
; O2-LABEL: _s2f_i32:
; O2: ecall __floatsisf
; O2: eret
; O2-LABEL: _f2s_i32:
; O2: ecall __fixsfsi
; O2: eret

;--- conv.ll
; The unsigned family must never be reached through a signed helper: the
; operands are non-constant so no folding can erase the conversion.
define float @u2f_i8(i8 %a) {
  %r = uitofp i8 %a to float
  ret float %r
}

define float @u2f_i16(i16 %a) {
  %r = uitofp i16 %a to float
  ret float %r
}

define float @u2f_i32(i32 %a) {
  %r = uitofp i32 %a to float
  ret float %r
}

define i8 @f2u_i8(float %a) {
  %r = fptoui float %a to i8
  ret i8 %r
}

define i16 @f2u_i16(float %a) {
  %r = fptoui float %a to i16
  ret i16 %r
}

define i32 @f2u_i32(float %a) {
  %r = fptoui float %a to i32
  ret i32 %r
}

define float @s2f_i32(i32 %a) {
  %r = sitofp i32 %a to float
  ret float %r
}

define i32 @f2s_i32(float %a) {
  %r = fptosi float %a to i32
  ret i32 %r
}

;--- obj.ll
; A minimal v2 module that exercises both unsigned helpers, so the object
; carries both signature records.  The ABI call generation is 2.1 (the
; registered v2 identity), matching the metadata table below.
define i32 @uns_both(i32 %x) addrspace(4) {
  %f = uitofp i32 %x to float
  %r = fptoui float %f to i32
  ret i32 %r
}

!mcs251.signatures = !{!0}
!0 = !{!"_uns_both", i32 9, i32 0, i32 0}
