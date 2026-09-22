; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/f1.ll -o %t/f1.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/f1.o | FileCheck %s --check-prefix=F1
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/f2.ll -o %t/f2.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/f2.o | FileCheck %s --check-prefix=F2
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/f3.ll -o %t/f3.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/f3.o | FileCheck %s --check-prefix=F3
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/f4.ll -o %t/f4.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/f4.o | FileCheck %s --check-prefix=F4
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/f5.ll -o %t/f5.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/f5.o | FileCheck %s --check-prefix=F5
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/zok.ll -o %t/zok.o
; RUN: llvm-readobj --sections --section-data --symbols %t/zok.o | FileCheck %s --check-prefix=ZOK
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/nempty.ll -o %t/nempty.o 2>&1 | FileCheck %s --check-prefix=NEMPTY
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/nnested.ll -o %t/nnested.o 2>&1 | FileCheck %s --check-prefix=NNESTED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/nnestednz.ll -o %t/nnestednz.o 2>&1 | FileCheck %s --check-prefix=NNESTEDNZ
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/nundef.ll -o %t/nundef.o 2>&1 | FileCheck %s --check-prefix=NUNDEF
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/nas0.ll -o %t/nas0.o 2>&1 | FileCheck %s --check-prefix=NAS0
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/nalign.ll -o %t/nalign.o 2>&1 | FileCheck %s --check-prefix=NALIGN
;
; AS4-AGGREGATE (design AS4-AGGREGATE-INIT-DESIGN 6A/7-S2): read-only __code
; initializers accept nonempty non-opaque structs of i8/i16/i32, nested arrays
; and structs at any depth (packed or not), &global pointer leaves and the ROM
; zero image -- the isSupportedMutableInitializer/emitMutableInitializer shape
; mirrored onto the RO gate and emitter, AS0 stays array/scalar only.
; The gate order is the spec: recursive type qualification first, then the
; value dispatch, so a zero image never bypasses the type check.
;
; Positive scaled forms (goldens via --section-data, one byte derived from the
; 6A.3 algorithm: scalars big-endian, arrays element-wise with stride padding,
; structs member-wise over the getStructLayout offsets, zero images expanded
; to storeSize zero bytes, pointer leaves as the 4-byte container):
;   F1 [N x { u8[12] }]          -- the demo asc2_1206 array-of-struct shape
;   F2 gui.ll heterogeneous packed form (whole-member zero image and a
;      non-packed single-member struct wrapping a packed struct with a
;      non-zero tail array)
;   F3 { u8[32], i8[2] } with a gb18030 string leaf and a whole-item zero
;      image (the p8 mixed form)
;   F4 the tfont32 split-tail zero graph: a non-zero packed subtree (non-zero
;      leading scalars, non-zero sibling member) whose tail array member is a
;      genuine zeroinitializer expanded in place
;   F5 struct-internal pointer leaves: the 4-byte container plus the
;      R_MCS251_24 field at container offset 1, and a null leaf without one
;   ZOK zero-image guard: qualified-type whole-item and member zero images
;      keep emitting (the type qualification must not over-reject)
; Relocations are pinned explicitly against injection: readobj prints the
; Relocations block between SectionData and Symbols, where a trailing
; check-not after the symbol lines never looks. F1-F4 match the empty
; "Relocations [ ]" block (an injected entry inserts lines between the
; brackets and breaks CHECK-NEXT); F5 matches the full one-entry block, so
; any extra R_MCS251_24 fails too -- a null leaf has no relocation.
; The 5 real font.h tables (5772 bytes) are accepted by the same walk and are
; byte-compared against the mutable-channel reference objects in the design
; 8.2/8.3 probe flow; this file keeps the scaled single-form regressions.
;
; Negative boundary (still fail-closed):
;   NEMPTY/NNESTED/NNESTEDNZ  empty and nested-empty structs, with and without
;      a zero image root, rejected with the R1 text. These run under the v1
;      memory contract (1,1,32,8,1): the v2 identity capability scan rejects
;      them earlier with its own text, which would not pin the RO gate.
;      (Opaque structs cannot reach the backend at all: LLParser rejects a
;      zero-initialized opaque global and the verifier rejects sized
;      initializers containing one.)
;   NUNDEF  an undef member falls through the value dispatch to R1.
;   NAS0    an AS0 constant struct keeps the frozen rejection text (R4).
;   NALIGN  a struct with declared alignment 2 gets the R2 non-array text.

