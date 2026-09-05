; RUN: llc -mtriple=mcs251 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
;
; Defined globals are deliberately unsupported, but function prefix data goes
; through generic AsmPrinter constant emission. Test scalar, array, struct and
; 3-byte MCAsmStreamer fallback, without enabling global data-area emission.
; Packed size = 2 + 4 + 3 + 8 + 4 = 21 bytes. Prefix bytes are independently
; read back in QEMU by validation/mcs251-endian/run.py.
;
; ASM: .word 4951
; ASM-NEXT: .word 35243
; ASM-NEXT: .word 52719
; ASM-NEXT: .word 41394
; ASM-NEXT: .byte 195
; ASM-NEXT: .word 291
; ASM-NEXT: .word 17767
; ASM-NEXT: .word 35243
; ASM-NEXT: .word 52719
; ASM-NEXT: .word 9320
; ASM-NEXT: .word 44256
; ASM-NEXT: _prefix_anchor:
; OBJ: T 00 00 00 13 57 89 AB CD EF A1 B2 C3 01 23 45 67
; OBJ: T 00 00 0D 89 AB CD EF 24 68 AC E0 AA

define void @prefix_anchor() prefix <{i16, i32, i24, i64, [2 x i16]}> <{i16 4951, i32 2309737967, i24 10597059, i64 81985529216486895, [2 x i16] [i16 9320, i16 44256]}> {
  ret void
}

declare void @asm_anchor()

define i8 @asm_byte(i8 %index) {
  %offset = zext i8 %index to i32
  %start = getelementptr i8, ptr @asm_anchor, i32 -5
  %p = getelementptr i8, ptr %start, i32 %offset
  %r = load volatile i8, ptr %p, align 1
  ret i8 %r
}

define i8 @prefix_byte(i8 %index) {
  %offset = zext i8 %index to i32
  %start = getelementptr i8, ptr @prefix_anchor, i32 -21
  %p = getelementptr i8, ptr %start, i32 %offset
  %r = load volatile i8, ptr %p, align 1
  ret i8 %r
}
