; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 6: BR_CC (branch on icmp) lowering. Every conditional branch is the
; three-part long-branch form
;
;     jCCinv  skip      ; rel8: the ejmp is 4 bytes below, always in range
;     ejmp    target    ; addr24, reaches anywhere
;   skip:
;
; because the jcc family only reaches +/-128 bytes and this backend has no
; branch relaxation infrastructure. The examples below are written so the
; DAG combiner cannot fold the compares away; note that it does canonicalise
; conditions (e.g. `ult x,10` becomes the complementary `ugt x,9` with
; swapped branch destinations), which is why the checked jcc opcodes are
; the complements of the source conditions.
;
; icmp->jcc map (QEMU-verified): eq->JE ne->JNE ult->JC uge->JNC
; ugt->JG ule->JLE slt->JSL sge->JSGE sgt->JSG sle->JSLE.
; Only one i8/i16 argument is available (single-argument ABI), so second
; compare operands are constants.

define void @threeway(i8 %x) {
; CHECK-LABEL: threeway:
; %bb.0: `ult x,10 -> lo` canonicalised to the complement `ugt x,9`,
; so the skip jump is jle (!ugt = ule).
; CHECK: cmp r[[R0:[0-9]+]], #0x09
; CHECK: jle .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; %lo: `ugt x,2 -> mid` canonicalised to the complement `ult x,3`,
; skip jump jnc (!ult = uge).
; CHECK: cmp r[[R1:[0-9]+]], #0x03
; CHECK: jnc .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; %mid: `uge x,6 -> hi` canonicalised to the complement `ult x,6`,
; skip jump jnc (!ult = uge).
; CHECK: cmp r[[R2:[0-9]+]], #0x06
; CHECK: jnc .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; CHECK: eret
entry:
  %c1 = icmp ult i8 %x, 10
  br i1 %c1, label %lo, label %mid
lo:
  %c2 = icmp ugt i8 %x, 2
  br i1 %c2, label %mid, label %hi
mid:
  %c3 = icmp uge i8 %x, 6
  br i1 %c3, label %hi, label %exit
hi:
  ret void
exit:
  ret void
}

define void @eqne16(i16 %v) {
; CHECK-LABEL: eqne16:
; eq 1234 (0x04d2): fallthrough inversion swaps the targets and turns the
; eq into ne, so the skip jump is !ne = je.
; CHECK: cmp wr{{[0-9]+}}, #0x04d2
; CHECK: je .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; ne 4321 (0x10e1): the same fallthrough inversion turns the ne into eq,
; so the skip jump is !eq = jne.
; CHECK: cmp wr{{[0-9]+}}, #0x10e1
; CHECK: jne .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; uge 100 canonicalised to the complement ugt 99, skip jump jle.
; CHECK: cmp wr{{[0-9]+}}, #0x0063
; CHECK: jle .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; CHECK: eret
entry:
  %c1 = icmp eq i16 %v, 1234
  br i1 %c1, label %a, label %b
a:
  %c2 = icmp ne i16 %v, 4321
  br i1 %c2, label %b, label %exit
b:
  %c3 = icmp uge i16 %v, 100
  br i1 %c3, label %exit, label %b2
b2:
  ret void
exit:
  ret void
}

define void @sgn8(i8 %x) {
; CHECK-LABEL: sgn8:
; Signed i8 compares: the N-flag behaviour at 8-bit width is not measured,
; so both operands are widened to 16 bits with the offset-binary bias
; 0x00:(x^0x80) (x <s y  <=>  x^0x80 <u y^0x80) and compared with cmp
; wr,wr. The bias bytes land in the lanes of a fresh wr (REG_SEQUENCE).
; CHECK: mov r{{[0-9]+}}, #0xfc
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; `slt x,-3 -> a` canonicalised to the complement `sgt x,-4` (0xfc),
; skip jump jle.
; CHECK: jle .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; Second branch, same widened shape. MachineCSE reuses the first branch's
; flipped lhs word (it dominates), so only the flipped constant (0x9b is
; the canonicalised -101 of `sge x,-100`) is materialised here.
; CHECK: mov r{{[0-9]+}}, #0x9b
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK: jle .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; CHECK: eret
entry:
  %c1 = icmp slt i8 %x, -3
  br i1 %c1, label %a, label %b
a:
  %c2 = icmp sge i8 %x, -100
  br i1 %c2, label %b, label %a2
a2:
  ret void
b:
  ret void
}

define i8 @eqne8(i8 %x) {
; CHECK-LABEL: eqne8:
; i8 eq/ne take the plain CMP8ri+BRCC path (no signed widening). First
; branch: eq 42 (0x2a) with the true target %a as layout successor is
; inverted to ne, so the skip jump is !ne = je.
; CHECK: cmp r{{[0-9]+}}, #0x2a
; CHECK: je .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; Second branch: eq 99 (0x63) with the true target %b2 *not* the layout
; successor is not inverted, so the eq itself reaches BR_CC and the skip
; jump is !eq = jne.
; CHECK: cmp r{{[0-9]+}}, #0x63
; CHECK: jne .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; CHECK: eret
entry:
  %c1 = icmp eq i8 %x, 42
  br i1 %c1, label %a, label %b
a:
  %c2 = icmp eq i8 %x, 99
  br i1 %c2, label %b2, label %exit
b:
  ret i8 1
b2:
  ret i8 2
exit:
  ret i8 3
}

define void @countdown(i16 %n) {
; CHECK-LABEL: countdown:
; Loop back edge: `icmp ne %i.next, 0` canonicalised to eq with an inverted
; branch, so the skip jump is je and the ejmp carries the back edge.
; CHECK: cmp wr{{[0-9]+}}, #0x0000
; CHECK: je .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; CHECK: eret
entry:
  br label %loop
loop:
  %i = phi i16 [ %n, %entry ], [ %i.next, %loop ]
  %i.next = add i16 %i, -1
  %c = icmp ne i16 %i.next, 0
  br i1 %c, label %loop, label %done
done:
  ret void
}
