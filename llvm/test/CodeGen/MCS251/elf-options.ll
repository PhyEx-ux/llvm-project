; RUN: not llc -mtriple=mcs251 -filetype=asm -mcs251-object-format=elf %s -o %t 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -filetype=null -mcs251-object-format=elf %s -o %t 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=invalid %s -o %t 2>&1 | FileCheck %s --check-prefix=FORMAT
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -crel %s -o %t 2>&1 | FileCheck %s --check-prefix=CREL
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -mcs251-memory-contract=1,2,16,1,1 %s -o %t 2>&1 | FileCheck %s --check-prefix=V2
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %s -o %t.v1.o
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -mcs251-memory-contract=1,2,32,8,1 %s -o %t.v2-compatible.o
; RUN: cmp %t.v1.o %t.v2-compatible.o
; RUN: llc -mtriple=mcs251 %s -o %t.default.asm
; RUN: llc -mtriple=mcs251 -mcs251-object-format=rel %s -o %t.rel.asm
; RUN: cmp %t.default.asm %t.rel.asm
;
; TYPE: MCS251 ELF output requires -filetype=obj
; FORMAT: Cannot find option named 'invalid'
; CREL: MCS251 ELF ABI v1 requires RELA; CREL is not supported
; V2: MCS251 16-bit pointer ABI cannot emit relocatable objects until the v2 ABI attributes and linker compatibility gate are implemented

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
