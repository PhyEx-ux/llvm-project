; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/rc6-1-escape-constprop.ll -o - | FileCheck %s --check-prefix=ESC1
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/rc6-2-dce-side-effect-call.ll -o - | FileCheck %s --check-prefix=ESC2
; The read-only check walks the self-referential initializer first; a
; regression would die there with a bare stack dump (no LLVM ERROR line).
; Reaching the unrelated object-writer relocation gate afterwards proves the
; walk terminated through the visited set.
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/rc6-3-const-recursion.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESC3
; ESC3: LLVM ERROR: MCS251: global 'node': a pointer initializer requires ELF object output
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/rc6-4-volatile-load.ll -o - | FileCheck %s --check-prefix=ESC4
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/rc6-5-mixed-store.ll -o - | FileCheck %s --check-prefix=ESC5
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/rc6-6-dce-global-escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESC6
; ESC6: LLVM ERROR: MCS251 contract violation: i64 integer arithmetic is not yet implemented; wide-integer runtime is not connected
;
; P12-6 (Alice review round 4): cases 3 and 6 give their non-optnone twin its
; OWN split section and RUN. In the shared module the optnone function dies
; first, so the twin's verdict was masked -- deleting the twin flipped no
; check. In isolation the twin's assertion binds: case 3's twin must walk the
; self-referential global through the visited set (the read-only contract
; check runs before the prep stop point) and reach the prep gate; case 6's
; twin alone must die with the same loud rejection.
; RUN: llc -mtriple=mcs251 -O0 -stop-after=mcs251-lowering-prep %t/rc6-3-const-recursion-prepped.ll -o - | FileCheck %s --check-prefix=ESC3P
; ESC3P: @node = global ptr @node
; ESC3P: define i32 @self_referential_global_prepped() addrspace(4)
; ESC3P: name: {{ *}}self_referential_global_prepped
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/rc6-6-dce-global-escape-prepped.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESC6P
; ESC6P: LLVM ERROR: MCS251 contract violation: i64 integer arithmetic is not yet implemented; wide-integer runtime is not connected

; The six historical miscompiles of the old contract verifier's
; constant-propagation/DCE half (commit ee3bb98e2, Alice review round 2,
; "RC-6 错编译四项" plus the two follow-ups), each solidified as one
; regression check. All shapes use optnone functions -- the exact -O0
; frontend shape the bugs lived in. Since P1-2 the folding/DCE logic lives
; in MCS251LoweringPrep (which skips optnone entirely); these fixtures pin
; that neither the read-only MCS251ContractCheck verdicts, nor the
; instruction-selection substitutions for optnone functions, nor the
; preparation of non-optnone functions can reintroduce any of the six.
;
; P12-4: every case now also carries a non-optnone twin (_prepped), so the
; MCS251LoweringPrep rewrites themselves (the path that executes on
; ordinary functions) are tested against the same six shapes with the same
; verdicts -- both routes through the shared MCS251LocalInterp oracle.
; P12-6: in cases 3 and 6 the twin lives in its own split section (see the
; ESC3P/ESC6P RUNs above), because the shared-module RUN fatalizes on the
; optnone function before the twin's verdict could ever bind.

