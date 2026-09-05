; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -filetype=obj %s -o - | FileCheck %s --check-prefix=OBJ
;
; A non-leaf multi-argument function contributes its own DSEG parameter frame;
; mutable globals contribute a second DSEG slice. The XINIT destination must
; relocate against the global slice (area 3), not the parameter frame (area 2).
;
; ASM: .area DSEG (DATA)
; ASM: _nonleaf_PARM_2:
; ASM-NEXT: .ds 2
; ASM: .area DSEG (DATA)
; ASM: _g:
; ASM-NEXT: .ds 2
; ASM-NEXT: .area XINIT (CODE)
; ASM-NEXT: .word _g
; ASM-NEXT: .word 2
; ASM-NEXT: .word 2
; ASM-NEXT: .word 4951
;
; OBJ: H 6 areas 5 global symbols
; OBJ: A DSEG size 2 flags 0 addr 0
; OBJ-NEXT: S _nonleaf_PARM_2 Def000000
; OBJ-NEXT: A DSEG size 2 flags 0 addr 0
; OBJ-NEXT: S _g Def000000
; OBJ-NEXT: A XINIT size 8 flags 20 addr 0
; OBJ-NEXT: A REG_BANK_0 size 8 flags 4 addr 0
; OBJ: R 00 00 00 04 00 03 00 03

declare void @sink()

@g = global i16 4951, align 1

define i16 @nonleaf(i16 %a, i16 %b) noinline {
  call void @sink()
  %v = load volatile i16, ptr @g, align 1
  %r = add i16 %v, %b
  ret i16 %r
}
