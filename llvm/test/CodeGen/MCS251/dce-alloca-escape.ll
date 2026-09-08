; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/no-escape.ll -o - | FileCheck %s --check-prefix=NOESC
; ESCAPE: LLVM ERROR: MCS251 contract violation: i64 integer arithmetic is not yet implemented; wide-integer runtime is not connected

; RC-7 (P1-3): the -O0 DCE over write-only allocas must treat a store of the
; alloca ADDRESS as an escape. This is the IR shape clang -O0 emits for:
;
;   extern unsigned long long *saved;
;   extern void observe(void);
;   void f(unsigned long a, unsigned long b) {
;     unsigned long long x = (unsigned long long)a + b;
;     saved = &x; observe();
;   }
;
; The old dead-slot check only tested isa<StoreInst> on the alloca users, so
; `store ptr %p, ptr @saved` counted as an ordinary write: the i64 addition
; feeding the slot was deleted while the escaped address kept pointing at the
; never-stored slot, and the external observer read garbage (a silent
; miscompile). With the unified escape analysis the writer is kept alive, so
; the still-unsupported dynamic i64 arithmetic is rejected loudly instead of
; being miscompiled.

;--- escape.ll
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

;--- no-escape.ll
; Control: without the address escape the slot really is write-only scratch,
; the dead i64 computation is removed, and compilation succeeds.
define void @f(i32 %x, i32 %y) noinline optnone {
entry:
  %p = alloca i64, align 1
  %a = zext i32 %x to i64
  %b = zext i32 %y to i64
  %r = add i64 %a, %b
  store i64 %r, ptr %p, align 1
  ret void
}
; NOESC-LABEL: _f:
; NOESC-NOT: add
; NOESC: eret
