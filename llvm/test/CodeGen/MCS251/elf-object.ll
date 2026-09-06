; RUN: llc -mtriple=mcs251 -filetype=obj %s -o %t.default.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %s -o %t.explicit.rel
; RUN: cmp %t.default.rel %t.explicit.rel
; RUN: FileCheck %s --check-prefix=REL < %t.default.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations --section-data %t.o | FileCheck %s --check-prefix=ELF
; RUN: llvm-readelf -h -r -n %t.o | FileCheck %s --check-prefix=READELF
; RUN: %python %S/Inputs/check-elf-rela.py %t.o %t.default.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -addrsig %s -o %t.addrsig.o
; RUN: cmp %t.o %t.addrsig.o
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
; ELF: Format: elf32-mcs251
; ELF: Arch: mcs251
; ELF: Class: 32-bit
; ELF: DataEncoding: BigEndian
; ELF: Type: Relocatable
; ELF: Machine: EM_MCS251 (0x9999)
; ELF: Flags [ (0x1)
; ELF: Name: .text
; ELF: AddressAlignment: 1
; ELF: Name: .note.mcs251.abi
; ELF: Type: SHT_NOTE
; ELF: Size: 52
; ELF: AddressAlignment: 4
; ELF: 0000: 00000007 00000020 00000001 4D435332
; ELF: 0010: 35310000 00000001 00000001 00000000
; ELF: 0020: 00000002 0000F3FF 00000007 00000000
; ELF: 0030: 00000000
; ELF: Relocations [
; ELF-DAG: R_MCS251_MID8 _gv 0x0
; ELF-DAG: R_MCS251_LO8 _gv 0x0
; ELF-DAG: R_MCS251_HI8 _gv 0x0
; ELF-DAG: R_MCS251_24 _external 0x0
; ELF-DAG: R_MCS251_24 _callee 0x0
; ELF: Symbols [
; ELF: Name: _address
; ELF: Type: Function
;
; READELF: Class: ELF32
; READELF: Data: 2's complement, big endian
; READELF: Machine: MCS251 (experimental)
; READELF: R_MCS251_MID8
; READELF: R_MCS251_LO8
; READELF: R_MCS251_HI8
; READELF: R_MCS251_24
; READELF: MCS251
