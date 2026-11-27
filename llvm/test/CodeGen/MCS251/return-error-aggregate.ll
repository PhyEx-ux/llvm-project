; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Multi-value aggregate returns are automatically demoted via hidden sret
; pointer (CanLowerReturn returns false for Outs.size() > 1).

define { i8, i8 } @ret_aggregate() {
; CHECK-LABEL: _ret_aggregate:
; CHECK-DAG: mov r{{[0-9]+}}, a
; CHECK-DAG: mov r{{[0-9]+}}, b
; CHECK-DAG: mov r{{[0-9]+}}, dph
; CHECK-DAG: mov r{{[0-9]+}}, dpl
; CHECK-DAG: mov @dr0+0x0001, r{{[0-9]+}}
; CHECK-DAG: mov @dr0, r{{[0-9]+}}
; CHECK: eret
  ret { i8, i8 } { i8 1, i8 2 }
}
