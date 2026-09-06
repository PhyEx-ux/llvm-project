; RUN: llc -mtriple=mcs251 -filetype=obj %S/oseg-multi.ll -o %t.multi.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %S/oseg-multi.ll -o %t.multi.o
; RUN: %python %S/Inputs/check-elf-rela.py %t.multi.o %t.multi.rel
; RUN: llc -mtriple=mcs251 -filetype=obj %S/Inputs/oseg-caller.ll -o %t.caller.rel
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %S/Inputs/oseg-caller.ll -o %t.caller.o
; RUN: %python %S/Inputs/check-elf-rela.py %t.caller.o %t.caller.rel
; RUN: llvm-readobj -r %t.caller.o | FileCheck %s --check-prefix=CALLER
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %s -o %t.address.o
; RUN: llvm-readobj -s -r %t.address.o | FileCheck %s --check-prefix=ADDRESS --implicit-check-not=PARM
;
; Cross-module slots remain ordinary symbol references. Taking a function's
; address alone must not invent undefined parameter-slot dependencies.
; CALLER-DAG: R_MCS251_MID8 _mixed_PARM_3 0x0
; CALLER-DAG: R_MCS251_LO8 _mixed_PARM_3 0x0
; CALLER-DAG: R_MCS251_HI8 _mixed_PARM_3 0x0
; CALLER-DAG: R_MCS251_24 _mixed 0x0
; ADDRESS: R_MCS251_MID8 _remote 0x0
; ADDRESS: Name: _remote
; ADDRESS: Section: Undefined

declare i16 @remote(i16, i16)
define ptr @address_only() {
  ret ptr @remote
}
