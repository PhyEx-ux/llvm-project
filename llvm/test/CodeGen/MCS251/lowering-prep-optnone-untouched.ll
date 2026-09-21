; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -stop-after=mcs251-lowering-prep %t/mixed.ll -o %t.mir
; RUN: FileCheck %s --check-prefix=IR --input-file %t.mir
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/optnone-only.ll -o - | FileCheck %s --check-prefix=ASM
; RUN: not llc -mtriple=mcs251 -O0 %t/optnone-escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; ESCAPE: LLVM ERROR: MCS251 contract violation: i64 integer arithmetic is not yet implemented; wide-integer runtime is not connected

; P1-2: MCS251LoweringPrep is the only pass allowed to rewrite IR for this
; target, and it skips every optnone function. Their IR must come out of the
; pass byte-identical (no alloca constant propagation, no constant folding,
; no targeted DCE), while non-optnone functions are prepared as before.
; The two optnone shapes that survive to instruction selection are consumed
; there: effectively-dead wide results are substituted locally, and wide
; results provably parked on constants are resolved through the DAG-local
; proof (optnone-only.ll below compiles and returns 25 with no library
; call and no add).

;--- mixed.ll
define i32 @prepared(i32 %a) {
entry:
  %p = alloca i32, align 1
  store i32 7, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  %r = add i32 %v, %a
  ret i32 %r
}

; IR-LABEL: define i32 @prepared
; IR-NEXT: entry:
; IR-NEXT: %p = alloca i32, align 1
; IR-NEXT: store i32 7, ptr %p, align 1
; IR-NEXT: %r = add i32 7, %a
; IR-NEXT: ret i32 %r
; IR-NEXT: }

define i32 @untouched_nofold(i8 %c) noinline optnone {
entry:
  %p = alloca i32, align 1
  store i32 0, ptr %p, align 1
  store i8 %c, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  ret i32 %v
}

; The optnone body must be identical line for line: the mixed-width store
; is not folded (RC-7), the load is not propagated, nothing is erased.
; P12-4: the bodies are pinned with gapless NEXT chains (whole-function
; line-by-line comparison), so any inserted, removed or reordered line --
; e.g. an extra store smuggled into the "untouched" function -- breaks the
; chain instead of slipping through a match gap.
; IR-LABEL: define i32 @untouched_nofold
; IR-NEXT: entry:
; IR-NEXT: %p = alloca i32, align 1
; IR-NEXT: store i32 0, ptr %p, align 1
; IR-NEXT: store i8 %c, ptr %p, align 1
; IR-NEXT: %v = load i32, ptr %p, align 1
; IR-NEXT: ret i32 %v
; IR-NEXT: }

define void @untouched_dead(i32 %x, i32 %y) noinline optnone {
entry:
  %p = alloca i64, align 1
  %a = zext i32 %x to i64
  %b = zext i32 %y to i64
  %r = add i64 %a, %b
  store i64 %r, ptr %p, align 1
  ret void
}

; IR-LABEL: define void @untouched_dead
; IR-NEXT: entry:
; IR-NEXT: %p = alloca i64, align 1
; IR-NEXT: %a = zext i32 %x to i64
; IR-NEXT: %b = zext i32 %y to i64
; IR-NEXT: %r = add i64 %a, %b
; IR-NEXT: store i64 %r, ptr %p, align 1
; IR-NEXT: ret void
; IR-NEXT: }

;--- optnone-only.ll
; Both consumer shapes end-to-end: the dead i64 computation compiles (no
; add reaches the output) and the parked-constant udiv resolves to 25.
define i32 @parked_udiv() noinline optnone {
entry:
  %pq = alloca i64, align 1
  %pr = alloca i64, align 1
  store i64 100, ptr %pq, align 1
  store i64 4, ptr %pr, align 1
  %q = load i64, ptr %pq, align 1
  %r = load i64, ptr %pr, align 1
  %div = udiv i64 %q, %r
  %t = trunc i64 %div to i32
  ret i32 %t
}

define void @dead_wide(i32 %x, i32 %y) noinline optnone {
entry:
  %p = alloca i64, align 1
  %a = zext i32 %x to i64
  %b = zext i32 %y to i64
  %r = add i64 %a, %b
  store i64 %r, ptr %p, align 1
  ret void
}

; ASM-LABEL: _parked_udiv:
; ASM: mov dr4, #0x0019
; ASM: eret
; ASM-LABEL: _dead_wide:
; ASM-NOT: add
; ASM: eret

;--- optnone-escape.ll
; Control: once the slot address escapes, neither the read-only check nor
; instruction selection may treat the parked wide value as dead or constant;
; the still-unsupported live i64 arithmetic is rejected loudly instead of
; being miscompiled.
@saved = external global ptr
declare void @observe()

define void @f(i32 %x, i32 %y) noinline optnone {
entry:
  %p = alloca i64, align 1
  %a = zext i32 %x to i64
  %b = zext i32 %y to i64
  %r = add i64 %a, %b
  store i64 %r, ptr %p, align 1
  store ptr %p, ptr @saved, align 1
  call void @observe()
  ret void
}
