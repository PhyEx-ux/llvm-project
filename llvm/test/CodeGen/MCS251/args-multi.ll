; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel < %s | FileCheck %s --check-prefix=OBJ
;
; Continuation arguments are passed on the stack below the return address
; (no static `_PARM_` / OSEG overlay slots).
; CHECK-NOT: .area OSEG
; CHECK-NOT: _addw_PARM_2
; CHECK: .area CSEG (CODE)
; CHECK-LABEL: _addw:
; CHECK-DAG: mov {{r[0-9]+}}, dpl
; CHECK-DAG: mov {{r[0-9]+}}, dph
; CHECK: mov {{r[0-9]+}}, @dr60-0x0004
; CHECK: mov {{r[0-9]+}}, @dr60-0x0003
; CHECK: add {{wr[0-9]+}}, {{wr[0-9]+}}
; CHECK: eret
; OBJ: A CSEG
; OBJ: S _addw Def000000
; OBJ-NOT: A OSEG
; OBJ-NOT: _addw_PARM_2

define i16 @addw(i16 %a, i16 %b) {
  %t = add i16 %a, %b
  ret i16 %t
}
