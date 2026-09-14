; RUN: llc -mtriple=mcs251 -filetype=obj %s -o %t.default.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %s -o %t.explicit.rel
; RUN: cmp %t.default.rel %t.explicit.rel
; RUN: FileCheck %s --check-prefix=REL < %t.default.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations --section-data %t.o | FileCheck %s --check-prefix=V2-ELF
; RUN: llvm-readelf -h -r -n %t.o | FileCheck %s --check-prefix=READELF
; RUN: %python %S/Inputs/check-elf-rela.py %t.o %t.default.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -addrsig %s -o %t.addrsig.o
; RUN: cmp %t.o %t.addrsig.o
;
; W3b (PM ruling 2026-09-13 #2): the no-flag default contract is the v2
; layout (1,2,32,8,1), and an ELF object under a v2 contract publishes the
; v2 identity (e_flags 0x102 + .mcs251.attributes, NO v1 note) regardless
; of module content. The v1 identity bytes are golden-pinned by the explicit
; v1-contract RUN below; the default REL object stays byte-identical to the
; explicit REL one.
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf -verify-machineinstrs %s -o %t.v1.o
; RUN: llvm-readobj --file-headers --sections --section-data %t.v1.o | FileCheck %s --check-prefix=V1-ELF
;
; Same IR, unchanged default REL, real ELF32/MSB/RELA with a versioned ABI.
; Keep a named call target defined at a nonzero offset: the ELF relocation
; field must be zero, not the old REL area offset in addition to its addend.
source_filename = "elf-object.c"

@gv = external global i8

declare void @external()

define ptr @address() {
  ret ptr @gv
}

define void @callee() {
  call void @external()
  ret void
}

define void @caller() {
  call void @callee()
  ret void
}

; REL: XH3
; REL: M elf_object
; REL: A CSEG size
; REL: F1 83
; REL: F1 03
; REL: F3 83
;
; V2-ELF: Format: elf32-mcs251
; V2-ELF: Arch: mcs251
; V2-ELF: Class: 32-bit
; V2-ELF: DataEncoding: BigEndian
; V2-ELF: Type: Relocatable
; V2-ELF: Machine: EM_MCS251 (0x9999)
; V2-ELF: Flags [ (0x102)
; V2-ELF-NOT: .note.mcs251.abi
; V2-ELF: Name: .text
; V2-ELF: AddressAlignment: 1
; The registered v2 identity carrier (the 250-byte XSmall envelope+payload;
; see elf-v2-identity.ll for the byte-pinned variant). readelf -n has no
; note to print: the v2 object carries none.
; V2-ELF: Name: .mcs251.attributes
; V2-ELF: Type: Unknown (0x70000003)
; V2-ELF: Size: 250
; V2-ELF: AddressAlignment: 1
; V2-ELF: 0000: 41000000 F94D4353 32353100 01000000
; Envelope: 0x41, VendorSize 0xF9 (249 = 16+233), "MCS251\0", scope 1,
; ScopeSize 0xEE (238 = 5+233). The P-4 Tag 28 record follows Tag 27.
; V2-ELF: Relocations [
; V2-ELF-DAG: R_MCS251_MID8 _gv 0x0
; V2-ELF-DAG: R_MCS251_LO8 _gv 0x0
; V2-ELF-DAG: R_MCS251_HI8 _gv 0x0
; V2-ELF-DAG: R_MCS251_24 _external 0x0
; V2-ELF-DAG: R_MCS251_24 _callee 0x0
; V2-ELF: Symbols [
; V2-ELF: Name: _address
; V2-ELF: Type: Function
;
; READELF: Class: ELF32
; READELF: Data: 2's complement, big endian
; READELF: Machine: MCS251 (experimental)
; READELF: Flags:                             0x102
; READELF: R_MCS251_MID8
; READELF: R_MCS251_LO8
; READELF: R_MCS251_HI8
; READELF: R_MCS251_24
;
; V1-ELF: Flags [ (0x1)
; V1-ELF: Name: .note.mcs251.abi
; V1-ELF: Type: SHT_NOTE
; V1-ELF: Size: 52
; V1-ELF: AddressAlignment: 4
; V1-ELF: 0000: 00000007 00000020 00000001 4D435332
; V1-ELF: 0010: 35310000 00000001 00000001 00000000
; V1-ELF: 0020: 00000002 0000F3FF 00000007 00000000
; V1-ELF: 0030: 00000000

!mcs251.signatures = !{!10000, !10001, !10002, !10003}
!10000 = !{!"_external", i32 2, i32 0}
!10001 = !{!"_address", i32 1, i32 0}
!10002 = !{!"_callee", i32 1, i32 0}
!10003 = !{!"_caller", i32 1, i32 0}
