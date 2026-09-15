; G8 S1/S2: the EDATA-movable capability mark on the producer side.
;
; Design §3 (option (i)): under a specified layout-v2 contract an ELF object
; marks, per input section, the AS0 writable data slices the linker may
; migrate into [0x100, --edata-end] when the low 128-byte DSEG window cannot
; hold them:
;   * `.mcs251.dseg`            (ordinary AS0 mutable globals)      -- S1
;   * `.mcs251.DSEG.<n>`        (non-leaf parameter slot areas)     -- S2
; The mark is a capability bit, never a placement decision: the linker still
; tries the low window first and a program that fits keeps its pre-G8 layout
; byte for byte.  The following must NOT carry the mark:
;   * v1 (layout-compatibility) objects and the REL stream -- their layout is
;     a frozen historical asset;
;   * leaf `.mcs251.OSEG.<n>` slot areas -- they overlay and are placed by the
;     OSEG rule, never migrated;
;   * `.mcs251.REG_BANK_0`, `.mcs251.xinit`, `.mcs251.attributes` and every
;     non-DATA class.
;
; The ELF object also stays AS0 with 32-bit pointers: the mark widens the
; admissible window of a section, it never changes an address space, a
; relocation width, or a signature.
;
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 \
; RUN:   -mcs251-object-format=elf -filetype=obj %s -o %t.v2.o
; RUN: llvm-readobj --sections %t.v2.o | FileCheck %s --check-prefix=V2
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 \
; RUN:   -mcs251-object-format=elf -filetype=obj %s -o %t.v1.o
; RUN: llvm-readobj --sections %t.v1.o | FileCheck %s --check-prefix=V1
; RUN: llvm-readobj --file-headers %t.v2.o | FileCheck %s --check-prefix=IDENT
;
; The v2 identity stays the registered one: the mark does not add a Tag or a
; capability word.
; IDENT: Flags [ (0x102)

; V2: Name: .mcs251.DSEG.
; V2: SHT_NOBITS
; V2: Flags [ (0x20000003)
; V2: SHF_MCS251_EDATA_MOVABLE

; The leaf slot area is an overlay and is never a migration candidate, under
; either contract.
; V2: Name: .mcs251.OSEG.
; V2: Flags [ (0x10000003)
; V2-NOT: SHF_MCS251_EDATA_MOVABLE

; V2: Name: .mcs251.dseg
; V2: SHT_NOBITS
; V2: Flags [ (0x20000003)
; V2: SHF_MCS251_EDATA_MOVABLE

; V1: Name: .mcs251.DSEG.
; V1: SHT_NOBITS
; V1: Flags [ (0x3)
; V1-NOT: SHF_MCS251_EDATA_MOVABLE
; V1: Name: .mcs251.OSEG.
; V1: Flags [ (0x10000003)
; V1: Name: .mcs251.dseg
; V1: SHT_NOBITS
; V1: Flags [ (0x3)
; V1-NOT: SHF_MCS251_EDATA_MOVABLE

@g = global i16 4951, align 1
@big = global [200 x i8] zeroinitializer, align 1

declare void @sink()

; A non-leaf function: its slot area must survive nested calls, so it is a
; `.mcs251.DSEG.<n>` slice (the S2 mark).
define i16 @nonleaf(i16 %a, i16 %b) noinline {
  call void @sink()
  %v = load volatile i16, ptr @g, align 1
  %r = add i16 %v, %b
  ret i16 %r
}

; A leaf function: its slot area overlays and stays UNMARKED in both
; contracts.
define i16 @leaf(i16 %a, i16 %b) noinline {
  %r = xor i16 %a, %b
  ret i16 %r
}

!mcs251.signatures = !{!0, !1, !2}
!0 = !{!"_sink", i32 2, i32 0}
!1 = !{!"_nonleaf", i32 1, i32 0, i32 0, i32 0}
!2 = !{!"_leaf", i32 1, i32 0, i32 0, i32 0}
