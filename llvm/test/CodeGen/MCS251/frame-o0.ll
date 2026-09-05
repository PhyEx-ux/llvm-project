; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s

; Phase 9: the -O0 fast register allocator spills cross-block live vregs
; through loadRegFromStackSlot/storeRegToStackSlot, which the frame now
; provides. Before Phase 9 any -O0 function with cross-block live values
; crashed (the spilling stub aborted); this file pins the unlocked shape:
; a 3-byte frame (inc spx #2 + #1), the argument parked in a frame slot
; across the first call, both call results spilled, and the join block
; reloading and adding them.

declare i8 @f8a()

define i8 @o0cross(i8 %c) {
; CHECK-LABEL: _o0cross:
; CHECK: inc spx, #0x2
; CHECK: inc spx, #0x1
; CHECK: mov @dr60-0x0001, r{{[0-9]+}}
; CHECK: ecall _f8a
; CHECK: mov r{{[0-9]+}}, @dr60-0x0001
; CHECK: mov @dr60-0x0002, r{{[0-9]+}}
; CHECK: je
; CHECK: ecall _f8a
; CHECK: mov @dr60, r{{[0-9]+}}
; CHECK: ecall _f8a
; CHECK: mov @dr60, r{{[0-9]+}}
; CHECK: mov r{{[0-9]+}}, @dr60-0x0002
; CHECK: mov r{{[0-9]+}}, @dr60
; CHECK: add
; CHECK: dec spx, #0x2
; CHECK: dec spx, #0x1
; CHECK: eret
entry:
  %a = call i8 @f8a()
  %t = icmp eq i8 %c, 0
  br i1 %t, label %then, label %else
then:
  %b = call i8 @f8a()
  br label %join
else:
  %d = call i8 @f8a()
  br label %join
join:
  %p = phi i8 [ %b, %then ], [ %d, %else ]
  %s = add i8 %a, %p
  ret i8 %s
}
