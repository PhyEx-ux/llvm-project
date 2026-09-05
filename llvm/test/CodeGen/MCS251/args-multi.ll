; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj < %s | FileCheck %s --check-prefix=OBJ
;
; Second arguments now have a static slot, not the old fatal rejection.
; CHECK: .area REG_BANK_0 (OVR,DATA)
; CHECK-NEXT: .ds 8
; CHECK: .area OSEG (OVR,DATA)
; CHECK-NEXT: .globl _addw_PARM_2
; CHECK-NEXT: _addw_PARM_2:
; CHECK-NEXT: .ds 2
; CHECK-NEXT: .area CSEG (CODE)
; CHECK-LABEL: _addw:
; CHECK-DAG: mov {{r[0-9]+}}, dpl
; CHECK-DAG: mov {{r[0-9]+}}, dph
; CHECK: (_addw_PARM_2) >> 8, (_addw_PARM_2)
; CHECK: mov {{r[0-9]+}}, @{{dr[0-9]+}}{{$}}
; CHECK: mov {{r[0-9]+}}, @{{dr[0-9]+}}+0x0001
; CHECK: add {{wr[0-9]+}}, {{wr[0-9]+}}
; CHECK: eret
; OBJ: A OSEG size 2 flags 4 addr 0
; OBJ-NEXT: S _addw_PARM_2 Def000000
; OBJ-NEXT: A REG_BANK_0 size 8 flags 4 addr 0

define i16 @addw(i16 %a, i16 %b) {
  %t = add i16 %a, %b
  ret i16 %t
}
