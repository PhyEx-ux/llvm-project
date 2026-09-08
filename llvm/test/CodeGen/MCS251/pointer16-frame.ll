; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; SPX/frame bases remain physical 32-bit DR registers, but escaping AS0 stack
; addresses and pointer spills use the module's 16-bit pointer representation.

declare void @clobber()

define ptr @near_frame_address(i8 %value) {
; CHECK-LABEL: _near_frame_address:
; CHECK: inc spx, #0x1
; CHECK: mov {{r[0-9]+}}, 0x81
; CHECK: mov {{r[0-9]+}}, 0x85
; CHECK: add [[ADDR:wr[0-9]+]], #
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK-NOT: mov b,
; CHECK-NOT: mov a,
; CHECK: dec spx, #0x1
; CHECK: eret
  %slot = alloca i8, align 1
  store volatile i8 %value, ptr %slot, align 1
  ret ptr %slot
}

define i8 @near_pointer_across_call(ptr %p) {
; CHECK-LABEL: _near_pointer_across_call:
; CHECK: inc spx, #0x2
; CHECK: mov @dr60-0x0001, [[SAVED:wr[0-9]+]]
; CHECK: ecall _clobber
; CHECK: mov [[RESTORED:wr[0-9]+]], @dr60-0x0001
; CHECK: mov {{r[0-9]+}}, @[[RESTORED]]
; CHECK: dec spx, #0x2
; CHECK: eret
  call void @clobber()
  %value = load volatile i8, ptr %p, align 1
  ret i8 %value
}

define ptr @near_dynamic_alloca(i16 %size) {
; CHECK-LABEL: _near_dynamic_alloca:
; CHECK: push dr16
; CHECK: mov dr16, dr60
; CHECK: mov {{r[0-9]+}}, 0x81
; CHECK: mov {{r[0-9]+}}, 0x85
; CHECK: add [[NEWSP:wr[0-9]+]], {{wr[0-9]+}}
; CHECK: mov 0x85,
; CHECK: mov 0x81,
; CHECK: add [[RESULT:wr[0-9]+]], #0x0001
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK: mov dr60, dr16
; CHECK: pop dr16
; CHECK: eret
  %slot = alloca i8, i16 %size, align 1
  ret ptr %slot
}
