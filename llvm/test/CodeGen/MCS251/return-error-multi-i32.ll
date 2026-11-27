; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Multi-value returns are automatically demoted via hidden sret pointer
; in DPL:DPH:B:A (`dr0`) by SelectionDAG when CanLowerReturn returns false.

define { i32, i32 } @two_i32() {
; CHECK-LABEL: _two_i32:
; CHECK-DAG: mov @dr0+0x0003, r{{[0-9]+}}
; CHECK-DAG: mov @dr0+0x0007, r{{[0-9]+}}
; CHECK: eret
  ret { i32, i32 } { i32 1, i32 2 }
}

define { i8, i32 } @i8_i32() {
; CHECK-LABEL: _i8_i32:
; CHECK-DAG: mov @dr0, r{{[0-9]+}}
; CHECK-DAG: mov @dr0+0x0004, r{{[0-9]+}}
; CHECK: eret
  ret { i8, i32 } { i8 1, i32 2 }
}
