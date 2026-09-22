; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/defs.ll -o %t/defs.o
; RUN: llvm-readobj --sections --section-data --relocations --symbols %t/defs.o | FileCheck %s --check-prefix=DEFS
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/extern.ll -o %t/extern.o
; RUN: llvm-readobj --sections %t/extern.o | FileCheck %s --check-prefix=EXTERN
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/ptrleaf.ll -o %t/ptrleaf.o
; RUN: llvm-readobj --sections --section-data --relocations %t/ptrleaf.o | FileCheck %s --check-prefix=PTRL
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/addend.ll -o %t/addend.o
; RUN: llvm-readobj --sections --section-data --relocations %t/addend.o | FileCheck %s --check-prefix=ADDEND
; G13b: under the default (v2) contract a >65535 all-zero object is placed
; (SHF_MCS251_XSEG_SPLIT, no record); a v1 object keeps the frozen gate.
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/huge.ll -o %t/huge.o
; RUN: llvm-readobj --sections %t/huge.o | FileCheck %s --check-prefix=SPLIT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/huge.ll -o %t/huge-v1.o 2>&1 | FileCheck %s --check-prefix=HUGE
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/align.ll -o %t/align.o 2>&1 | FileCheck %s --check-prefix=ALIGN
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/gepnull.ll -o %t/gepnull.o 2>&1 | FileCheck %s --check-prefix=ALG
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/inttoptr.ll -o %t/inttoptr.o 2>&1 | FileCheck %s --check-prefix=INTPTR
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/ascast.ll -o %t/ascast.o 2>&1 | FileCheck %s --check-prefix=ALG
; RUN: not llc -mtriple=mcs251 %t/defs.ll -o - 2>&1 | FileCheck %s --check-prefix=TEXT
;
; X3 placement: an AS3 (__xdata) definition becomes ONE per-object NOBITS
; section `.mcs251.XSEG.<sym>` plus one `.mcs251.xdata_init` record
;   u8 bank(HI8), u16 window(MID8|LO8, big-endian), u16 object_size,
;   u16 payload_size (0 = clear only), payload
; through the byte-of-24 relocation channel. `const __xdata` stays in XSEG
; (const is a write discipline, the storage class is the address space).
; Extern-only TUs emit no placement at all. A single unmarked object is
; capped at 65535 bytes (XSEG objects never straddle a 64K window); G13b:
; a v2 object may declare ONE larger all-zero object via
; SHF_MCS251_XSEG_SPLIT with no record (the linker synthesizes records).
;
; X3-R2: a stored pointer leaf is a BIG-ENDIAN 4-byte container whose low
; 24 bits are the canonical address: the most significant byte (always
; zero, bits [31:24] are not part of the effective address) sits at the
; leaf offset and the R_MCS251_24 field occupies offsets +1..+3 (bank at
; +1, window big-endian at +2..+3). After linking, a symbol at 0x011234
; serializes as 00 01 12 34.
;
; X3-R4: the legal "&global + constant" form is a GEP with a GlobalVariable
; base and constant indices; the addend is folded per the DataLayout and
; travels in the R_MCS251_24 addend. GEP null, inttoptr, ptrtoint, casts
; and all other expression algebra stay rejected.

; DEFS: Name: .mcs251.XSEG._zero
; DEFS: Type: SHT_NOBITS
; DEFS: Size: 16
; The single `.mcs251.xdata_init` section follows the first record's
; section switch; every later record appends to it (module order).
; DEFS: Name: .mcs251.xdata_init
; DEFS: Type: SHT_PROGBITS
; DEFS: EntrySize: 0
; DEFS-NEXT: SectionData (
; DEFS-NEXT:     0000: 00000000 10000000 00000004 00044142  |..............AB|
; DEFS-NEXT:     0010: 43440000 00000300 0378797A           |CD.......xyz|
; DEFS-NEXT:   )
; DEFS: Name: .mcs251.XSEG._filled
; DEFS: Type: SHT_NOBITS
; DEFS: Size: 4
; DEFS: Name: .mcs251.XSEG._cx
; DEFS: Type: SHT_NOBITS
; DEFS: Size: 3
; The three destination bytes are the HI8/MID8/LO8 associations of the
; object symbols (global symbols keep their names).
; DEFS: R_MCS251_HI8 _zero 0x0
; DEFS-NEXT: R_MCS251_MID8 _zero 0x0
; DEFS-NEXT: R_MCS251_LO8 _zero 0x0
; DEFS: R_MCS251_HI8 _filled 0x0
; DEFS-NEXT: R_MCS251_MID8 _filled 0x0
; DEFS-NEXT: R_MCS251_LO8 _filled 0x0
; DEFS: R_MCS251_HI8 _cx 0x0
; DEFS-NEXT: R_MCS251_MID8 _cx 0x0
; DEFS-NEXT: R_MCS251_LO8 _cx 0x0
; STT_OBJECT with the alloc size, one per placed object.
; DEFS: Name: _zero
; DEFS-NEXT: Value: 0x0
; DEFS-NEXT: Size: 16
; DEFS-NEXT: Binding: Global (0x1)
; DEFS-NEXT: Type: Object (0x1)

; EXTERN-NOT: XSEG
; EXTERN-NOT: xdata_init

