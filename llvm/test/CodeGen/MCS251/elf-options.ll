; RUN: not llc -mtriple=mcs251 -filetype=asm -mcs251-object-format=elf %s -o %t 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -filetype=null -mcs251-object-format=elf %s -o %t 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=invalid %s -o %t 2>&1 | FileCheck %s --check-prefix=FORMAT
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -crel %s -o %t 2>&1 | FileCheck %s --check-prefix=CREL
; RUN: llc -mtriple=mcs251 %s -o %t.default.asm
; RUN: llc -mtriple=mcs251 -mcs251-object-format=rel %s -o %t.rel.asm
; RUN: cmp %t.default.asm %t.rel.asm
;
; TYPE: MCS251 ELF output requires -filetype=obj
; FORMAT: Cannot find option named 'invalid'
; CREL: MCS251 ELF ABI v1 requires RELA; CREL is not supported

define void @f() {
  ret void
}
