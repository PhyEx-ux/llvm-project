; RUN: not --crash llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %s -o %t 2>&1 | FileCheck %s
;
; A raw i32 symbolic prefix needs a relocation outside ABI v1. Do not emit
; R_NONE, truncate it to 24 bits, or accidentally pass through the REL writer.
@external = external global i8

define void @f() prefix i32 ptrtoint (ptr @external to i32) {
  ret void
}

; CHECK: LLVM ERROR: MCS251 ELF: unsupported relocation fixup

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
