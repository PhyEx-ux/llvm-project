; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/difference.ll -o %t/diff.o 2>&1 | FileCheck %s --check-prefix=DIFF
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/mutable.ll -o %t/data.o 2>&1 | FileCheck %s --check-prefix=MUTABLE
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/align.ll -o %t/align.o 2>&1 | FileCheck %s --check-prefix=ALIGN
;
; A new container does not silently widen the supported data/expression ABI.
; (X3 widened the pointer INITIALIZER leaf -- &global + addend and null --
; into ELF objects; pointer expression algebra like inttoptr still fails.)
; DIFF: MCS251 ELF: symbol-difference relocations are not supported
; MUTABLE: LLVM ERROR: MCS251 contract violation: global 'g': absolute-address (integer-to-pointer cast) pointer initialization is not supported; the supported static pointer forms are null and '&symbol' with a constant offset; access fixed device addresses through a macro such as '#define PB (*(volatile uint8_t *)0xFF00)' 
; ALIGN: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only
;
;--- difference.ll
@a = external global i8
@b = external global i8
define void @difference() prefix i16 trunc (i32 sub (i32 ptrtoint (ptr @a to i32), i32 ptrtoint (ptr @b to i32)) to i16) {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_difference", i32 1, i32 0}
;--- mutable.ll
@g = global ptr inttoptr (i32 4096 to ptr), align 1
define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- align.ll
@g = constant i16 17, align 2
define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
