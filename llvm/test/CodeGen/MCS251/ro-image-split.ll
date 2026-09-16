; P09 textdecode fix (producer-side RO split): the read-only byte images of a
; module that carries persistent bit objects must leave .text.  lld's
; BT13/BT15 bit validation decodes the WHOLE .text of any section carrying
; R_MCS251_BITADDR8 as an instruction stream (input bytes and final image), so
; a string constant or `__code` image appended to .text would make the module
; unlinkable ("not a decodable instruction stream" at the string's offset).
; The producer keeps .text a pure instruction stream by diverting the RO
; images of such a module into .rodata (MCS251AsmPrinter
; selectROImageSection).  lld classifies .rodata into the SAME CSEG region as
; .text, so CODE-space semantics, the HI8/MID8/LO8/24 addressing channels and
; the area accounting are unchanged.
;
; The condition key is the module-level bit-object property (any global
; carrying the `mcs251-bit-object` attribute) AND ELF object output.  The
; negative cases below pin the complement: a module with no bit object keeps
; the historical single-CSEG layout byte for byte, and a bit module's REL/asm
; streams (no bit protocol there) are rejected before any layout applies.

; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bit.ll -o %t/bit.o
; RUN: llvm-readobj --sections --section-data --symbols --relocations %t/bit.o | FileCheck %s --check-prefix=BIT

; The bit module: .text is PROGBITS/ALLOC+EXECINSTR, one byte-aligned
; instruction stream; the string literal (13 bytes, "KeyCode=%d\r\n\0") and
; the AS4 `__code` image (6 bytes, "ABCDEF") both live in .rodata
; (PROGBITS/ALLOC, no EXECINSTR, align 1) -- the same section, concatenated
; in emission order.  The string's address trio and the AS4 image's address
; channel name `.rodata`, never `.text`; the BITADDR8 field still names the
; bit handle, not the container.
;BIT:      Name: .text
;BIT:      Type: SHT_PROGBITS
;BIT:      Flags [ (0x6)
;BIT-NEXT:   SHF_ALLOC (0x2)
;BIT-NEXT:   SHF_EXECINSTR (0x4)
;BIT:      SectionData (
;BIT-NEXT:   0000: 3000278A 0000007E 1800007A 1C00007D  |0.'....~...z...}|
;BIT-NEXT:   0010: 037C317C 207D027E 00007A31 827A2183  |.|1| }.~..z1.z!.|
;BIT-NEXT:   0020: 7A11F0A5 E89A0000 00AAAA             |z..........|
;BIT:      Name: .rodata
;BIT:      Type: SHT_PROGBITS
;BIT:      Flags [ (0x2)
;BIT-NEXT:   SHF_ALLOC (0x2)
;BIT:      ]
;BIT:      AddressAlignment: 1
;BIT:      SectionData (
;BIT-NEXT:   0000: 4B657943 6F64653D 25640D0A 00414243  |KeyCode=%d...ABC|
;BIT-NEXT:   0010: 444546                               |DEF|
;BIT:      Section ({{[0-9]+}}) .rela.text {
;BIT-NEXT:   0x1 R_MCS251_BITADDR8 _flag 0x0
;BIT-NEXT:   0x4 R_MCS251_24 .text 0x7
;BIT-NEXT:   0x9 R_MCS251_MID8 .rodata 0x0
;BIT-NEXT:   0xA R_MCS251_LO8 .rodata 0x0
;BIT-NEXT:   0xE R_MCS251_HI8 .rodata 0x0
;BIT-NEXT:   0x26 R_MCS251_24 _putstr 0x0
;BIT:      Name: _f (
;BIT-NEXT: Value: 0x0
;BIT-NEXT: Size: 43
;BIT-NEXT: Binding: Global (0x1)
;BIT-NEXT: Type: Function (0x2)
;BIT:      Name: _image (
;BIT-NEXT: Value: 0xD
;BIT-NEXT: Size: 6
;BIT-NEXT: Binding: Global (0x1)
;BIT-NEXT: Type: Object (0x1)
;BIT-NEXT: Other: 0
;BIT-NEXT: Section: .rodata (0x7)

; Without a bit object the module keeps the frozen layout: .text holds the
; function body AND the trailing string image and AS4 image (old `.text+0x23`
; addressing), and .rodata does not exist at all.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/nobit.ll -o %t/nobit.o
; RUN: llvm-readobj --sections --section-data --symbols --relocations %t/nobit.o | FileCheck %s --check-prefix=NOBIT --implicit-check-not=.rodata
;NOBIT:      Name: .text
;NOBIT:      SectionData (
;NOBIT-NEXT:   0000: 7E180000 7A1C0000 7D037C31 7C207D02  |~...z...}.|1| }.|
;NOBIT-NEXT:   0010: 7E00007A 31827A21 837A11F0 A5E89A00  |~..z1.z!.z......|
;NOBIT-NEXT:   0020: 0000AA4B 6579436F 64653D25 640D0A00  |...KeyCode=%d...|
;NOBIT-NEXT:   0030: 41424344 4546                        |ABCDEF|
;NOBIT:      Section ({{[0-9]+}}) .rela.text {
;NOBIT-NEXT:   0x2 R_MCS251_MID8 .text 0x23
;NOBIT-NEXT:   0x3 R_MCS251_LO8 .text 0x23
;NOBIT-NEXT:   0x7 R_MCS251_HI8 .text 0x23
;NOBIT-NEXT:   0x1F R_MCS251_24 _putstr 0x0
;NOBIT:      Name: _image (
;NOBIT-NEXT: Value: 0x30
;NOBIT-NEXT: Size: 6
;NOBIT-NEXT: Binding: Global (0x1)
;NOBIT-NEXT: Type: Object (0x1)
;NOBIT-NEXT: Other: 0
;NOBIT-NEXT: Section: .text (0x2)

; A v1-contract ELF object with bit objects splits identically: the split is
; keyed on the module's bit-object property and the ELF object boundary, not
; on the ABI contract generation (the bit protocol is the same in both).
; (The v1 contract uses the AS0 program address space, so this fixture drops
; the addrspace(4) markers of the v2 ones.)
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bit-v1.ll -o %t/bit-v1.o
; RUN: llvm-readobj --sections --section-data --relocations %t/bit-v1.o | FileCheck %s --check-prefix=V1
;V1:      Name: .text
;V1:      SectionData (
;V1-NEXT:   0000: 3000278A 0000007E 1800007A 1C00007D  |0.'....~...z...}|
;V1:      Name: .rodata
;V1:      SectionData (
;V1-NEXT:   0000: 4B657943 6F64653D 25640D0A 00        |KeyCode=%d...|
;V1:      Section ({{[0-9]+}}) .rela.text {
;V1-NEXT:   0x1 R_MCS251_BITADDR8 _flag 0x0
;V1-NEXT:   0x4 R_MCS251_24 .text 0x7
;V1-NEXT:   0x9 R_MCS251_MID8 .rodata 0x0
;V1-NEXT:   0xA R_MCS251_LO8 .rodata 0x0
;V1-NEXT:   0xE R_MCS251_HI8 .rodata 0x0

; An extern-only bit TU (no record of its own, uses through BITADDR8) splits
; the same way: the property is per module, not per defined record.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/ext.ll -o %t/ext.o
; RUN: llvm-readobj --sections --relocations %t/ext.o | FileCheck %s --check-prefix=EXT --implicit-check-not=.mcs251.bit
;EXT:      Name: .text
;EXT:      Name: .rodata
;EXT:      Section ({{[0-9]+}}) .rela.text {
;EXT-NEXT:   0x1 R_MCS251_BITADDR8 _ext 0x0
;EXT-NEXT:   0x4 R_MCS251_24 .text 0x7
;EXT-NEXT:   0x9 R_MCS251_MID8 .rodata 0x0

; The bit-object protocol is ELF-object-only: the REL object writer and the
; assembly text reject a bit module before any layout question, so no layout
; change can be observed there (the frozen single-CSEG shape is unreachable
; for a bit module on those paths by construction).
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj %t/bit.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=REL
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=asm %t/bit.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=REL
;REL: MCS251 bit object requires ELF object output

;--- bit.ll
target triple = "mcs251"

@flag = global i8 1 #0
@.str = private unnamed_addr constant [13 x i8] c"KeyCode=%d\0D\0A\00", align 1
@image = addrspace(4) global [6 x i8] c"ABCDEF", align 1

attributes #0 = { "mcs251-bit-object" }

declare i1 @llvm.mcs251.bit.obj.read(ptr)
declare void @putstr(ptr) addrspace(4)

define void @f() addrspace(4) {
  %v = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  br i1 %v, label %t, label %e
t:
  call addrspace(4) void @putstr(ptr @.str)
  ret void
e:
  ret void
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_f", i32 1, i32 0}
!10001 = !{!"_putstr", i32 2, i32 0}

; The exact same module minus the bit object: the reference shape for
; "condition not met => byte-identical historical layout".
;--- nobit.ll
target triple = "mcs251"

@.str = private unnamed_addr constant [13 x i8] c"KeyCode=%d\0D\0A\00", align 1
@image = addrspace(4) global [6 x i8] c"ABCDEF", align 1

declare void @putstr(ptr) addrspace(4)

define void @f() addrspace(4) {
  call addrspace(4) void @putstr(ptr @.str)
  ret void
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_f", i32 1, i32 0}
!10001 = !{!"_putstr", i32 2, i32 0}

; The v1-contract twin of bit.ll: AS0 pointers only (the v1 layout), a bit
; object and a string literal, no __code image.
;--- bit-v1.ll
target triple = "mcs251"

@flag = global i8 1 #0
@.str = private unnamed_addr constant [13 x i8] c"KeyCode=%d\0D\0A\00", align 1

attributes #0 = { "mcs251-bit-object" }

declare i1 @llvm.mcs251.bit.obj.read(ptr)
declare void @putstr(ptr)

define void @f() {
  %v = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  br i1 %v, label %t, label %e
t:
  call void @putstr(ptr @.str)
  ret void
e:
  ret void
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_f", i32 1, i32 0}
!10001 = !{!"_putstr", i32 2, i32 0}

;--- ext.ll
target triple = "mcs251"

@ext = external global i8 #0
@.str = private unnamed_addr constant [13 x i8] c"KeyCode=%d\0D\0A\00", align 1

attributes #0 = { "mcs251-bit-object" }

declare i1 @llvm.mcs251.bit.obj.read(ptr)
declare void @putstr(ptr) addrspace(4)

define void @f() addrspace(4) {
  %v = call i1 @llvm.mcs251.bit.obj.read(ptr @ext)
  br i1 %v, label %t, label %e
t:
  call addrspace(4) void @putstr(ptr @.str)
  ret void
e:
  ret void
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_f", i32 1, i32 0}
!10001 = !{!"_putstr", i32 2, i32 0}
