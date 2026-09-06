; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --relocations --symbols --section-data --sections %t.o | FileCheck %s
; RUN: %python %S/Inputs/check-elf-rela.py %t.o
; RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %s -o %t.o2
; RUN: llvm-readobj --relocations --symbols --section-data --sections %t.o2 | FileCheck %s
; RUN: llvm-readobj -r %t.o2 | FileCheck %s --check-prefix=SECTION
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -reloc-section-sym=none %s -o %t.named.o
; RUN: llvm-readobj -r %t.named.o | FileCheck %s --check-prefix=NAMED
;
; Positive/negative full-address addends must not be shifted before entering
; RELA. Local section symbols carry their offset exactly once; GLOBAL symbols
; keep their own st_value. All unresolved fields in PROGBITS are zero.
@external = external global i8
@local = internal constant [3 x i8] c"abc", align 1

define ptr @positive() {
  %p = getelementptr i8, ptr @external, i32 32
  ret ptr %p
}

define ptr @negative() {
  %p = getelementptr i8, ptr @external, i32 -1
  ret ptr %p
}

define ptr @local_address() {
  %p = getelementptr [3 x i8], ptr @local, i32 0, i32 2
  ret ptr %p
}

define void @word_prefix() prefix i16 ptrtoint (ptr @external to i16) {
  ret void
}

; CHECK: Relocations [
; CHECK-DAG: R_MCS251_MID8 _external 0x20
; CHECK-DAG: R_MCS251_LO8 _external 0x20
; CHECK-DAG: R_MCS251_HI8 _external 0x20
; CHECK-DAG: R_MCS251_MID8 _external 0xFFFFFFFF
; CHECK-DAG: R_MCS251_LO8 _external 0xFFFFFFFF
; CHECK-DAG: R_MCS251_HI8 _external 0xFFFFFFFF
; CHECK-DAG: R_MCS251_MID8 {{(_local|.text)}} 0x{{[0-9A-F]+}}
; CHECK-DAG: R_MCS251_LO8 {{(_local|.text)}} 0x{{[0-9A-F]+}}
; CHECK-DAG: R_MCS251_HI8 {{(_local|.text)}} 0x{{[0-9A-F]+}}
; CHECK-DAG: R_MCS251_16 _external 0x0
;
; SECTION: 0x30 R_MCS251_MID8 .text 0x4A
; SECTION-NEXT: 0x31 R_MCS251_LO8 .text 0x4A
; SECTION-NEXT: 0x35 R_MCS251_HI8 .text 0x4A
; NAMED: R_MCS251_MID8 _local 0x2
; NAMED-NEXT: R_MCS251_LO8 _local 0x2
; NAMED-NEXT: R_MCS251_HI8 _local 0x2
