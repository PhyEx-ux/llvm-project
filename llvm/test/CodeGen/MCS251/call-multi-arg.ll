; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj < %s | FileCheck %s --check-prefix=OBJ
;
; Write the callee's big-endian slot before setting DPTR and calling it.
; CHECK-LABEL: _call_two:
; CHECK: mov [[HI:r[0-9]+]], #0x00
; CHECK: .globl _g2_PARM_2
; CHECK: (_g2_PARM_2) >> 8, (_g2_PARM_2)
; CHECK: mov @[[ADDR:dr[0-9]+]], [[HI]]
; CHECK: mov [[LO:r[0-9]+]], #0x02
; CHECK: mov @[[ADDR]]+0x0001, [[LO]]
; CHECK: mov {{wr[0-9]+}}, #0x0001
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK-NEXT: ecall _g2
; CHECK: eret
; OBJ: S _g2 Ref000000
; OBJ: S _g2_PARM_2 Ref000000
; OBJ-NOT: A OSEG
; OBJ-NOT: A DSEG

declare i16 @g2(i16, i16)
define i16 @call_two() {
  %r = call i16 @g2(i16 1, i16 2)
  ret i16 %r
}
