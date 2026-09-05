; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %S/Inputs/oseg-caller.ll -o - | FileCheck %s --check-prefix=CALL
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %S/Inputs/oseg-caller.ll -o - | FileCheck %s --check-prefix=CALL
; RUN: llc -mtriple=mcs251 -filetype=obj %S/Inputs/oseg-caller.ll -o - | FileCheck %s --check-prefix=EXTERN
;
; ASM: .area REG_BANK_0 (OVR,DATA)
; ASM-NEXT: .ds 8
; ASM: .area OSEG (OVR,DATA)
; ASM: _bytes_PARM_2:
; ASM-NEXT: .ds 1
; ASM: _bytes_PARM_3:
; ASM-NEXT: .ds 1
; ASM: .area CSEG (CODE)
; ASM-LABEL: _bytes:
; ASM: .area OSEG (OVR,DATA)
; ASM: _mixed_PARM_2:
; ASM-NEXT: .ds 1
; ASM: _mixed_PARM_3:
; ASM-NEXT: .ds 4
; ASM-LABEL: _mixed:
; ASM: .area OSEG (OVR,DATA)
; ASM: _wide_PARM_2:
; ASM-NEXT: .ds 4
; ASM: _wide_PARM_3:
; ASM-NEXT: .ds 2
; ASM-LABEL: _wide:
; ASM: .area DSEG (DATA)
; ASM: _nested_PARM_2:
; ASM-NEXT: .ds 4
; ASM: _nested_PARM_3:
; ASM-NEXT: .ds 2
; ASM-LABEL: _nested:
; ASM-NOT: ecall
; ASM-DAG: mov {{r[0-9]+}}, a
; ASM-DAG: mov {{r[0-9]+}}, b
; ASM-DAG: mov {{r[0-9]+}}, dph
; ASM-DAG: mov {{r[0-9]+}}, dpl
; ASM: (_nested_PARM_2) >> 8
; ASM: (_wide_PARM_2) >> 8
; ASM: mov @{{dr[0-9]+}}+0x0003,
; ASM: (_wide_PARM_3) >> 8
; ASM: mov @{{dr[0-9]+}}+0x0001,
; ASM: mov dpl,
; ASM: mov dph,
; ASM: mov b,
; ASM: mov a,
; ASM-NEXT: ecall _wide
; ASM: mov {{r[0-9]+}}, @{{dr[0-9]+}}+0x0003
; ASM: (_nested_PARM_3) >> 8
; ASM: .area OSEG (OVR,DATA)
; ASM-NOT: .globl _local_PARM_2
; ASM: _local_PARM_2:
; ASM: .L_private_PARM_2:
; ASM-NOT: .globl .L_private_PARM_2
; ASM: raw_PARM_2:
; ASM-LABEL: _local_user:
; ASM: (_local_PARM_2) >> 8
; ASM: ecall _local
; ASM: (.L_private_PARM_2) >> 8
; ASM: ecall .L_private
; ASM: (raw_PARM_2) >> 8
; ASM: ecall raw
;
; Independent OSEG records start each leaf frame at offset zero.
; OBJ: A OSEG size 2 flags 4 addr 0
; OBJ-NEXT: S _bytes_PARM_2 Def000000
; OBJ-NEXT: S _bytes_PARM_3 Def000001
; OBJ-NEXT: A OSEG size 5 flags 4 addr 0
; OBJ-NEXT: S _mixed_PARM_2 Def000000
; OBJ-NEXT: S _mixed_PARM_3 Def000001
; OBJ-NEXT: A OSEG size 6 flags 4 addr 0
; OBJ-NEXT: S _wide_PARM_2 Def000000
; OBJ-NEXT: S _wide_PARM_3 Def000004
; OBJ-NEXT: A DSEG size 6 flags 0 addr 0
; OBJ-NEXT: S _nested_PARM_2 Def000000
; OBJ-NEXT: S _nested_PARM_3 Def000004
; OBJ-NEXT: A OSEG size 2 flags 4 addr 0
; OBJ-NEXT: A OSEG size 2 flags 4 addr 0
; OBJ-NEXT: A OSEG size 2 flags 4 addr 0
; OBJ-NEXT: S raw_PARM_2 Def000000
; OBJ: A DSEG size 4 flags 0 addr 0
; OBJ-NEXT: S _outer_PARM_2 Def000000
; OBJ: A REG_BANK_0 size 8 flags 4 addr 0
; Byte-of24 relocations to the first slot's area (index 2), NOT CSEG (1).
; OBJ: R 00 00 00 01 {{.*}}F1 81 {{[0-9A-F][0-9A-F]}} 00 02
;
; Literal 0x89abcdef is written MSB first; all stores precede the ABI copy.
; CALL-LABEL: _call_mixed:
; CALL: (_mixed_PARM_2) >> 8
; CALL: mov @{{dr[0-9]+}},
; CALL: mov [[B0:r[0-9]+]], #0x89
; CALL: (_mixed_PARM_3) >> 8
; CALL: mov @[[P:dr[0-9]+]], [[B0]]
; CALL: mov [[B1:r[0-9]+]], #0xab
; CALL: mov @[[P]]+0x0001, [[B1]]
; CALL: mov [[B2:r[0-9]+]], #0xcd
; CALL: mov @[[P]]+0x0002, [[B2]]
; CALL: mov [[B3:r[0-9]+]], #0xef
; CALL: mov @[[P]]+0x0003, [[B3]]
; CALL: mov {{wr[0-9]+}}, #0x1357
; CALL: mov dpl,
; CALL: mov dph,
; CALL-NEXT: ecall _mixed
; CALL-LABEL: _call_wide:
; CALL: mov {{wr[0-9]+}}, #0x5678
; CALL: mov {{wr[0-9]+}}, #0x1234
; CALL: ecall _wide
; EXTERN: S _bytes_PARM_2 Ref000000
; EXTERN: S _mixed_PARM_3 Ref000000
; EXTERN: S _nested_PARM_2 Ref000000
; EXTERN: S _oracle_PARM_3 Ref000000
; EXTERN: A DSEG size 2 flags 0 addr 0
; EXTERN-NEXT: S _remote_PARM_2 Def000000
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