; F1: Name: .text
; F1: SectionData (
; F1-NEXT:     0000: AA010203 04050607 08090A0B 0CAAAAAA  |................|
; F1-NEXT:     0010: AAAAAAAA AAAAAAAA AA000000 00000000  |................|
; F1-NEXT:     0020: 00000000 00                          |.....|
; F1-NEXT:   )
; F1: Relocations [
; F1-NEXT: ]
; F1: Name: _f1
; F1-NEXT: Value: 0x1
; F1-NEXT: Size: 36

; F2: Name: .text
; F2: SectionData (
; F2-NEXT:     0000: AA111213 14151617 18191A1B 1C000000  |................|
; F2-NEXT:     0010: 00000000 00000000 00556677 88212223  |.........Ufw.!"#|
; F2-NEXT:     0020: 24252627 28999897 96959493 9291908F  |$%&'(...........|
; F2-NEXT:     0030: 8E                                   |.|
; F2-NEXT:   )
; F2: Relocations [
; F2-NEXT: ]
; F2: Name: _f2
; F2-NEXT: Value: 0x1
; F2-NEXT: Size: 48

; F3: Name: .text
; F3: SectionData (
; F3-NEXT:     0000: AA000000 00000000 00000000 00000000  |................|
; F3-NEXT:     0010: 00000000 00000000 00000000 00000000  |................|
; F3-NEXT:     0020: 00000001 02030405 06070809 0A0B0C0D  |................|
; F3-NEXT:     0030: 0E0F1011 12131415 16171819 1A1B1C1D  |................|
; F3-NEXT:     0040: 1E1F2061 62000000 00000000 00000000  |.. ab...........|
; F3-NEXT:     0050: 00000000 00000000 00000000 00000000  |................|
; F3-NEXT:     0060: 00000000 00C9EE                      |.......|
; F3-NEXT:   )
; F3: Relocations [
; F3-NEXT: ]
; F3: Name: _f3
; F3-NEXT: Value: 0x1
; F3-NEXT: Size: 102

; F4: Name: .text
; F4: SectionData (
; F4-NEXT:     0000: AA333435 00000000 0063               |.345.....c|
; F4-NEXT:   )
; F4: Relocations [
; F4-NEXT: ]
; F4: Name: _f4
; F4-NEXT: Value: 0x1
; F4-NEXT: Size: 9

; F5: Name: .text
; F5: SectionData (
; F5-NEXT:     0000: AA686900 00000000 07000000 00        |.hi..........|
; F5-NEXT:   )
; F5: Relocations [
; F5-NEXT:   Section ({{[0-9]+}}) .rela.text {
; F5-NEXT:     0x5 R_MCS251_24 .text 0x1
; F5-NEXT:   }
; F5-NEXT: ]
; F5: Name: _f5
; F5-NEXT: Value: 0x4
; F5-NEXT: Size: 9

; ZOK: Name: .text
; ZOK: SectionData (
; ZOK-NEXT:     0000: AA000000 00000000 00000000 0102      |..............|
; ZOK-NEXT:   )
; ZOK: Name: _p1
; ZOK-NEXT: Value: 0x1
; ZOK-NEXT: Size: 3
; ZOK: Name: _p2
; ZOK-NEXT: Value: 0x4
; ZOK-NEXT: Size: 6
; ZOK: Name: _p3
; ZOK-NEXT: Value: 0xA
; ZOK-NEXT: Size: 4
; ZOK-NOT: R_MCS251_24

