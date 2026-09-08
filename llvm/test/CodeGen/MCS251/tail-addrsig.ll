; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O0 -addrsig < %s | FileCheck %s --implicit-check-not=.addrsig
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O2 -addrsig < %s > %t.asm
; RUN: FileCheck %s --implicit-check-not=.addrsig < %t.asm
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O2 -addrsig=false < %s > %t.no-addrsig.asm
; RUN: diff %t.asm %t.no-addrsig.asm
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O2 -filetype=obj -addrsig < %s > %t.rel
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O2 -filetype=obj -addrsig=false < %s > %t.no-addrsig.rel
; RUN: diff %t.rel %t.no-addrsig.rel
;
; Pinned to the v1 compatibility contract: this is a legacy-layout suite. The
; llc no-flag default is the xsmall/v2-Small model (clang cc1 default).

; Tail hints fall back to ordinary calls, including indirect calls and calls
; with a live frame. ASxxxx REL ignores address-significance emission requests.
source_filename = "tail-addrsig.ll"

declare i8 @step8(i8)
declare i16 @step16(i16)
declare i32 @step32(i32)

define i8 @tail8(i8 %x) {
; CHECK-LABEL: _tail8:
; CHECK: ecall _step8
; CHECK: eret
  %v = tail call i8 @step8(i8 %x)
  ret i8 %v
}

define i16 @tail16(i16 %x) {
; CHECK-LABEL: _tail16:
; CHECK: ecall _step16
; CHECK: eret
  %slot = alloca i16, align 1
  store volatile i16 %x, ptr %slot, align 1
  %saved = load volatile i16, ptr %slot, align 1
  %v = tail call i16 @step16(i16 %saved)
  ret i16 %v
}

define i32 @tail32(i32 %x) {
; CHECK-LABEL: _tail32:
; CHECK: ecall _step32
; CHECK: eret
  %v = tail call i32 @step32(i32 %x)
  ret i32 %v
}

define i8 @tail_indirect(ptr %f) {
; CHECK-LABEL: _tail_indirect:
; CHECK: ecall @dr{{[0-9]+}}
; CHECK: eret
  %v = tail call i8 %f(i8 16)
  ret i8 %v
}
