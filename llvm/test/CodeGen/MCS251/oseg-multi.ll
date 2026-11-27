; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs -filetype=obj -mcs251-object-format=rel %s -o - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -filetype=obj -mcs251-object-format=rel %s -o - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %S/Inputs/oseg-caller.ll -o - | FileCheck %s --check-prefix=CALL
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %S/Inputs/oseg-caller.ll -o - | FileCheck %s --check-prefix=CALL
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %S/Inputs/oseg-caller.ll -o - | FileCheck %s --check-prefix=EXTERN
;
; Stack-based reentrant ABI: continuation arguments arrive on the stack below
; the return address (`@dr60-0x...`) and callers push outgoing continuation
; arguments via `inc spx` / `@dr60` / `dec spx` (no compiler-generated OSEG /
; DSEG `_PARM_` slots).
;
; ASM-NOT: .area OSEG
; ASM-LABEL: _bytes:
; ASM: mov {{r[0-9]+}}, dpl
; ASM: mov {{r[0-9]+}}, @dr60-0x0003
; ASM: mov {{r[0-9]+}}, @dr60-0x0004
; ASM: eret
; ASM-LABEL: _mixed:
; ASM: mov {{r[0-9]+}}, @dr60-0x
; ASM: eret
; ASM-LABEL: _wide:
; ASM: mov {{r[0-9]+}}, @dr60-0x
; ASM: eret
; ASM-LABEL: _nested:
; ASM-NOT: ecall
; ASM-DAG: mov {{r[0-9]+}}, a
; ASM-DAG: mov {{r[0-9]+}}, b
; ASM-DAG: mov {{r[0-9]+}}, dph
; ASM-DAG: mov {{r[0-9]+}}, dpl
; ASM: (_nested_PARM_2) >> 8
; ASM: inc spx, #0x4
; ASM: inc spx, #0x2
; ASM: mov dpl,
; ASM: mov dph,
; ASM: mov b,
; ASM: mov a,
; ASM-NEXT: ecall _wide
; ASM: dec spx, #0x4
; ASM: dec spx, #0x2
; ASM: (_nested_PARM_3) >> 8
; ASM: eret
; ASM-LABEL: _local_user:
; ASM: inc spx, #0x2
; ASM: ecall _local
; ASM: dec spx, #0x2
; ASM: inc spx, #0x2
; ASM: ecall .L_private
; ASM: dec spx, #0x2
; ASM: inc spx, #0x2
; ASM: ecall raw
; ASM: dec spx, #0x2
; ASM: eret
;
; OBJ: A CSEG
; OBJ-NOT: A OSEG
; OBJ-NOT: _bytes_PARM_2
; OBJ-NOT: _mixed_PARM_2
; OBJ-NOT: _wide_PARM_2
; OBJ: S _bytes Def
; OBJ: S _mixed Def
; OBJ: S _wide Def
; OBJ: S _nested Def
; OBJ: S raw Def
;
; Literal 0x89abcdef is written MSB first onto the stack before the ABI copy.
; CALL-LABEL: _call_mixed:
; CALL-NOT: _mixed_PARM_
; CALL: inc spx, #0x4
; CALL: inc spx, #0x1
; CALL-DAG: mov {{r[0-9]+}}, #0x89
; CALL-DAG: mov {{r[0-9]+}}, #0xab
; CALL-DAG: mov {{r[0-9]+}}, #0xcd
; CALL-DAG: mov {{r[0-9]+}}, #0xef
; CALL: mov {{wr[0-9]+}}, #0x1357
; CALL: mov dpl,
; CALL: mov dph,
; CALL-NEXT: ecall _mixed
; CALL: dec spx, #0x4
; CALL: dec spx, #0x1
; CALL-LABEL: _call_wide:
; CALL: inc spx, #0x4
; CALL: inc spx, #0x2
; CALL: ecall _wide
; CALL: dec spx, #0x4
; CALL: dec spx, #0x2
; EXTERN-NOT: _bytes_PARM_2
; EXTERN-NOT: _mixed_PARM_3
; EXTERN-NOT: _nested_PARM_2
; EXTERN-NOT: _oracle_PARM_3
; EXTERN-NOT: _remote_PARM_2
; EXTERN: S _mixed Ref000000
; EXTERN: S _wide Ref000000
;
; Also exercised by Inputs/oseg-qemu.py, with independently compiled callers.
; Distinct leaf frames must overlay, not concatenate, within a module too.

source_filename = "oseg-multi.ll"

define i8 @bytes(i8 %a, i8 %b, i8 %c) noinline {
  %x = xor i8 %a, %b
  %r = add i8 %x, %c
  ret i8 %r
}

define i32 @mixed(i16 %a, i8 %b, i32 %c) noinline {
  %aa = zext i16 %a to i32
  %bb = zext i8 %b to i32
  %x = xor i32 %c, %aa
  %r = add i32 %x, %bb
  ret i32 %r
}

define i32 @wide(i32 %a, i32 %b, i16 %c) noinline {
  %x = xor i32 %a, %b
  %cc = zext i16 %c to i32
  %r = add i32 %x, %cc
  ret i32 %r
}

; Force observable reads of the non-leaf's own slots before AND after B.
; Checking only SSA arguments would hide broken overlay behind entry spills.
@nested_PARM_2 = external global i32
@nested_PARM_3 = external global i16

define i32 @nested(i32 %a, i32 %b, i16 %c) noinline {
  %before = load volatile i32, ptr @nested_PARM_2, align 1
  %v = call i32 @wide(i32 %a, i32 270544960, i16 4951)
  %after = load volatile i32, ptr @nested_PARM_2, align 1
  %word = load volatile i16, ptr @nested_PARM_3, align 1
  %d = xor i32 %before, %after
  %w = xor i16 %word, %c
  %ww = zext i16 %w to i32
  %r0 = add i32 %v, %b
  %r1 = xor i32 %r0, %a
  %r2 = add i32 %r1, %ww
  %r = add i32 %r2, %d
  ret i32 %r
}

define internal i16 @local(i8 %a, i16 %b) noinline {
  %aa = zext i8 %a to i16
  %r = add i16 %aa, %b
  ret i16 %r
}

define private i16 @private(i16 %a, i16 %b) noinline {
  %r = xor i16 %a, %b
  ret i16 %r
}

define i16 @"\01raw"(i16 %a, i16 %b) noinline {
  %r = add i16 %a, %b
  ret i16 %r
}

define i16 @local_user() {
  %a = call i16 @local(i8 37, i16 4660)
  %b = call i16 @private(i16 %a, i16 85)
  %r = call i16 @"\01raw"(i16 %b, i16 7)
  ret i16 %r
}

; Used by the other module; it calls back into this module without recursion.
declare i16 @remote(i16, i16)
define i16 @roundtrip() {
  %r = call i16 @remote(i16 4660, i16 22136)
  ret i16 %r
}

define i16 @callback(i16 %a, i16 %b) noinline {
  %r = xor i16 %a, %b
  ret i16 %r
}

; A second DSEG frame in this module must concatenate, not overlay nested's.
@outer_PARM_2 = external global i32
define i32 @outer(i32 %a, i32 %b) noinline {
  %before = load volatile i32, ptr @outer_PARM_2, align 1
  %r = call i32 @nested(i32 %a, i32 2309737967, i16 9320)
  %after = load volatile i32, ptr @outer_PARM_2, align 1
  %d = xor i32 %before, %after
  %x = add i32 %r, %b
  %v = add i32 %x, %d
  ret i32 %v
}
