; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj < %s | FileCheck %s --check-prefix=OBJ
;
; Pinned to the v1 compatibility contract: this is a legacy-layout suite. The
; llc no-flag default is the xsmall/v2-Small model (clang cc1 default).
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