; NEMPTY: LLVM ERROR: MCS251: __code global 'nempty': unsupported initializer (i8/i16/i32 scalars, nonempty arrays of integers, nonempty non-opaque structs of those at any nesting, &global pointer leaves, or the ROM zero image)
; NNESTED: LLVM ERROR: MCS251: __code global 'nnested': unsupported initializer (i8/i16/i32 scalars, nonempty arrays of integers, nonempty non-opaque structs of those at any nesting, &global pointer leaves, or the ROM zero image)
; NNESTEDNZ: LLVM ERROR: MCS251: __code global 'nnestednz': unsupported initializer (i8/i16/i32 scalars, nonempty arrays of integers, nonempty non-opaque structs of those at any nesting, &global pointer leaves, or the ROM zero image)
; NUNDEF: LLVM ERROR: MCS251: __code global 'nundef': unsupported initializer (i8/i16/i32 scalars, nonempty arrays of integers, nonempty non-opaque structs of those at any nesting, &global pointer leaves, or the ROM zero image)
; NAS0: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only CSEG i8/i16/i32 scalar or nonempty initialized integer array of any alignment (emitted byte-aligned); mutable data, zeroinitializers, custom sections, TLS, weak/COMDAT, aggregates and initializer relocations are not supported
; NALIGN: LLVM ERROR: MCS251: __code global 'a': non-array storage must be byte-aligned (arrays of any declared alignment are emitted byte-aligned)

;--- f1.ll
%F1S = type { [12 x i8] }
@f1 = addrspace(4) global [3 x %F1S] [
  %F1S { [12 x i8] c"\01\02\03\04\05\06\07\08\09\0A\0B\0C" },
  %F1S { [12 x i8] c"\AA\AA\AA\AA\AA\AA\AA\AA\AA\AA\AA\AA" },
  %F1S zeroinitializer ]

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- f2.ll
%F1206 = type { [12 x i8] }
%PK = type <{ i8, i8, i8, i8, [8 x i8] }>
@f2 = addrspace(4) global <
  { %F1206, %F1206, { %PK }, %F1206 } > <{
    %F1206 { [12 x i8] c"\11\12\13\14\15\16\17\18\19\1A\1B\1C" },
    %F1206 zeroinitializer,
    { %PK } { %PK <{ i8 85, i8 102, i8 119, i8 136, [8 x i8] c"\21\22\23\24\25\26\27\28" }> },
    %F1206 { [12 x i8] c"\99\98\97\96\95\94\93\92\91\90\8F\8E" } }>

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- f3.ll
%GB = type { [32 x i8], [2 x i8] }
@f3 = addrspace(4) global [3 x %GB] [
  %GB zeroinitializer,
  %GB { [32 x i8] c"\01\02\03\04\05\06\07\08\09\0A\0B\0C\0D\0E\0F\10\11\12\13\14\15\16\17\18\19\1A\1B\1C\1D\1E\1F\20", [2 x i8] c"ab" },
  %GB { [32 x i8] zeroinitializer, [2 x i8] c"\C9\EE" } ]

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- f4.ll
%PKZ = type <{ i8, i8, i8, [5 x i8] }>
@f4 = addrspace(4) global { { %PKZ }, i8 } {
  { %PKZ } { %PKZ <{ i8 51, i8 52, i8 53, [5 x i8] zeroinitializer }> },
  i8 99 }

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- f5.ll
@f5msg = private unnamed_addr addrspace(4) constant [3 x i8] c"hi\00", align 1
@f5 = addrspace(4) global { ptr addrspace(4), i8, ptr addrspace(4) } { ptr addrspace(4) @f5msg, i8 7, ptr addrspace(4) null }

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- zok.ll
@p1 = addrspace(4) global { i8, [2 x i8] } zeroinitializer
@p2 = addrspace(4) global [2 x [3 x i8]] zeroinitializer
@p3 = addrspace(4) global [2 x { i8, i8 }] [ { i8, i8 } zeroinitializer, { i8, i8 } { i8 1, i8 2 } ]

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- nempty.ll
@nempty = addrspace(4) global {} zeroinitializer
;--- nnested.ll
@nnested = addrspace(4) global { {}, i8 } zeroinitializer
;--- nnestednz.ll
%E = type {}
@nnestednz = addrspace(4) global { %E, i8 } { %E zeroinitializer, i8 5 }
;--- nundef.ll
@nundef = addrspace(4) global { i8, i8 } { i8 1, i8 undef }

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- nas0.ll
@s0 = constant { i8, i16 } { i8 1, i16 2 }

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- nalign.ll
%PS = type { i8, i16 }
@a = addrspace(4) global %PS { i8 1, i16 2 }, align 2

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
