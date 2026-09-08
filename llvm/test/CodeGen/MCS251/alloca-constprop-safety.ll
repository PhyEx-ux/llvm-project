; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/volatile.ll -o - | FileCheck %s --check-prefix=VOL
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/partial-store.ll -o - | FileCheck %s --check-prefix=PART
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/folded-control.ll -o - | FileCheck %s --check-prefix=FOLD
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/non-dominating.ll -o - | FileCheck %s --check-prefix=NODOM

; RC-7 (P1-1/P1-2): the -O0 constant propagation over single-constant-store
; allocas must refuse to fold when the access is volatile or when stores of
; differing width alias the same slot. These are the IR shapes clang -O0
; emits for the C counterexamples:
;
;   unsigned long f(void) { volatile unsigned long x = 7; return x; }
;   unsigned long f(unsigned char c) {
;     unsigned long x = 0; *(unsigned char *)&x = c; return x;
;   }
;
; Before the fix, the volatile load was deleted and f returned the literal
; 7, and the partial (i8) store was ignored so f returned the stale 0
; instead of the byte actually stored. After the fix the loads survive to
; instruction selection, so the generated code reads memory back.

;--- volatile.ll
; The volatile store of 7 is followed by a read-back of every frame byte,
; including the lane holding the 7 -- the load is not folded to a constant.
define i32 @f() noinline optnone {
entry:
  %p = alloca i32, align 1
  store volatile i32 7, ptr %p, align 1
  %v = load volatile i32, ptr %p, align 1
  ret i32 %v
}
; VOL-LABEL: _f:
; VOL: mov r0, #0x07
; VOL: mov @dr60, r0
; VOL: mov r0, @dr60-0x0003
; VOL: mov r2, @dr60
; VOL: eret

;--- partial-store.ll
; The i8 store into the i32 slot lands in the first frame byte
; (big-endian lane); the reload reads the whole slot back instead of
; folding to the stale i32 0.
define i32 @f(i8 %c) noinline optnone {
entry:
  %p = alloca i32, align 1
  store i32 0, ptr %p, align 1
  store i8 %c, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; PART-LABEL: _f:
; PART: mov r0, dpl
; PART: mov @dr60-0x0003, r0
; PART: mov r0, @dr60-0x0003
; PART: mov r2, @dr60
; PART: eret

;--- folded-control.ll
; The safety gate must not over-reach: a plain (non-volatile, same-width)
; store still propagates, so the return value is the folded constant and no
; frame byte is ever read back.
define i32 @f() noinline optnone {
entry:
  %p = alloca i32, align 1
  store i32 7, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; FOLD-LABEL: _f:
; FOLD: mov wr0, #0x0007
; FOLD-NOT: mov r{{[0-9]+}}, @dr60
; FOLD: eret

;--- non-dominating.ll
; The single store does not dominate the load (it sits on one incoming
; branch), so on the other path the load reads undef -- the load is not
; provably the stored constant and must not be replaced.
define i32 @f(i8 %c) noinline optnone {
entry:
  %p = alloca i32, align 1
  %t = icmp eq i8 %c, 0
  br i1 %t, label %store, label %join
store:
  store i32 7, ptr %p, align 1
  br label %join
join:
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; NODOM-LABEL: _f:
; NODOM: mov r0, #0x07
; NODOM: mov r0, @dr60-0x0003
; NODOM: mov r2, @dr60
; NODOM: eret
