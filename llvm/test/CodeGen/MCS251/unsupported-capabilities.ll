; RUN: split-file %s %t
;
; WP4 IR-layer capability coverage (the safety net that also protects direct
; llc input and the optimization pipeline). Every rejected construct exits
; with status 1 and a single actionable "LLVM ERROR: MCS251 contract
; violation: ..." line -- no bug-report request, no stack dump, no abort.
; The positive sections must keep compiling.
;
; The `not llc ... | FileCheck` lines below prove "nonzero and the expected
; text"; `not` accepts a crash (134), an abort, or exit 70 just as readily as
; the intended 1. The exact status is pinned separately, for one rejection of
; each family and for a positive control, by the helper below.
; RUN: %python %S/Inputs/check-llc-exit-code.py --llc llc
;
; The C-source-level halves live in clang/test/Sema/mcs251-capability-diagnostics.c
; and the dialect spellings in clang/test/Parser/mcs251-unsupported-dialects.c.

; ---------------------------------------------------------------- A4/A5/A6 --
; The three historical exits (Cannot generate unaligned atomic load /
; Cannot select: AtomicLoadAdd / Cannot select: AtomicFence) were inconsistent
; and aborted; one family sentence now names the operation.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-load.ll 2>&1 | FileCheck %s --check-prefix=ALOAD --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; ALOAD: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic load)
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-store.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=ASTORE
; ASTORE: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic store)
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-rmw.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=ARMW
; ARMW: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic read-modify-write)
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-cmpxchg.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=ACMPXCHG
; ACMPXCHG: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic compare-exchange)
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-fence.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=AFENCE
; AFENCE: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic fence)
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-libcall.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=ALIBCALL
; ALIBCALL: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic builtin '__atomic_load_2' lowered to a runtime call)

; -------------------------------------------------------------------- A7 --
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/multiarg-indirect.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=A7
; A7: LLVM ERROR: MCS251 contract violation: multi-argument indirect calls are not supported (static parameter slots require a named callee)
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/indirect-supported.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/direct-supported.ll -o /dev/null
; Real object generation for the supported shapes (v2 ELF objects, with the
; signature metadata the identity requires).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -O0 %t/indirect-supported.ll -o %t/indirect.o && test -s %t/indirect.o
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -O0 %t/direct-supported.ll -o %t/direct.o && test -s %t/direct.o
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -O0 %t/normal-switch.ll -o %t/switch.o && test -s %t/switch.o
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -O0 %t/volatile-supported.ll -o %t/volatile.o && test -s %t/volatile.o
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -O0 %t/weak-decl-supported.ll -o %t/weakdecl.o && test -s %t/weakdecl.o

; -------------------------------------------------------------------- A8 --
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/weak-def.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=A8
; A8: LLVM ERROR: MCS251 contract violation: weak function definitions are not supported
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/weak-global.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=A8G
; A8G: LLVM ERROR: MCS251 contract violation: weak global definitions are not supported
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/weak-decl-supported.ll -o /dev/null
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/weak-alias.ll 2>&1 | FileCheck %s --check-prefix=A8A --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; A8A: LLVM ERROR: MCS251 contract violation: weak alias definitions are not supported

; -------------------------------------------------------------------- A9 --
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/module-asm.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=A9
; A9: LLVM ERROR: MCS251 contract violation: module-level inline assembly is not supported
; Whitespace-only text is still TEXT the streamer would receive, so it is a
; module-asm request like any other; only a truly empty string is not. The
; fixture is refused by the same gate and must not print crash text either.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/module-asm-blank.ll 2>&1 | FileCheck %s --check-prefix=A9BLANK --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; A9BLANK: LLVM ERROR: MCS251 contract violation: module-level inline assembly is not supported
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-address-taken.ll 2>&1 | FileCheck %s --check-prefix=ATAKEN --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; ATAKEN: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-fence-name.ll 2>&1 | FileCheck %s --check-prefix=AFNAME --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; AFNAME: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/atomic-lockfree-query.ll 2>&1 | FileCheck %s --check-prefix=ALFQ --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; ALFQ: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/debug-location-tracking.ll -o /dev/null

