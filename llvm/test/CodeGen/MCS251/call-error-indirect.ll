; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj < %s | FileCheck %s --check-prefix=OBJ
;
; Former rejection: canonical i32 function pointers select ECALLr.
define void @caller(ptr %f) {
; CHECK-LABEL: caller:
; CHECK: ecall @dr{{[0-9]+}}
; CHECK: eret
; OBJ: T 00 00 00 {{.*}} 99
; OBJ: T 00 00 0D 08 AA
  call void %f()
  ret void
}
