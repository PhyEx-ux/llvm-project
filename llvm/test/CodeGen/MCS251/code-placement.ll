; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/defs.ll -o %t/defs.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/defs.o | FileCheck %s --check-prefix=DEFS
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/tab.ll -o %t/tab.o
; RUN: llvm-readobj --sections --section-data --relocations %t/tab.o | FileCheck %s --check-prefix=TAB
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/struct.ll -o %t/struct.o
; RUN: llvm-readobj --sections --section-data --symbols %t/struct.o | FileCheck %s --check-prefix=STRUCT
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/scalar-align.ll -o %t/sc.o 2>&1 | FileCheck %s --check-prefix=SCALIGN
; RUN: not llc -mtriple=mcs251 -filetype=obj %t/defs.ll -o %t/rel.o 2>&1 | FileCheck %s --check-prefix=REL
;
; X3 placement: an AS4 (__code) definition is a read-only CODE-space image
; emitted in place in CSEG exactly like the ordinary RO path (PROGBITS in the
; text section, any declared array alignment demoted to byte alignment).
; TR18037 address spaces imply no IR constness, so `char code tab[18]` is an
; IR-mutable global that lands in ROM here; an uninitialized/tentative
; definition is the ROM zero image. Writes were already rejected fail-closed
; by X2; this slice does not redo them.
; AS4 struct aggregates are accepted here since the AS4-AGGREGATE slice
; (design AS4-AGGREGATE-INIT-DESIGN §6A); AS0 stays array/scalar only.

; DEFS: Name: .text
; DEFS: SectionData (
; The eret of the trivial function, then the 6-byte image, then the 16-byte
; ROM zero image, then the i16 scalar 00 37 (big-endian).
; DEFS-NEXT:     0000: AA414243 44454600 00000000 00000000  |.ABCDEF.........|
; DEFS-NEXT:     0010: 00000000 00000000 37                 |........7|
; DEFS-NEXT:   )
; DEFS: Name: _devicedesc
; DEFS-NEXT: Value: 0x1
; DEFS-NEXT: Size: 6
; DEFS-NEXT: Binding: Global (0x1)
; DEFS-NEXT: Type: Object (0x1)
; DEFS: Name: _table16
; DEFS-NEXT: Value: 0x7
; DEFS-NEXT: Size: 16
; DEFS: Name: _answer
; DEFS-NEXT: Value: 0x17
; DEFS-NEXT: Size: 2

; The X1 string-table form: the AS0 mutable table goes to DSEG+XINIT and
; its AS4 pointer leaves live in the record payload; the AS4 table's image
; is in .text. Every leaf is a BIG-ENDIAN 4-byte container whose low 24
; bits are the canonical address (zero most-significant byte at the leaf
; offset, then the three-byte R_MCS251_24 field at +1..+3), against the
; string's folded .text+1 address. The XINIT payload starts at record
; offset 6 (containers at 6..9 and 10..13, fields at 0x7 and 0xB); the
; .text leaf container sits right after the 6-byte string image (field at
; 0x7).
; TAB: Name: .text
; TAB: SectionData (
; TAB-NEXT:     0000: AA626F6F 74000000 0000               |.boot.....|
; TAB-NEXT:   )
; TAB: Name: .mcs251.xinit
; TAB: SectionData (
; TAB-NEXT:     0000: 00000008 00080000 00000000 0000      |..............|
; TAB-NEXT:   )
; TAB: 0x7 R_MCS251_24 .text 0x1
; TAB: R_MCS251_16 _plotmodetxt 0x0
; TAB: 0x7 R_MCS251_24 .text 0x1
; TAB: 0xB R_MCS251_24 .text 0x1

; STRUCT: Name: .text
; STRUCT: SectionData (
; STRUCT-NEXT:     0000: AA010002 |....|
; STRUCT-NEXT:   )
; STRUCT: Name: _s
; STRUCT-NEXT: Value: 0x1
; STRUCT-NEXT: Size: 3
; SCALIGN: LLVM ERROR: MCS251: __code global 'a': non-array storage must be byte-aligned (arrays of any declared alignment are emitted byte-aligned)
; REL: LLVM ERROR: MCS251: __code global 'devicedesc': storage requires ELF object output

;--- defs.ll
@devicedesc = addrspace(4) global [6 x i8] c"ABCDEF", align 1
@table16 = addrspace(4) global [8 x i16] zeroinitializer, align 16
@answer = addrspace(4) global i16 55, align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- tab.ll
@msg = private unnamed_addr addrspace(4) constant [5 x i8] c"boot\00", align 1
@plotmodetxt = global [2 x ptr addrspace(4)] [ptr addrspace(4) @msg, ptr addrspace(4) @msg], align 1
@romtab = addrspace(4) global [1 x ptr addrspace(4)] [ptr addrspace(4) @msg], align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- struct.ll
@s = addrspace(4) global { i8, i16 } { i8 1, i16 2 }

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- scalar-align.ll
@a = addrspace(4) global i16 4951, align 2

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