; -------------------------------------------------------------------- D1 --
; Both -O0 and -O2 must refuse: acceptance must not depend on whether an
; optimizer happens to eliminate the construct.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/computed-goto-init.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=D1ADDR
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O2 %t/computed-goto-init.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=D1ADDR
; D1ADDR: LLVM ERROR: MCS251 contract violation: computed goto is not supported: an address-of-label constant ('blockaddress') appears in a static initializer
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/computed-goto-br.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=D1BR
; D1BR: LLVM ERROR: MCS251 contract violation: computed goto is not supported: 'indirectbr' has no ABI on this target
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/normal-switch.ll -o /dev/null

; -------------------------------------------------------------------- A2 --
; The absolute-address (inttoptr) static pointer form is rejected with the
; actionable message; null and &symbol initializers keep compiling (the
; supported-pointer sections below).
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/abs-ptr-init.ll 2>&1 | FileCheck --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' %s --check-prefix=A2
; A2: LLVM ERROR: MCS251 contract violation: global 'abs_ptr': absolute-address (integer-to-pointer cast) pointer initialization is not supported
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -O0 %t/ptr-init-supported.ll -o /dev/null

; -------------------------------------------------------------------- C1 --
; Each signal is pinned independently by its message variant:
;   * a source-debug compile unit (FullDebug / LineTablesOnly) -> "compile unit";
;   * a debug record on a NoDebug compile unit -> "debug record";
;   * a legacy dbg intrinsic on a NoDebug compile unit -> "debug intrinsic";
;   * the LocTrackingOnly shape (NoDebug CU + subprograms + locations, no
;     records) is the NEGATIVE control and must keep compiling.
; (The subprogram/location rejection branches are only reachable without a
; compile unit, a shape the IR parser refuses to build from text -- see the
; comment on verifyNoDebugInfo; they stay fail-closed for other inputs.)
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/debug-cu-full.ll 2>&1 | FileCheck %s --check-prefix=C1CU --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/debug-cu-linetables.ll 2>&1 | FileCheck %s --check-prefix=C1CU --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/debug-record.ll 2>&1 | FileCheck %s --check-prefix=C1REC --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/debug-legacy.ll 2>&1 | FileCheck %s --check-prefix=C1REC --implicit-check-not='PLEASE submit a bug report' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/debug-location-tracking.ll -o /dev/null
; (The textual llvm.dbg.value intrinsic is migrated to a debug record by the
; IR parser, so both spellings are pinned by C1REC; the intrinsic branch stays
; as a fail-closed backstop for other inputs.)
; C1CU: LLVM ERROR: MCS251 contract violation: a debug information request is not supported on this target (compile unit)
; C1REC: LLVM ERROR: MCS251 contract violation: a debug information request is not supported on this target (debug record)
; C1DBG: LLVM ERROR: MCS251 contract violation: a debug information request is not supported on this target (debug intrinsic)

; ------------------------------------------------------- positive controls --
; A volatile access is not an atomic access and stays supported.
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=null -O0 %t/volatile-supported.ll -o /dev/null

;--- atomic-load.ll
target triple = "mcs251-unknown-none"
@w = global i16 0
define i16 @f() {
  %v = load atomic i16, ptr @w seq_cst, align 2
  ret i16 %v
}

;--- atomic-store.ll
target triple = "mcs251-unknown-none"
@w = global i16 0
define void @f() {
  store atomic i16 1, ptr @w seq_cst, align 2
  ret void
}

;--- atomic-rmw.ll
target triple = "mcs251-unknown-none"
@w = global i16 0
define i16 @f() {
  %v = atomicrmw add ptr @w, i16 1 seq_cst
  ret i16 %v
}

;--- atomic-cmpxchg.ll
target triple = "mcs251-unknown-none"
@w = global i16 0
define void @f() {
  %p = cmpxchg ptr @w, i16 0, i16 1 seq_cst seq_cst
  ret void
}

