; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
;
; Mutable globals have one NOBITS DSEG allocation and one sparse XINIT record:
;   u16 DSEG address, u16 object size, u16 payload size, payload bytes.
; Zero-only objects have payload size zero, so a large BSS object never becomes
; a same-sized ROM blob. Nonzero scalar/aggregate payloads use target big endian.
;
; ASM: .area REG_BANK_0 (OVR,DATA)
; ASM-NEXT: .ds 8
; ASM: .area CSEG (CODE)
; ASM-LABEL: _touch:
; ASM: .area DSEG (DATA)
; ASM: .globl _zero8
; ASM-NEXT: _zero8:
; ASM-NEXT: .ds 1
; ASM-NEXT: .area XINIT (CODE)
; ASM-NEXT: .word _zero8
; ASM-NEXT: .word 1
; ASM-NEXT: .word 0
; ASM: .area DSEG (DATA)
; ASM: _init16:
; ASM-NEXT: .ds 2
; ASM-NEXT: .area XINIT (CODE)
; ASM-NEXT: .word _init16
; ASM-NEXT: .word 2
; ASM-NEXT: .word 2
; ASM-NEXT: .word 4951
; ASM: _init32:
; ASM-NEXT: .ds 4
; ASM: .word 4
; ASM-NEXT: .word 4
; ASM-NEXT: .word 35243
; ASM-NEXT: .word 52719
; ASM: _tab:
; ASM-NEXT: .ds 5
; ASM: .word 5
; ASM-NEXT: .word 5
; ASM-NEXT: .byte 1
; ASM-NEXT: .byte 2
; ASM-NEXT: .byte 0
; ASM-NEXT: .byte 254
; ASM-NEXT: .byte 255
; ASM: _bigzero:
; ASM-NEXT: .ds 64
; ASM: .word 64
; ASM-NEXT: .word 0
; ASM: _record:
; ASM-NEXT: .ds 7
; ASM: .word 7
; ASM-NEXT: .word 7
; ASM-NEXT: .byte 170
; ASM-NEXT: .word 9320
; ASM-NEXT: .word 35243
; ASM-NEXT: .word 52719
; ASM: _latezero:
; ASM-NEXT: .ds 2
; ASM: .word 2
; ASM-NEXT: .word 0
; ASM: .area CSEG (CODE)
;
; Direct REL output represents RAM as a non-loadable DSEG A record and the ROM
; table as a CODE-class XINIT A record. DSEG is area index 2 and XINIT index 3;
; the first R line below relocates both target fields against DSEG, not CSEG.
; OBJ: H 5 areas 9 global symbols
; OBJ: A CSEG size {{[0-9A-F]+}} flags 20 addr 0
; OBJ-NEXT: S _touch Def000000
; OBJ-NEXT: A DSEG size 55 flags 0 addr 0
; OBJ-NEXT: S _zero8 Def000000
; OBJ-NEXT: S _init16 Def000001
; OBJ-NEXT: S _init32 Def000003
; OBJ-NEXT: S _tab Def000007
; OBJ-NEXT: S _bigzero Def00000C
; OBJ-NEXT: S _record Def00004C
; OBJ-NEXT: S _latezero Def000053
; OBJ-NEXT: A XINIT size 3C flags 20 addr 0
; OBJ-NEXT: A REG_BANK_0 size 8 flags 4 addr 0
; OBJ: T 00 00 00 00 00 00 01 00 00 00 01 00 02 00 02 13
; OBJ-NEXT: R 00 00 00 03 00 03 00 02 00 09 00 02
; OBJ: 89 AB CD EF
; OBJ: 01 02 00 FE FF
; OBJ: T {{.*}}AA 24 68 89 AB
; OBJ: T {{.*}}CD EF

source_filename = "global-data.c"

@zero8 = global i8 0, align 1
@init16 = global i16 4951, align 1
@init32 = global i32 2309737967, align 1
@tab = global [5 x i8] [i8 1, i8 2, i8 0, i8 254, i8 255], align 1
@bigzero = global [64 x i8] zeroinitializer, align 1
@record = global { i8, i16, i32 } { i8 170, i16 9320, i32 2309737967 }, align 1
@latezero = global i16 0, align 1

define i16 @touch() {
  %a = load volatile i16, ptr @init16, align 1
  store volatile i16 4660, ptr @init16, align 1
  %b = load volatile i8, ptr @zero8, align 1
  %z = zext i8 %b to i16
  %r = add i16 %a, %z
  ret i16 %r
}