; A pointer leaf inside the payload: a big-endian 4-byte container (zero
; most-significant byte at the leaf offset, then the three-byte
; R_MCS251_24 field at +1..+3). The _p record's payload starts at record
; offset 14, so the container is 14..17 and the 24-bit field is at 0xF.
; PTRL: Name: .mcs251.xdata_init
; PTRL: EntrySize: 0
; PTRL-NEXT: SectionData (
; PTRL-NEXT:     0000: 00000000 02000000 00000004 00040000  |................|
; PTRL-NEXT:     0010: 0000                                    |..|
; PTRL-NEXT:   )
; PTRL: R_MCS251_HI8 _q 0x0
; PTRL-NEXT: R_MCS251_MID8 _q 0x0
; PTRL-NEXT: R_MCS251_LO8 _q 0x0
; PTRL: R_MCS251_HI8 _p 0x0
; PTRL-NEXT: R_MCS251_MID8 _p 0x0
; PTRL-NEXT: R_MCS251_LO8 _p 0x0
; PTRL: 0xF R_MCS251_24 _q 0x0

; X3-R4 positive: the GEP form of "&global + constant" is accepted; the
; folded addend (4 bytes of i8) travels in the R_MCS251_24 addend, and the
; XINIT record for the DSEG slot @q carries it in its payload. The payload
; starts at record offset 6, so the container is 6..9 and the field is at
; 0x7.
; ADDEND: Name: .mcs251.xinit
; ADDEND: SectionData (
; ADDEND-NEXT:     0000: 00000004 00040000 0000               |..........|
; ADDEND-NEXT:   )
; ADDEND: 0x0 R_MCS251_16 _q 0x0
; ADDEND: 0x7 R_MCS251_24 _g 0x4

; HUGE: LLVM ERROR: MCS251: __xdata global 'big': object size 65536 does not fit the 16-bit XDATA record limit (65535 bytes; XSEG objects never straddle a 64K window)
; G13b (check prefix SPLIT): the v2 object marks the 65536-byte all-zero
; object as one logical section (SHF_MCS251_XSEG_SPLIT, D3 v2 producer
; capability) and emits NO XDATA_INIT record -- the linker places the
; section across windows and synthesizes the per-window clear-only v1
; records.
; SPLIT: Name: .mcs251.XSEG._big
; SPLIT: Type: SHT_NOBITS
; SPLIT: Flags [ (0x40000003)
; SPLIT-NEXT:       SHF_ALLOC (0x2)
; SPLIT-NEXT:       SHF_MCS251_XSEG_SPLIT (0x40000000)
; SPLIT-NEXT:       SHF_WRITE (0x1)
; SPLIT-NEXT:     ]
; SPLIT: Size: 65536
; SPLIT-NOT: xdata_init
; ALIGN: LLVM ERROR: MCS251: __xdata global 'a': storage must be byte-aligned
; W3b: the default contract is v2, so initializer algebra (GEP over null,
; inttoptr, addrspacecast) is rejected by the registered-capability gate of
; the v2 identity (still fail-closed for the same modules as before).
; WP4 A2: an inttoptr initializer is now rejected by the structural contract
; check with its actionable message before the v2 identity gate (gepnull and
; addrspacecast still reach the gate).
; INTPTR: LLVM ERROR: MCS251 contract violation: global 'q': absolute-address (integer-to-pointer cast) pointer initialization is not supported; the supported static pointer forms are null and '&symbol' with a constant offset; access fixed device addresses through a macro such as '#define PB (*(volatile uint8_t *)0xFF00)' 
; ALG: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
; TEXT: LLVM ERROR: MCS251: __xdata global 'zero': storage requires ELF object output

;--- defs.ll
@zero = addrspace(3) global [16 x i8] zeroinitializer, align 1
@filled = addrspace(3) global [4 x i8] c"ABCD", align 1
@cx = addrspace(3) constant [3 x i8] c"xyz", align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- extern.ll
@ext = external addrspace(3) global [16 x i8]
@extc = external addrspace(4) global i8

define void @g() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_g", i32 1, i32 0}
;--- ptrleaf.ll
@q = addrspace(3) global [2 x i8] zeroinitializer, align 1
@p = addrspace(3) global ptr addrspace(3) @q, align 1

define void @h() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_h", i32 1, i32 0}
;--- addend.ll
; The legal GEP form: base is a GlobalVariable, indices fold to constants.
@g = addrspace(3) global [8 x i8] zeroinitializer, align 1
@q = global ptr addrspace(3) getelementptr (i8, ptr addrspace(3) @g, i32 4), align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- huge.ll
@big = addrspace(3) global [65536 x i8] zeroinitializer, align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- align.ll
@a = addrspace(3) global [4 x i8] zeroinitializer, align 2

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- gepnull.ll
; GEP null: the base is not an object identity, the leaf stays rejected.
@q = global ptr addrspace(3) getelementptr (i8, ptr addrspace(3) null, i32 4), align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- inttoptr.ll
; An absolute integer laundered into a pointer: never a placement leaf.
@q = global ptr addrspace(3) inttoptr (i32 16 to ptr addrspace(3)), align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- ascast.ll
; A GEP over an addrspacecast must not launder the AS4 base into an AS3
; pointer leaf (the base must be the GlobalVariable itself, no casts).
@cg = addrspace(4) global [4 x i8] zeroinitializer, align 1
@q = global ptr addrspace(3) getelementptr (i8, ptr addrspace(3) addrspacecast (ptr addrspace(4) @cg to ptr addrspace(3)), i32 4), align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