;--- atomic-fence.ll
target triple = "mcs251-unknown-none"
define void @f() {
  fence seq_cst
  ret void
}

;--- atomic-libcall.ll
; The library lowering of an atomic builtin carries no atomic opcode, so the
; family check also matches the runtime-call spelling.
target triple = "mcs251-unknown-none"
declare i16 @__atomic_load_2(ptr, i32)
define i16 @f(ptr %p) {
  %v = call i16 @__atomic_load_2(ptr %p, i32 5)
  ret i16 %v
}

;--- multiarg-indirect.ll
target triple = "mcs251-unknown-none"
define void @f(ptr addrspace(4) %fp) {
  call void %fp(i16 1, i16 2)
  ret void
}

;--- indirect-supported.ll
; A one-argument indirect call uses the register channel only and stays legal.
target triple = "mcs251-unknown-none"
define void @f(ptr addrspace(4) %fp) {
  call void %fp(i16 1)
  ret void
}
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0, i32 0}

;--- direct-supported.ll
; A DIRECT multi-argument call has a named callee and stays legal.
target triple = "mcs251-unknown-none"
define void @callee(i16 %a, i16 %b) { ret void }
define void @f() {
  call void @callee(i16 1, i16 2)
  ret void
}
!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_callee", i32 1, i32 0, i32 0, i32 0}
!10001 = !{!"_f", i32 1, i32 0}

;--- weak-def.ll
target triple = "mcs251-unknown-none"
define weak void @wf() { ret void }

;--- weak-global.ll
target triple = "mcs251-unknown-none"
@wg = weak global i16 1

;--- weak-alias.ll
; A weak alias is a weak definition of the alias symbol (the linker has no
; weak resolution, and the alias is outside the registered object identity).
target triple = "mcs251-unknown-none"
define i16 @f() { ret i16 1 }
@wg = weak alias i16 (), ptr addrspace(4) @f

;--- module-asm-blank.ll
; Whitespace-only text is still text the streamer would receive: only a
; TRULY empty string is not a request.
target triple = "mcs251-unknown-none"
module asm " "
define void @f() { ret void }

;--- atomic-address-taken.ll
; The <stdatomic.h> function name is a real declaration; taking its address
; (or calling it through a pointer) is an atomic use even though the call site
; itself has no resolvable callee.
target triple = "mcs251-unknown-none"
@fp = global ptr addrspace(4) @atomic_thread_fence
declare void @atomic_thread_fence(i32) addrspace(4)
define void @f() {
  %p = load ptr addrspace(4), ptr @fp
  call addrspace(4) void %p(i32 5)
  ret void
}

;--- atomic-fence-name.ll
; The __atomic_fence spelling (no longer excluded by name).
target triple = "mcs251-unknown-none"
declare void @__atomic_fence(ptr, i64, i32)
define void @f(ptr %p) {
  call void @__atomic_fence(ptr %p, i64 4, i32 5)
  ret void
}

;--- atomic-lockfree-query.ll
; The RUNTIME lock-free query is rejected; the compile-time
; __atomic_always_lock_free is a folded constant and stays.
target triple = "mcs251-unknown-none"
declare zeroext i1 @__atomic_is_lock_free(i64, ptr)
define zeroext i1 @f() {
  %r = call zeroext i1 @__atomic_is_lock_free(i64 4, ptr null)
  ret i1 %r
}

