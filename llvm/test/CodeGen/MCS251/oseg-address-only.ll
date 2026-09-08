; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
;
; Pinned to the v1 compatibility contract: this is a legacy-layout suite. The
; llc no-flag default is the xsmall/v2-Small model (clang cc1 default).
;
; Taking the address alone must not require another module to provide slots.
; CHECK-NOT: _unused_PARM_
; CHECK: (_unused)
; CHECK-NOT: _unused_PARM_
; OBJ: S _unused Ref000000
; OBJ-NOT: _unused_PARM_

declare i16 @unused(i16, i16)
define ptr @address() {
  ret ptr @unused
}
