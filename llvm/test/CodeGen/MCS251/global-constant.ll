; RUN: llc -mtriple=mcs251 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
;
; Data-only modules must select the same CSEG MCSection as functions. Mixed
; modules, private references and SDCC __code reads are independently exercised
; by the P-C QEMU matrix (not a dependency of lit).

@_bytes = constant [8 x i8] c"\13\57\89\AB\00\22\5C\FF", align 1
@_word = constant i16 4951, align 1
@_long = constant i32 2309737967, align 1
@_words = constant [2 x i16] [i16 9320, i16 44256], align 1
@_string = constant [3 x i8] c"Hi\00", align 1
@_repeated = constant [3 x i8] c"\A5\A5\A5", align 1
@_zero = constant i8 0, align 1

; ASM: .area CSEG (CODE)
; ASM: .globl _bytes
; ASM-NEXT: _bytes:
; ASM-NEXT: .byte 19
; ASM-NEXT: .byte 87
; ASM-NEXT: .byte 137
; ASM-NEXT: .byte 171
; ASM-NEXT: .byte 0
; ASM-NEXT: .byte 34
; ASM-NEXT: .byte 92
; ASM-NEXT: .byte 255
; ASM: _word:
; ASM-NEXT: .word 4951
; ASM: _long:
; ASM-NEXT: .word 35243
; ASM-NEXT: .word 52719
; ASM: _words:
; ASM-NEXT: .word 9320
; ASM-NEXT: .word 44256
; ASM: _string:
; ASM-NEXT: .byte 72
; ASM-NEXT: .byte 105
; ASM-NEXT: .byte 0
; ASM: _repeated:
; ASM-NEXT: .byte 165
; ASM-NEXT: .byte 165
; ASM-NEXT: .byte 165
; ASM: _zero:
; ASM-NEXT: .byte 0
; OBJ: A CSEG size 19 flags 20 addr 0
; OBJ: S _bytes Def000000
; OBJ: S _word Def000008
; OBJ: S _long Def00000A
; OBJ: S _words Def00000E
; OBJ: S _string Def000012
; OBJ: S _repeated Def000015
; OBJ: S _zero Def000018
; OBJ: T 00 00 00 13 57 89 AB 00 22 5C FF 13 57 89 AB CD
; OBJ: T 00 00 0D EF 24 68 AC E0 48 69 00 A5 A5 A5 00
