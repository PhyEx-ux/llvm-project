; RUN: llc -mtriple=mcs251 -O0 -filetype=obj %s -o %t.rel
; RUN: FileCheck %s --check-prefix=REL < %t.rel
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --sections --symbols --relocations --section-data %t.o | FileCheck %s --check-prefix=ELF
; RUN: %python %S/Inputs/check-elf-rela.py %t.o %t.rel
; RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %s -o %t.o2
; RUN: llvm-readobj --sections --symbols --relocations --section-data %t.o2 | FileCheck %s --check-prefix=ELF
; RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf -compile-twice %s -o %t.twice.o
; RUN: cmp %t.o2 %t.twice.o
;
; Each leaf retains a distinct NOBITS section starting at offset zero; the
; linker will overlay max(2,6) bytes. Non-leaf slots and mutable storage are
; independent DSEG slices. Initializers are a sparse ROM table, not .data.
@g = global i16 4951, align 1
@zero = global [64 x i8] zeroinitializer, align 1
@aggregate = global { i8, i32 } { i8 66, i32 305419896 }, align 1

declare void @sink()

define i16 @leaf(i16 %a, i16 %b) noinline {
  %r = xor i16 %a, %b
  ret i16 %r
}

define i32 @wide(i32 %a, i32 %b, i16 %c) noinline {
  %d = zext i16 %c to i32
  %r = add i32 %a, %b
  %s = xor i32 %r, %d
  ret i32 %s
}

define i16 @nonleaf(i16 %a, i16 %b) noinline {
  call void @sink()
  %v = load volatile i16, ptr @g, align 1
  %r = add i16 %v, %b
  ret i16 %r
}

; REL: A OSEG size 2 flags 4 addr 0
; REL: A OSEG size 6 flags 4 addr 0
; REL: A DSEG size 2 flags 0 addr 0
; REL: A DSEG size 47 flags 0 addr 0
; REL: A XINIT size 19 flags 20 addr 0
; REL: A REG_BANK_0 size 8 flags 4 addr 0
;
; ELF: Name: .mcs251.REG_BANK_0
; ELF: Type: SHT_NOBITS
; ELF: SHF_MCS251_OVERLAY
; ELF: Size: 8
; ELF: Name: .mcs251.OSEG.0
; ELF: Type: SHT_NOBITS
; ELF: SHF_MCS251_OVERLAY
; ELF: Size: 2
; ELF: Name: .mcs251.OSEG.1
; ELF: Type: SHT_NOBITS
; ELF: SHF_MCS251_OVERLAY
; ELF: Size: 6
; ELF: Name: .mcs251.DSEG.2
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 2
; ELF: Name: .mcs251.dseg
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 71
; ELF: Name: .mcs251.xinit
; ELF: Type: SHT_PROGBITS
; ELF: Size: 25
; ELF: 0000: 00000002 00021357 00000040 00000000
; ELF: 0010: 00050005 42123456 78
; ELF: Relocations [
; ELF: Section {{.*}} .rela.mcs251.xinit {
; ELF-NEXT: 0x0 R_MCS251_16 _g 0x0
; ELF-NEXT: 0x8 R_MCS251_16 _zero 0x0
; ELF-NEXT: 0xE R_MCS251_16 _aggregate 0x0
; ELF: Symbols [
; ELF: Name: _leaf_PARM_2
; ELF: Value: 0x0
; ELF: Size: 2
; ELF: Type: Object

!mcs251.signatures = !{!10000, !10001, !10002, !10003}
!10000 = !{!"_sink", i32 2, i32 0}
!10001 = !{!"_leaf", i32 1, i32 0, i32 0, i32 0}
!10002 = !{!"_wide", i32 1, i32 0, i32 0, i32 0, i32 0}
!10003 = !{!"_nonleaf", i32 1, i32 0, i32 0, i32 0}
