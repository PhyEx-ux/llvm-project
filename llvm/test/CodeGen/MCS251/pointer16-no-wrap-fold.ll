; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Without a base-range proof, a near GEP offset must participate in i16 address
; formation before memory access. It may not be folded into @wr+dis16, whose
; hardware addition has no established equivalence at the 16-bit wrap boundary.

define i8 @negative_256(ptr %p) addrspace(4) {
; CHECK-LABEL: _negative_256:
; CHECK: add [[ADDR:wr[0-9]+]], #0xff00
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
; CHECK-NOT: @{{wr[0-9]+}}+0xff00
  %q = getelementptr i8, ptr %p, i16 -256
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i16 @positive_word(ptr %p) addrspace(4) {
; CHECK-LABEL: _positive_word:
; CHECK: add [[FIRST:wr[0-9]+]], #0x0010
; CHECK: mov {{r[0-9]+}}, @[[FIRST]]
; CHECK: add [[FIRST]], #0x0001
; CHECK: mov {{r[0-9]+}}, @[[FIRST]]
; CHECK-NOT: @{{wr[0-9]+}}+0x001{{0|1}}
  %q = getelementptr i8, ptr %p, i16 16
  %v = load volatile i16, ptr %q, align 1
  ret i16 %v
}
