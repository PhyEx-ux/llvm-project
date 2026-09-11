; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/crossblock.ll -o - | FileCheck %s --check-prefix=CROSS
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/sext.ll -o - | FileCheck %s --check-prefix=SEXT
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/select-alias.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FAIL

; P12-1 / P12-2 (Alice review round 3) regression pins for the two
; optnone-substitution proofs of instruction selection:
;
;   * the "slot is dead" verdict and the parked-constant resolution are
;     FUNCTION-level. A SelectionDAG covers one basic block, so the old
;     DAG-local FI-user scan misjudged slots that successor blocks read
;     (crossblock_live returned 0 instead of 25) or whose address escaped
;     (crossblock_escape handed observe() a zeroed slot), and it could not
;     resolve constants parked in the entry block and reloaded in a
;     successor at all.
;   * constant reconstruction of an extending load honors
;     getExtensionType(): `sext i8 -100` used to rebuild +156 and fold the
;     i64 sdiv to 39 instead of -25.
;
; All functions keep the exact -O0 optnone shape; only instruction
; selection's local substitutions run.
;
; P12-7 (Alice review round 4): the CROSS checks pin the DATA-FLOW CHAIN,
; not just the bare `#0x19` immediate -- every parked/folded constant must
; be materialized into r1 and the very next instruction must write that
; register into its slot byte, and the read/observe path must reload the
; same slot. Rewriting any store source register (e.g. r1 -> r0, 12 sites)
; now fails the RUN.

;--- crossblock.ll
; CROSS-LABEL: _crossblock_live:
; CROSS: mov r1, #0x64
; CROSS-NEXT: mov @dr60-0x0011, r1
; CROSS: mov r1, #0x04
; CROSS-NEXT: mov @dr60-0x0009, r1
; CROSS: mov r1, #0x19
; CROSS-NEXT: mov @dr60-0x0001, r1
; CROSS: mov r0, @dr60-0x0004
; CROSS-NEXT: mov r2, @dr60-0x0003
; CROSS: eret
define i32 @crossblock_live() noinline optnone {
entry:
  %q = alloca i64, align 1
  %r = alloca i64, align 1
  %out = alloca i64, align 1
  store i64 100, ptr %q, align 1
  store i64 4, ptr %r, align 1
  br label %calc

calc:
  %a = load i64, ptr %q, align 1
  %b = load i64, ptr %r, align 1
  %v = udiv i64 %a, %b
  store i64 %v, ptr %out, align 1
  br label %read

read:
  %z = load i64, ptr %out, align 1
  %t = trunc i64 %z to i32
  ret i32 %t
}

; The slot address escapes into the call: the stored result stays
; observable, so the parked operands must still resolve (100/4 -> 25) and
; 25 must reach the frame, never a substituted zero.
declare void @observe(ptr)
; CROSS-LABEL: _crossblock_escape:
; CROSS: mov r1, #0x64
; CROSS-NEXT: mov @dr60-0x0013, r1
; CROSS: mov r1, #0x04
; CROSS-NEXT: mov @dr60-0x000b, r1
; CROSS: mov r1, #0x19
; CROSS-NEXT: mov @dr60-0x0003, r1
; CROSS: ecall _observe
; CROSS: eret
define i32 @crossblock_escape() noinline optnone {
entry:
  %q = alloca i64, align 1
  %r = alloca i64, align 1
  %out = alloca i64, align 1
  store i64 100, ptr %q, align 1
  store i64 4, ptr %r, align 1
  br label %calc

calc:
  %a = load i64, ptr %q, align 1
  %b = load i64, ptr %r, align 1
  %v = udiv i64 %a, %b
  store i64 %v, ptr %out, align 1
  br label %read

read:
  call void @observe(ptr %out)
  ret i32 0
}

; Partial read: only the low 4 bytes of the stored result are loaded.
; CROSS-LABEL: _crossblock_partial:
; CROSS: mov r1, #0x64
; CROSS-NEXT: mov @dr60-0x0011, r1
; CROSS: mov r1, #0x04
; CROSS-NEXT: mov @dr60-0x0009, r1
; CROSS: mov r1, #0x19
; CROSS-NEXT: mov @dr60-0x0001, r1
; CROSS: mov r0, @dr60-0x0008
; CROSS-NEXT: mov r2, @dr60-0x0007
; CROSS: eret
define i32 @crossblock_partial() noinline optnone {
entry:
  %q = alloca i64, align 1
  %r = alloca i64, align 1
  %out = alloca i64, align 1
  store i64 100, ptr %q, align 1
  store i64 4, ptr %r, align 1
  br label %calc

calc:
  %a = load i64, ptr %q, align 1
  %b = load i64, ptr %r, align 1
  %v = udiv i64 %a, %b
  store i64 %v, ptr %out, align 1
  br label %read

read:
  %t = load i32, ptr %out, align 1
  ret i32 %t
}

