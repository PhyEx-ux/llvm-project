; RUN: llc -mtriple=mcs251 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
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
