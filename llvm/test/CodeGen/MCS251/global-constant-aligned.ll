; RUN: llc -mtriple=mcs251 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --sections --symbols --section-data %t.o | FileCheck %s --check-prefix=ELF
;
; Read-only integer tables with an IR-level alignment above 1 are accepted
; and emitted byte-aligned (packed) into CSEG: MCS-251 word accesses need no
; address alignment (QEMU + real hardware verified), so the align attribute
; is demoted rather than honored.  Byte order follows the established
; big-endian CSEG scalar rule, and multidimensional tables flatten
; depth-first with the same per-element stride logic as the mutable path.

@bytes4 = constant [4 x i8] c"\11\22\33\44", align 4
@words16 = constant [4 x i16] [i16 258, i16 772, i16 1286, i16 1800], align 16
@longs8 = constant [2 x i32] [i32 305419896, i32 1432778632], align 8
@multi = constant [2 x [2 x i16]] [[2 x i16] [i16 2571, i16 3085], [2 x i16] [i16 3599, i16 4113]], align 16
@multi3 = constant [2 x [2 x [2 x i8]]] [[2 x [2 x i8]] [[2 x i8] c"\AA\BB", [2 x i8] c"\CC\DD"], [2 x [2 x i8]] [[2 x i8] c"\EE\FF", [2 x i8] c"\01\02"]], align 2

; ASM: .area CSEG (CODE)
; ASM: .globl _bytes4
; ASM-NEXT: _bytes4:
; ASM-NEXT: .byte 17
; ASM-NEXT: .byte 34
; ASM-NEXT: .byte 51
; ASM-NEXT: .byte 68
; ASM: _words16:
; ASM-NEXT: .word 258
; ASM-NEXT: .word 772
; ASM-NEXT: .word 1286
; ASM-NEXT: .word 1800
; ASM: _longs8:
; ASM-NEXT: .word 4660
; ASM-NEXT: .word 22136
; ASM-NEXT: .word 21862
; ASM-NEXT: .word 30600
; ASM: _multi:
; ASM-NEXT: .word 2571
; ASM-NEXT: .word 3085
; ASM-NEXT: .word 3599
; ASM-NEXT: .word 4113
; ASM: _multi3:
; ASM-NEXT: .byte 170
; ASM-NEXT: .byte 187
; ASM-NEXT: .byte 204
; ASM-NEXT: .byte 221
; ASM-NEXT: .byte 238
; ASM-NEXT: .byte 255
; ASM-NEXT: .byte 1
; ASM-NEXT: .byte 2

; Total CSEG image is 36 bytes; no padding is inserted anywhere (alignment
; demoted to 1, labels are consecutive).
; REL: A CSEG size 24 flags 20 addr 0
; REL: S _bytes4 Def000000
; REL: S _words16 Def000004
; REL: S _longs8 Def00000C
; REL: S _multi Def000014
; REL: S _multi3 Def00001C
; REL: T 00 00 00 11 22 33 44 01 02 03 04 05 06 07 08 12
; REL: T 00 00 0D 34 56 78 55 66 77 88 0A 0B 0C 0D 0E 0F
; REL: T 00 00 1A 10 11 AA BB CC DD EE FF 01 02

; ELF: Name: .text
; ELF: Size: 36
; ELF: AddressAlignment: 1
; ELF: 0000: 11223344 01020304 05060708 12345678
; ELF: 0010: 55667788 0A0B0C0D 0E0F1011 AABBCCDD
; ELF: 0020: EEFF0102
; ELF: Name: _bytes4
; ELF: Value: 0x0
; ELF: Size: 4
; ELF: Name: _words16
; ELF: Value: 0x4
; ELF: Size: 8
; ELF: Name: _longs8
; ELF: Value: 0xC
; ELF: Size: 8
; ELF: Name: _multi
; ELF: Value: 0x14
; ELF: Size: 8
; ELF: Name: _multi3
; ELF: Value: 0x1C
; ELF: Size: 8

!mcs251.signatures = !{}
