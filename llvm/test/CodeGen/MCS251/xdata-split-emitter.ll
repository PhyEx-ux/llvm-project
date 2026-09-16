; RUN: split-file %s %t
; G13b S1: the v2 emitter marks a >65535-byte all-zero __xdata object as ONE
; logical XSEG section (SHF_MCS251_XSEG_SPLIT) and emits NO XDATA_INIT
; record for it (a single v1 record cannot describe more than 65535 bytes;
; the linker places the section and synthesizes the per-window records).
; Every Size <= 65535 object keeps the established path byte for byte.
;
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/sizes.ll -o %t/sizes.o
; RUN: llvm-readobj --sections --section-data --relocations %t/sizes.o | FileCheck %s --check-prefix=SIZES
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/constbig.ll -o %t/constbig.o
; RUN: llvm-readobj --sections %t/constbig.o | FileCheck %s --check-prefix=CONSTBIG
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/payload.ll -o %t/payload.o 2>&1 | FileCheck %s --check-prefix=PAYLOAD
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/payload.ll -o %t/payload-v1.o 2>&1 | FileCheck %s --check-prefix=PAYLOAD
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/zero65537.ll -o %t/zero65537-v1.o 2>&1 | FileCheck %s --check-prefix=V1ZERO

; 65535 (boundary, fits): unchanged path -- flags without the split bit and
; the clear-only v1 record with its HI8/MID8/LO8 destination associations.
; 65536/65537/131072 (zero): one flagged NOBITS section each, no record.
; SIZES: Name: .mcs251.XSEG._b65535
; SIZES: Type: SHT_NOBITS
; SIZES: Flags [ (0x3)
; SIZES-NEXT:       SHF_ALLOC (0x2)
; SIZES-NEXT:       SHF_WRITE (0x1)
; SIZES-NEXT:     ]
; SIZES: Size: 65535
; SIZES: Name: .mcs251.xdata_init
; SIZES: Type: SHT_PROGBITS
; SIZES-NEXT: Flags [ (0x2)
; SIZES-NEXT:       SHF_ALLOC (0x2)
; SIZES-NEXT:     ]
; SIZES-NEXT: Address: 0x0
; SIZES-NEXT: Offset:
; SIZES-NEXT: Size: 7
; SIZES-NEXT: Link: 0
; SIZES-NEXT: Info: 0
; SIZES-NEXT: AddressAlignment: 1
; SIZES-NEXT: EntrySize: 0
; SIZES-NEXT: SectionData (
; SIZES-NEXT:     0000: 000000FF FF0000                     |.......|
; SIZES-NEXT:   )
; SIZES: Name: .mcs251.XSEG._b65536
; SIZES: Type: SHT_NOBITS
; SIZES: Flags [ (0x40000003)
; SIZES-NEXT:       SHF_ALLOC (0x2)
; SIZES-NEXT:       SHF_MCS251_XSEG_SPLIT (0x40000000)
; SIZES-NEXT:       SHF_WRITE (0x1)
; SIZES-NEXT:     ]
; SIZES: Size: 65536
; SIZES-NOT: xdata_init
; SIZES: Name: .mcs251.XSEG._b65537
; SIZES: Flags [ (0x40000003)
; SIZES: Size: 65537
; SIZES-NOT: xdata_init
; SIZES: Name: .mcs251.XSEG._b131072
; SIZES: Flags [ (0x40000003)
; SIZES: Size: 131072
; The relocation-dump tail (for the _b65535 record's rela section) contains
; only `.rela.mcs251.xdata_init` -- no second record section header exists.
; SIZES-NOT: Name: .mcs251.xdata_init
; Exactly one record section exists (for _b65535 only), with exactly one
; record and its three destination-byte relocations (the .rela section dump
; comes after every section header, i.e. after the last SIZES match above).
; SIZES: R_MCS251_HI8 _b65535 0x0
; SIZES-NEXT: R_MCS251_MID8 _b65535 0x0
; SIZES-NEXT: R_MCS251_LO8 _b65535 0x0
; SIZES-NOT: R_MCS251

; `const __xdata` keeps the storage class from the address space; a big
; all-zero const object takes the same split form (a v1 record cannot
; describe it either).
; CONSTBIG: Name: .mcs251.XSEG._cbig
; CONSTBIG: Flags [ (0x40000003)
; CONSTBIG-NEXT:       SHF_ALLOC (0x2)
; CONSTBIG-NEXT:       SHF_MCS251_XSEG_SPLIT (0x40000000)
; CONSTBIG-NEXT:       SHF_WRITE (0x1)
; CONSTBIG-NEXT:     ]
; CONSTBIG: Size: 65536
; CONSTBIG-NOT: xdata_init

; A nonzero initializer of any size > 65535 stays rejected under BOTH
; identities: the v1 record payload fields are u16 and cannot describe more.
; PAYLOAD: LLVM ERROR: MCS251: __xdata global 'big': payload cannot exceed the 16-bit XDATA record limit (65535 bytes); zero-initialized objects are split by the linker into per-window records
; A v1 object keeps the frozen gate for the all-zero form too: the split
; capability is a v2 producer capability (D3, G8 mechanism).
; V1ZERO: LLVM ERROR: MCS251: __xdata global 'big': object size 65537 does not fit the 16-bit XDATA record limit (65535 bytes; XSEG objects never straddle a 64K window)

;--- sizes.ll
@b65535 = addrspace(3) global [65535 x i8] zeroinitializer, align 1
@b65536 = addrspace(3) global [65536 x i8] zeroinitializer, align 1
@b65537 = addrspace(3) global [65537 x i8] zeroinitializer, align 1
@b131072 = addrspace(3) global [131072 x i8] zeroinitializer, align 1

define void @f() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- constbig.ll
@cbig = addrspace(3) constant [65536 x i8] zeroinitializer, align 1

define void @g() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_g", i32 1, i32 0}
;--- payload.ll
@big = addrspace(3) global { i8, [65535 x i8] } { i8 1, [65535 x i8] zeroinitializer }, align 1

define void @h() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_h", i32 1, i32 0}
;--- zero65537.ll
@big = addrspace(3) global [65537 x i8] zeroinitializer, align 1

define void @i() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_i", i32 1, i32 0}