; The phi keeps the slot address alive across blocks; with the parked
; operands resolved the udiv folds to 25 and the load reads it back.
; CROSS-LABEL: _crossblock_phi:
; CROSS: mov r1, #0x64
; CROSS-NEXT: mov @dr60-0x0015, r1
; CROSS: mov r1, #0x04
; CROSS-NEXT: mov @dr60-0x000d, r1
; CROSS: mov r1, #0x19
; CROSS-NEXT: mov @dr60-0x0005, r1
; CROSS: mov wr12, @dr60-0x0003
; CROSS: mov r0, @dr12+0x0004
; CROSS: eret
define i32 @crossblock_phi() noinline optnone {
entry:
  %q = alloca i64, align 1
  %r = alloca i64, align 1
  %out = alloca i64, align 1
  store i64 100, ptr %q, align 1
  store i64 4, ptr %r, align 1
  br label %calc

calc:
  %a = load i64, ptr %q, align 1
  %b = load i64, ptr %r, align 1
  %v = udiv i64 %a, %b
  store i64 %v, ptr %out, align 1
  br label %read

read:
  %alias = phi ptr [ %out, %calc ]
  %z = load i64, ptr %alias, align 1
  %t = trunc i64 %z to i32
  ret i32 %t
}

;--- sext.ll
; SEXTLOAD i8 -> i64 with a negative byte: -100 / 4 = -25 (0xE7 in the low
; lane after the signed fold; the old zero-extending rebuild produced 39).
; SEXT-LABEL: _sext8:
; SEXT: mov dr4, #0xffe7
; SEXT: movh dr4, #0xffff
; SEXT: eret
define i32 @sext8() noinline optnone {
entry:
  %p = alloca i8, align 1
  store i8 -100, ptr %p, align 1
  %v = load i8, ptr %p, align 1
  %w = sext i8 %v to i64
  %a = sdiv i64 %w, 4
  %t = trunc i64 %a to i32
  ret i32 %t
}

; SEXTLOAD i32 -> i64: same -25 result at word width (the old rebuild
; produced 0x3fffffe7).
; SEXT-LABEL: _sext32:
; SEXT: mov dr4, #0xffe7
; SEXT: movh dr4, #0xffff
; SEXT: eret
define i32 @sext32() noinline optnone {
entry:
  %p = alloca i32, align 1
  store i32 -100, ptr %p, align 1
  %v = load i32, ptr %p, align 1
  %w = sext i32 %v to i64
  %a = sdiv i64 %w, 4
  %t = trunc i64 %a to i32
  ret i32 %t
}

; ZEXT control: 0xFF01 zext to i64, * 257 = 0x1000001, trunc to i32 -> 1.
; The zero-extension semantics must keep holding alongside the fix.
; SEXT-LABEL: _zext16:
; SEXT: mov r0, #0xff
; SEXT: mov r0, #0x01
; SEXT: eret
define i32 @zext16() noinline optnone {
entry:
  %p = alloca i16, align 1
  store i16 65281, ptr %p, align 1
  %v = load i16, ptr %p, align 1
  %w = zext i16 %v to i64
  %a = mul i64 %w, 257
  %t = trunc i64 %a to i32
  ret i32 %t
}

;--- select-alias.ll
; Control: a select of two slot addresses is an unprovable alias; the
; parked operands cannot be resolved for the q slot (its address escapes
; into the select), so instruction selection must fail closed rather than
; guess.
; FAIL: LLVM ERROR: MCS251 contract violation: i64 integer arithmetic is not yet implemented; wide-integer runtime is not connected
define i32 @crossblock_select(i1 %c) noinline optnone {
entry:
  %q = alloca i64, align 1
  %r = alloca i64, align 1
  %out = alloca i64, align 1
  store i64 100, ptr %q, align 1
  store i64 4, ptr %r, align 1
  br label %calc

calc:
  %a = load i64, ptr %q, align 1
  %b = load i64, ptr %r, align 1
  %v = udiv i64 %a, %b
  store i64 %v, ptr %out, align 1
  br label %read

read:
  %alias = select i1 %c, ptr %out, ptr %q
  %z = load i64, ptr %alias, align 1
  %t = trunc i64 %z to i32
  ret i32 %t
}