;--- rc6-1-escape-constprop.ll
; Miscompile 1: constant propagation ignored escape. After mutate(&x) the
; old folder still replaced the load with the stale stored constant, so the
; function returned 1 regardless of what mutate wrote. The unified escape
; analysis (RC-6-A) must keep the load: the value is read back from the
; frame AFTER the call returns.
declare void @mutate(ptr)
define i32 @escape_then_load() noinline optnone {
entry:
  %p = alloca i32, align 1
  store i32 1, ptr %p, align 1
  call void @mutate(ptr %p)
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; ESC1-LABEL: _escape_then_load:
; ESC1: ecall _mutate
; ESC1: mov r0, @dr60-0x0003
; ESC1: mov r2, @dr60
; ESC1: eret
; Non-optnone twin: MCS251LoweringPrep performs the rewrites here, and its
; escape gate (shared oracle) must reach the same verdict.
define i32 @escape_then_load_prepped() {
entry:
  %p = alloca i32, align 1
  store i32 1, ptr %p, align 1
  call void @mutate(ptr %p)
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; ESC1-LABEL: _escape_then_load_prepped:
; ESC1: ecall _mutate
; ESC1: mov r0, @dr60-0x0003
; ESC1: mov r2, @dr60
; ESC1: eret

;--- rc6-2-dce-side-effect-call.ll
; Miscompile 2: DCE removed a side-effecting call. When the dead i64
; computation was erased, the old sweep also dropped the preceding
; side_effect() call, leaving a function body of just eret. Calls are never
; DCE candidates (RC-6-B): the ecall must survive while the dead wide
; arithmetic never reaches the output.
declare void @side_effect()
define void @call_survives_dce(i32 %x, i32 %y) noinline optnone {
entry:
  call void @side_effect()
  %p = alloca i64, align 1
  %a = zext i32 %x to i64
  %b = zext i32 %y to i64
  %r = add i64 %a, %b
  store i64 %r, ptr %p, align 1
  ret void
}
; ESC2-LABEL: _call_survives_dce:
; ESC2: ecall _side_effect
; ESC2-NOT: add
; ESC2: eret
; Non-optnone twin: the targeted DCE of MCS251LoweringPrep erases the wide
; computation and its sink store, and must equally keep the call.
define void @call_survives_dce_prepped(i32 %x, i32 %y) {
entry:
  call void @side_effect()
  %p = alloca i64, align 1
  %a = zext i32 %x to i64
  %b = zext i32 %y to i64
  %r = add i64 %a, %b
  store i64 %r, ptr %p, align 1
  ret void
}
; ESC2-LABEL: _call_survives_dce_prepped:
; ESC2: ecall _side_effect
; ESC2-NOT: add
; ESC2: eret

;--- rc6-3-const-recursion.ll
; Miscompile 3: constant recursion SIGSEGV. `@node = global ptr @node`
; sent the constant-tree walk into unbounded recursion. The visited set
; (RC-6-C) terminates the walk; compiling the module must not crash.
@node = global ptr @node
define i32 @self_referential_global() noinline optnone {
entry:
  ret i32 0
}

;--- rc6-3-const-recursion-prepped.ll
; P12-6: the non-optnone twin runs ALONE. The visited-set module walk must
; terminate through the preparation route too (the read-only contract check
; executes before the prep stop point), and the twin must reach the prep
; gate -- deleting this twin fails the ESC3P RUN.
@node = global ptr @node
define i32 @self_referential_global_prepped() {
entry:
  ret i32 0
}

;--- rc6-4-volatile-load.ll
; Miscompile 4: a volatile alloca load was deleted (and the return value
; folded to the stored literal). Volatile accesses are never fold or DCE
; candidates (RC-7): every frame byte is stored and read back.
define i32 @volatile_load_survives() noinline optnone {
entry:
  %p = alloca i32, align 1
  store volatile i32 7, ptr %p, align 1
  %v = load volatile i32, ptr %p, align 1
  ret i32 %v
}
; ESC4-LABEL: _volatile_load_survives:
; ESC4: mov r0, #0x07
; ESC4: mov @dr60, r0
; ESC4: mov r0, @dr60-0x0003
; ESC4: mov r2, @dr60
; ESC4: eret
; Non-optnone twin: MCS251LoweringPrep must leave volatile accesses alone.
define i32 @volatile_load_survives_prepped() {
entry:
  %p = alloca i32, align 1
  store volatile i32 7, ptr %p, align 1
  %v = load volatile i32, ptr %p, align 1
  ret i32 %v
}
; ESC4-LABEL: _volatile_load_survives_prepped:
; ESC4: mov r0, #0x07
; ESC4: mov @dr60, r0
; ESC4: mov r0, @dr60-0x0003
; ESC4: mov r2, @dr60
; ESC4: eret

;--- rc6-5-mixed-store.ll
; Miscompile 5: mixed-type store ignored. `*(unsigned char *)&x = c` writes
; one byte of the i32 slot; the old gate treated the slot as holding the
; constant 0, so the function always returned 0. Mixed-width stores
; disqualify propagation (RC-7): the byte store lands in the frame and the
; whole slot is reloaded.
define i32 @mixed_width_store(i8 %c) noinline optnone {
entry:
  %p = alloca i32, align 1
  store i32 0, ptr %p, align 1
  store i8 %c, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; ESC5-LABEL: _mixed_width_store:
; ESC5: mov @dr60-0x0003, r0
; ESC5: mov r0, @dr60-0x0003
; ESC5: mov r2, @dr60
; ESC5: eret
; Non-optnone twin: the preparation route must reach the same verdict.
define i32 @mixed_width_store_prepped(i8 %c) {
entry:
  %p = alloca i32, align 1
  store i32 0, ptr %p, align 1
  store i8 %c, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  ret i32 %v
}
; ESC5-LABEL: _mixed_width_store_prepped:
; ESC5: mov @dr60-0x0003, r0
; ESC5: mov r0, @dr60-0x0003
; ESC5: mov r2, @dr60
; ESC5: eret

;--- rc6-6-dce-global-escape.ll
; Miscompile 6: DCE deleted an initialization through a globally escaped
; address (`saved = &x`): the old dead-slot check counted a store OF the
; alloca address as an ordinary write, erased the i64 computation feeding
; the slot, and the external observer read a never-stored slot. The unified
; rule (store-of-address is an escape, RC-7 P1-3) keeps the value
; observable, so the still-unsupported live i64 arithmetic is rejected
; loudly instead of being silently miscompiled.
@saved = external global ptr
declare void @observe()
define void @global_escape_keeps_writer(i32 %x, i32 %y) noinline optnone {
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

;--- rc6-6-dce-global-escape-prepped.ll
; P12-6: the non-optnone twin runs ALONE: neither the prep-route DCE nor the
; ISel substitution may treat the escaped slot as dead, so the loud rejection
; must fire on this route by itself -- deleting this twin makes llc succeed
; and fails the ESC6P RUN.
@saved = external global ptr
declare void @observe()
define void @global_escape_keeps_writer_prepped(i32 %x, i32 %y) {
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
