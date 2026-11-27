; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel < %s | FileCheck %s --check-prefix=OBJ
;
; Push continuation argument (big-endian) onto the stack before setting DPTR
; and calling `_g2`, then pop after return.
; CHECK-LABEL: _call_two:
; CHECK-NOT: _g2_PARM_2
; CHECK: inc spx, #0x2
; CHECK-DAG: mov @dr60-0x0001, r{{[0-9]+}}
; CHECK-DAG: mov @dr60, r{{[0-9]+}}
; CHECK: mov {{wr[0-9]+}}, #0x0001
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK-NEXT: ecall _g2
; CHECK: dec spx, #0x2
; CHECK: eret
; OBJ: S _g2 Ref000000
; OBJ-NOT: _g2_PARM_2
; OBJ-NOT: A OSEG
; OBJ-NOT: A DSEG

declare i16 @g2(i16, i16)
define i16 @call_two() {
  %r = call i16 @g2(i16 1, i16 2)
  ret i16 %r
}
