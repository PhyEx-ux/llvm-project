; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/difference.ll -o %t/diff.o 2>&1 | FileCheck %s --check-prefix=DIFF
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/mutable.ll -o %t/data.o 2>&1 | FileCheck %s --check-prefix=MUTABLE
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/align.ll -o %t/align.o 2>&1 | FileCheck %s --check-prefix=ALIGN
;
; A new container does not silently widen the supported data/expression ABI.
; DIFF: MCS251 ELF: symbol-difference relocations are not supported
; MUTABLE: LLVM ERROR: MCS251: defined global data requires byte-aligned default-address-space
; ALIGN: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only
;
;--- difference.ll
@a = external global i8
@b = external global i8
define void @difference() prefix i16 trunc (i32 sub (i32 ptrtoint (ptr @a to i32), i32 ptrtoint (ptr @b to i32)) to i16) {
  ret void
}
;--- mutable.ll
@g = global ptr null, align 1
define void @f() {
  ret void
}
;--- align.ll
@g = constant i16 17, align 2
define void @f() {
  ret void
}
