; RUN: llc -mtriple=mcs251-unknown-none -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=mcs251-unknown-none -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs \
; RUN:   -mcs251-object-format=elf -filetype=obj -o %t.o < %s

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"

@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @isr_entry to ptr)], section "llvm.metadata"

declare i32 @callee1(i32) addrspace(4)
declare i32 @callee2(i32, i32) addrspace(4)
declare i32 @callee3(i32, i32, i32) addrspace(4)
declare void @isr_helper() addrspace(4)

; 1. Simple leaf function must NOT save/restore callee-saved DR12/WR8.
; CHECK-LABEL: _leaf_add:
; CHECK-NOT: wr12
; CHECK-NOT: wr14
; CHECK-NOT: wr8
; CHECK-NOT: inc spx
; CHECK: eret
define i32 @leaf_add(i32 %a, i32 %b) addrspace(4) {
  %s = add i32 %a, %b
  ret i32 %s
}

; 2. Non-leaf function with values live across a call saves/restores DR12/WR8.
; CHECK-LABEL: _csr_across_call:
; CHECK: inc spx,
; CHECK: mov @dr60-{{.*}}, wr12
; CHECK: mov @dr60-{{.*}}, wr14
; CHECK: ecall _callee1
; CHECK: mov wr12, @dr60-
; CHECK: mov wr14, @dr60-
; CHECK: dec spx,
; CHECK: eret
define i32 @csr_across_call(i32 %a, i32 %b) addrspace(4) {
  %r = call addrspace(4) i32 @callee1(i32 %a)
  %s = add i32 %r, %a
  %t = add i32 %s, %b
  ret i32 %t
}

; 3. Direct 1-arg tail call lowers to ejmp (no ecall/eret).
; CHECK-LABEL: _tail_one_arg:
; CHECK-NOT: ecall
; CHECK-NOT: eret
; CHECK: ejmp _callee1
define i32 @tail_one_arg(i32 %a) addrspace(4) {
  %a1 = add i32 %a, 1
  %r = tail call addrspace(4) i32 @callee1(i32 %a1)
  ret i32 %r
}

; 4. Direct multi-arg tail call with OutStackBytes <= IncomingStackArgBytes
;    rewrites incoming stack arg slot, tears down frame, and jumps via ejmp.
; CHECK-LABEL: _tail_two_arg:
; CHECK-NOT: ecall
; CHECK-NOT: eret
; CHECK: ejmp _callee2
define i32 @tail_two_arg(i32 %a, i32 %b) addrspace(4) {
  %a1 = add i32 %a, 1
  %b2 = add i32 %b, 2
  %r = tail call addrspace(4) i32 @callee2(i32 %a1, i32 %b2)
  ret i32 %r
}

; 5. Tail call needing more stack arg bytes than incoming falls back to ecall.
; CHECK-LABEL: _no_tail_bigger_stack:
; CHECK: inc spx, #0x4
; CHECK: inc spx, #0x4
; CHECK: ecall _callee3
; CHECK: dec spx, #0x4
; CHECK: dec spx, #0x4
; CHECK: eret
define i32 @no_tail_bigger_stack(i32 %a, i32 %b) addrspace(4) {
  %r = tail call addrspace(4) i32 @callee3(i32 %a, i32 %b, i32 99)
  ret i32 %r
}

; 6. ISR must NEVER tail-call and must NEVER double-save CSRs beyond its fixed 37B frame.
; CHECK-LABEL: _isr_entry:
; CHECK: push psw
; CHECK: push dr12
; CHECK: push dpx
; CHECK-NOT: mov @dr60-{{.*}}, wr12
; CHECK-NOT: mov @dr60-{{.*}}, wr8
; CHECK: ecall _isr_helper
; CHECK-NOT: ejmp _isr_helper
; CHECK: pop dpx
; CHECK: pop psw
; CHECK: reti
define mcs251_intrcc void @isr_entry() addrspace(4) #0 {
  tail call addrspace(4) void @isr_helper()
  ret void
}

; 7. Dynamic alloca (DR16 anchor) + incoming stack arguments + address-of-frame-object (wr18).
; CHECK-LABEL: _alloca_and_stack_args:
; CHECK: push dr16
; CHECK: mov dr16, dr60
; CHECK: mov {{wr[0-9]+}}, wr18
; CHECK: mov dr60, dr16
; CHECK: pop dr16
; CHECK: eret
define i8 @alloca_and_stack_args(i16 %n, i8 %a, i8 %b) addrspace(4) {
  %local = alloca i8, align 1
  store i8 %a, ptr %local, align 1
  %dyn = alloca i8, i16 %n, align 1
  store volatile i8 %b, ptr %dyn, align 1
  %p = ptrtoint ptr %local to i32
  %v = load i8, ptr %local, align 1
  %low = trunc i32 %p to i8
  %sum = add i8 %v, %low
  ret i8 %sum
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10}
!0 = !{!"_callee1", i32 2, i32 0, i32 0}
!1 = !{!"_callee2", i32 2, i32 0, i32 0, i32 0}
!2 = !{!"_callee3", i32 2, i32 0, i32 0, i32 0, i32 0}
!3 = !{!"_isr_helper", i32 2, i32 0}
!4 = !{!"_leaf_add", i32 1, i32 0, i32 0, i32 0}
!5 = !{!"_csr_across_call", i32 1, i32 0, i32 0, i32 0}
!6 = !{!"_tail_one_arg", i32 1, i32 0, i32 0}
!7 = !{!"_tail_two_arg", i32 1, i32 0, i32 0, i32 0}
!8 = !{!"_no_tail_bigger_stack", i32 1, i32 0, i32 0, i32 0}
!9 = !{!"_isr_entry", i32 1, i32 0}
!10 = !{!"_alloca_and_stack_args", i32 1, i32 0, i32 0, i32 0, i32 0}
