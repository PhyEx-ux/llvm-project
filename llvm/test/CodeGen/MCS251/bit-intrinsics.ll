; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
;
; BT03: controlled bit-access intrinsics lower to single bit instructions.
; The bit address is an immarg i32 constant in [0, 255]:
;   set    -> setb bit      clear -> clr bit     toggle -> cpl bit
;   read   -> mov c,bit ; mov a,#0 ; rlc a ; mov dst,a  (atomic sample)
; Bit address 0xd7 names PSW.CY, so it routes to the setb c / clr c / cpl c
; forms (the flag-bearing C mnemonics).
;
; read's sample group is glued (mov c,bit -> mov a,#0 -> rlc a -> mov dst,a),
; so the bit is read exactly once: there must be no second `mov c` for the
; same bit and no byte load of the backing byte.

declare i1 @llvm.mcs251.bit.read(i32 immarg)
declare void @llvm.mcs251.bit.set(i32 immarg)
declare void @llvm.mcs251.bit.clear(i32 immarg)
declare void @llvm.mcs251.bit.toggle(i32 immarg)

; Address edges: 0, 127 (RAM bit space edge), 128, 255 (SFR bit space edge).
;
; The sample group is pinned line-by-line with CHECK-NEXT, and the CHECK-NOTs
; after it span the REST OF THE FUNCTION up to the next CHECK-LABEL, so any
; extra `mov c` (a second sample) or a byte load of the backing byte in this
; function is a hard failure -- not just a comment. (A `mov c,` inserted
; between the pinned lines breaks the CHECK-NEXT chain; one inserted after it
; is caught by CHECK-NOT.)
; CHECK-LABEL: r0:
; CHECK: mov c, 0x00
; CHECK-NEXT: mov a, #0x00
; CHECK-NEXT: rlc a
; CHECK-NEXT: mov r0, a
; CHECK-NEXT: anl r0, #0x01
; CHECK-NOT: mov c,
; CHECK-NOT: mov r0, 0x20
; CHECK-NOT: mov r0, 0x2
define i8 @r0() {
  %b = call i1 @llvm.mcs251.bit.read(i32 0)
  %z = zext i1 %b to i8
  ret i8 %z
}

; CHECK-LABEL: r127:
; CHECK: mov c, 0x7f
; CHECK-NEXT: mov a, #0x00
; CHECK-NEXT: rlc a
; CHECK-NOT: mov c,
define i8 @r127() {
  %b = call i1 @llvm.mcs251.bit.read(i32 127)
  %z = zext i1 %b to i8
  ret i8 %z
}

; CHECK-LABEL: r128:
; CHECK: mov c, 0x80
define i8 @r128() {
  %b = call i1 @llvm.mcs251.bit.read(i32 128)
  %z = zext i1 %b to i8
  ret i8 %z
}

; CHECK-LABEL: r255:
; CHECK: mov c, 0xff
define i8 @r255() {
  %b = call i1 @llvm.mcs251.bit.read(i32 255)
  %z = zext i1 %b to i8
  ret i8 %z
}

; CHECK-LABEL: s5:
; CHECK: setb 0x05
define void @s5() {
  call void @llvm.mcs251.bit.set(i32 5)
  ret void
}

; CHECK-LABEL: c9:
; CHECK: clr 0x09
define void @c9() {
  call void @llvm.mcs251.bit.clear(i32 9)
  ret void
}

; toggle is a single atomic cpl: exactly one instruction in the whole
; function, never a read-then-write pair. The CHECK-NOTs bracket the branch
; from both sides (before and after `eret`) so a duplicated cpl anywhere in
; t33 is rejected, and the target of the CHECK-NOTs is the next function's
; label rather than the end of a small check block.
; CHECK-LABEL: t33:
; CHECK: cpl 0x21
; CHECK-NOT: cpl
; CHECK-NOT: setb
; CHECK-NOT: clr
; CHECK-NOT: mov
; CHECK: eret
; CHECK-NOT: cpl
define void @t33() {
  call void @llvm.mcs251.bit.toggle(i32 33)
  ret void
}

; Bit address 0xd7 is PSW.CY: the flag-bearing C forms are used instead of
; the bit-address forms.
; CHECK-LABEL: carry_set:
; CHECK: setb c
define void @carry_set() {
  call void @llvm.mcs251.bit.set(i32 215)
  ret void
}
; CHECK-LABEL: carry_clr:
; CHECK: clr c
define void @carry_clr() {
  call void @llvm.mcs251.bit.clear(i32 215)
  ret void
}
; CHECK-LABEL: carry_tog:
; CHECK: cpl c
define void @carry_tog() {
  call void @llvm.mcs251.bit.toggle(i32 215)
  ret void
}

; Relative order of side-effecting bit accesses must be preserved (they are
; implicitly volatile and chained), and none may be dropped.
; CHECK-LABEL: order:
; CHECK: setb 0x01
; CHECK-NEXT: cpl 0x02
; CHECK-NEXT: clr 0x03
define void @order() {
  call void @llvm.mcs251.bit.set(i32 1)
  call void @llvm.mcs251.bit.toggle(i32 2)
  call void @llvm.mcs251.bit.clear(i32 3)
  ret void
}

; A read whose result is unused still performs the access (no dead-access
; elimination): the mov c must survive.
; CHECK-LABEL: unused:
; CHECK: mov c, 0x04
define void @unused() {
  %b = call i1 @llvm.mcs251.bit.read(i32 4)
  ret void
}