;--- debug-location-tracking.ll
; NEGATIVE control: clang's LocTrackingOnly shape (a NoDebug compile unit plus
; subprograms and locations, no records) is how -Rpass/-fstack-usage carry
; source locations and must keep compiling.
target triple = "mcs251-unknown-none"
define void @f() !dbg !4 {
  ret void, !dbg !9
}
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: NoDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!4 = distinct !DISubprogram(name: "f", file: !1, line: 1, type: !5, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
!7 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !DILocation(line: 1, column: 1, scope: !4)

;--- weak-decl-supported.ll
; A weak declaration that is never defined here is not a definition.
target triple = "mcs251-unknown-none"
declare extern_weak void @wf()
define void @f() {
  call void @wf()
  ret void
}
!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_wf", i32 2, i32 0}
!10001 = !{!"_f", i32 1, i32 0}

;--- module-asm.ll
target triple = "mcs251-unknown-none"
module asm ".globl myasm"
define void @f() { ret void }

;--- computed-goto-init.ll
target triple = "mcs251-unknown-none"
@tab = global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) blockaddress(@f, %b) to ptr)]
define void @f() {
entry:
  br label %b
b:
  ret void
}

;--- computed-goto-br.ll
target triple = "mcs251-unknown-none"
define void @f(ptr %p) {
entry:
  indirectbr ptr %p, [label %b]
b:
  ret void
}

;--- normal-switch.ll
target triple = "mcs251-unknown-none"
define i16 @f(i16 %x) {
entry:
  switch i16 %x, label %default [
    i16 1, label %one
    i16 2, label %two
  ]
one:
  ret i16 1
two:
  ret i16 2
default:
  ret i16 0
}
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0, i32 0}

;--- abs-ptr-init.ll
target triple = "mcs251-unknown-none"
@abs_ptr = global ptr inttoptr (i32 65280 to ptr)

;--- ptr-init-supported.ll
; null and &symbol (+ folded constant offset) are the supported static forms.
target triple = "mcs251-unknown-none"
@target_word = global i16 0
@null_ptr = global ptr null
@symbol_ptr = global ptr @target_word
!mcs251.signatures = !{}

;--- debug-cu-full.ll
target triple = "mcs251-unknown-none"
define void @f() { ret void }
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!7 = !{i32 2, !"Debug Info Version", i32 3}

;--- debug-cu-linetables.ll
target triple = "mcs251-unknown-none"
define void @f() { ret void }
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: LineTablesOnly)
!1 = !DIFile(filename: "a.c", directory: "/")
!7 = !{i32 2, !"Debug Info Version", i32 3}

;--- debug-record.ll
; The modern debug records (not the legacy llvm.dbg.* intrinsics) are their
; own signal: an instruction carrying a DbgRecord must not pass silently. The
; compile unit is NoDebug so only the record branch can fire.
target triple = "mcs251-unknown-none"
define void @f(i16 %x) !dbg !5 {
entry:
  #dbg_value(i16 %x, !4, !DIExpression(), !9)
  ret void, !dbg !9
}
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: NoDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!4 = !DILocalVariable(name: "x", scope: !5, file: !1, line: 1)
!5 = distinct !DISubprogram(name: "f", file: !1, line: 1, type: !6, unit: !0)
!6 = !DISubroutineType(types: !8)
!8 = !{null}
!7 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !DILocation(line: 1, column: 1, scope: !5)

;--- debug-legacy.ll
; The pre-records dbg intrinsics are still debug information. NoDebug CU again,
; so only the intrinsic branch can fire.
target triple = "mcs251-unknown-none"
declare void @llvm.dbg.value(metadata, metadata, metadata)
define void @f(i16 %x) !dbg !5 {
  call void @llvm.dbg.value(metadata i16 %x, metadata !4, metadata !DIExpression()), !dbg !9
  ret void
}
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: NoDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!4 = !DILocalVariable(name: "x", scope: !5, file: !1, line: 1)
!5 = distinct !DISubprogram(name: "f", file: !1, line: 1, type: !6, unit: !0)
!6 = !DISubroutineType(types: !8)
!8 = !{null}
!9 = !DILocation(line: 1, column: 1, scope: !5)
!7 = !{i32 2, !"Debug Info Version", i32 3}

;--- volatile-supported.ll
; A volatile (non-atomic) access is the supported shared-memory form.
target triple = "mcs251-unknown-none"
@shared_word = global i16 0
define i16 @f() {
  %v = load volatile i16, ptr @shared_word, align 2
  ret i16 %v
}
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}