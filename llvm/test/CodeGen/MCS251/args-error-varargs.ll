; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/vaarg-i32.ll -o %t/a.o 2>&1 | FileCheck %s --check-prefix=E
; RUN: not --crash llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/vaarg-f32.ll -o %t/b.o 2>&1 | FileCheck %s --check-prefix=E
; RUN: not --crash llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/seven.ll -o %t/c.o 2>&1 | FileCheck %s --check-prefix=F
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/indirect.ll -o %t/d.o 2>&1 | FileCheck %s --check-prefix=C2
; RUN: not --crash llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/nofixed.ll -o %t/e.o 2>&1 | FileCheck %s --check-prefix=F0
;
; G2 B-S2 (G2-VARIADIC-DESIGN-draft.md R3 §4.3.5/§4.3.6/§4.4.3): ordinary
; variadic definitions and direct variadic calls now lower through the
; static continuation-slot ABI (see vararg-slots.ll).  What stays
; fail-closed at the backend are the Sema-bypass shapes:
;
;   E  a residual llvm.va_arg reaching llc -- clang lowers va_arg itself
;      (MCS251ABIInfo::EmitVAArg), so any va_arg node is hand-written-IR or
;      stale-compiler leakage; both the legal-result path (LowerOperation)
;      and the illegal-result path (ReplaceNodeResults, f32 here) reject
;      with the same frozen message.
;   F  a variadic call site passing more than 6 variadic actuals -- the
;      Sema cap gate (message A) should have caught it in the source; the
;      count is re-checked here on the lowered IR.
;   C2 an indirect variadic call -- static slots are named-callee keyed.
;   F0 a variadic definition with zero fixed parameters -- the first source
;      argument would ride the DPL register channel, which no slot-based
;      va_arg can read (silent-skip hazard), so the shape is a hard error
;      (G2 §4.3.6 design stance: compiler-countable overflow always errs).
;
; ISR x variadic stays rejected as well, but one layer earlier: the IR
; verifier itself enforces the T05 void(void) interrupt type, so it can
; not even reach instruction selection (see clang/test/Sema/mcs251-isr.c
; and the CanLowerReturn ISR-branch fatal kept as defense in depth).

; E: LLVM ERROR: MCS251: llvm.va_arg is not supported; va_arg is lowered by clang CodeGen (MCS251ABIInfo::EmitVAArg)
; F: LLVM ERROR: MCS251: variadic call passes more than 6 variadic arguments (Sema cap gate missed this call; recompile the caller with a current compiler)
; C2: LLVM ERROR: MCS251 contract violation: multi-argument indirect calls are not supported (static parameter slots require a named callee); call the function directly, or pass at most one argument through the function pointer
; F0: LLVM ERROR: MCS251: a variadic definition must have at least one fixed parameter (the first source argument uses the register channel, which va_arg cannot read)

;--- vaarg-i32.ll
define i32 @f(ptr %ap) addrspace(4) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- vaarg-f32.ll
define float @g(ptr %ap) addrspace(4) {
  %v = va_arg ptr %ap, float
  ret float %v
}
!mcs251.signatures = !{!0}
!0 = !{!"_g", i32 9, i32 0, i32 0}

;--- seven.ll
declare void @sink(i32, ...) addrspace(4)
define void @caller() addrspace(4) {
  call void (i32, ...) @sink(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8)
  ret void
}
!mcs251.signatures = !{!0, !1}
!0 = !{!"_caller", i32 1, i32 0, i32 0}
!1 = !{!"_sink", i32 10, i32 0, i32 0}

;--- indirect.ll
define void @caller(ptr addrspace(4) %fp) addrspace(4) {
  call addrspace(4) void (i32, ...) %fp(i32 1, i32 2)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_caller", i32 1, i32 0, i32 0}

;--- nofixed.ll
define void @f0(...) addrspace(4) {
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f0", i32 9, i32 0, i32 0}
