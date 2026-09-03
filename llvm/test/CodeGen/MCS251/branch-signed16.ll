; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Signed i16 compares, one use case per predicate. Which of jsl/jsge/jsg/
; jsle reaches the assembler is decided by two canonicalisations upstream
; of LowerBR_CC, so the CHECKs below assert measured output, not theory:
;
;  * fallthrough inversion: when the *true* target is the layout successor
;    of the branch's block, SelectionDAGBuilder emits the branch on the
;    inverted condition with swapped targets;
;  * GE/LE -> GT/LT normalisation of the surviving predicate when the
;    immediate allows the c-1/c+1 rewrite.
;
; Boundary immediates do not survive as their source predicate: slt 32767
; and sgt -32768 become ne (cmp #0x7fff / #0x8000 with a jne skip), and
; sge -32768 / sle 32767 fold as always-true. For that reason the slt and
; sle use cases compare against a computed register operand instead: a
; register has no c-1/c+1 rewrite, so the inverted sge / the surviving sle
; keep their GE/LE form -- which is the only way jsl and jsg get emitted
; at all.
;
; Only one i8/i16 argument is available (single-argument ABI), so the
; second compare operand is a constant or a value computed from %v.

define i8 @slt16(i16 %v) {
; CHECK-LABEL: slt16:
; slt v,w with the true target %a as layout successor: inverted to
; sge(v,w); a register operand cannot be GE->GT rewritten, so the sge
; survives and the skip jump is !sge = jsl.
; CHECK: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK: jsl .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
entry:
  %w = and i16 %v, 255
  %c = icmp slt i16 %v, %w
  br i1 %c, label %a, label %b
a:
  ret i8 1
b:
  ret i8 2
}

define i8 @sge16(i16 %v) {
; CHECK-LABEL: sge16:
; sge v,-1 (0xffff) with the true target %a as layout successor: inverted
; to slt v,-1, already an LT form so no further rewrite, skip jump
; !slt = jsge.
; CHECK: cmp wr{{[0-9]+}}, #0xffff
; CHECK: jsge .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
entry:
  %c = icmp sge i16 %v, -1
  br i1 %c, label %a, label %b
a:
  ret i8 1
b:
  ret i8 2
}

define i8 @sgt16(i16 %v) {
; CHECK-LABEL: sgt16:
; sgt v,0 with the *false* target %b as layout successor: no inversion,
; the sgt itself reaches BR_CC (GT is already canonical), skip jump
; !sgt = jsle.
; CHECK: cmp wr{{[0-9]+}}, #0x0000
; CHECK: jsle .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
entry:
  %c = icmp sgt i16 %v, 0
  br i1 %c, label %a, label %b
b:
  ret i8 2
a:
  ret i8 1
}

define i8 @sle16(i16 %v) {
; CHECK-LABEL: sle16:
; sle v,w with the false target %b as layout successor: no inversion, and
; the sle survives because a register operand has no LE->LT rewrite,
; skip jump !sle = jsg.
; CHECK: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK: jsg .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
entry:
  %w = xor i16 %v, 1
  %c = icmp sle i16 %v, %w
  br i1 %c, label %a, label %b
b:
  ret i8 2
a:
  ret i8 1
}
