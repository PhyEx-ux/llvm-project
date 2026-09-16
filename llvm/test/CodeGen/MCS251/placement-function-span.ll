; G11-B: the entity span of a placed function (design rev 7 §3.2, the
; "entity span" ruling; the span-jumptable fixture of §9 probe4 as a strong
; lit assertion -- printed values do not count as acceptance, these are hard
; assertions).
;
; The entity span is the SECTION span sh_size: the function body PLUS the
; jump-table columns the BRJT emitter appends in the same section after the
; body (emitJumpTableInfo runs at the very end of emitFunctionBody).  The
; defined symbol's st_size keeps the body-label span, so for a function
; with a jump table:
;   NOTE.size == sh_size >= st_size   (equality iff no same-section payload)
; NOTE.size is produced by the MC symbol difference <stable>.end -
; <stable>.begin (both temporary notype labels of the fixed section), a
; fixup that only resolves after the final layout -- proving the writer
; never bakes in a pre-layout constant.  The jump-table J16 relocations
; land inside .mcu.fixed.spr itself (payload attribution: the entity that
; emitted them owns their bytes).
;
; RUN: llc -mtriple=mcs251 -O2 -mcs251-jump-tables -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --sections --symbols --relocations %t.o | FileCheck %s --check-prefix=ELF
;
; ELF: Name: .mcu.fixed.spr
; ELF: Type: SHT_PROGBITS
; ELF: Flags [ (0x6)
; ELF: Size: 197
; ELF: Section ({{[0-9]+}}) .rela.mcu.fixed.spr {
; ELF: 0x20 R_MCS251_J16 .mcu.fixed.spr 0xB9
; ELF: 0xBA R_MCS251_J16 .mcu.fixed.spr
; ELF: 0xBD R_MCS251_J16 .mcu.fixed.spr
; ELF: 0xC0 R_MCS251_J16 .mcu.fixed.spr
; ELF: 0xC3 R_MCS251_J16 .mcu.fixed.spr
; ELF: Name: _spr
; ELF: Value: 0x0
; ELF: Size: 185
; ELF: Type: Function
; ELF: Section: .mcu.fixed.spr
; The table column: the J16 relocations live in .rela.mcu.fixed.spr (the
; placeholder bytes are inside the entity's own section), the first table
; entry field at r_offset 0xBA -- after the 185 body bytes (0xB9).
;
; NOTE.size == sh_size (197) and the strict span ordering -- decoded and
; asserted by the independent reader (a FAIL, never a SKIP).
; RUN: %python %S/Inputs/check-placement-note.py %t.o spr 2 1 0 0xFC6000 197 1 0

target triple = "mcs251-unknown-none"

@glob = external dso_local global i32, align 1

define void @spr(i32 noundef %x) addrspace(4) #0 {
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c0:
  store volatile i32 10, ptr @glob, align 1
  ret void
c1:
  store volatile i32 11, ptr @glob, align 1
  ret void
c2:
  store volatile i32 12, ptr @glob, align 1
  ret void
c3:
  store volatile i32 13, ptr @glob, align 1
  ret void
default:
  store volatile i32 99, ptr @glob, align 1
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_spr", i32 1, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0xFC6000,code,function,owned,0" "mcs251-stable-symbol"="spr" }
