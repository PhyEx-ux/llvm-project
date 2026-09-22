; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 %t/algebra.ll -o - 2>&1 | FileCheck %s --check-prefix=ALG
; RUN: not llc -mtriple=mcs251 -filetype=obj %t/algebra.ll -o - 2>&1 | FileCheck %s --check-prefix=ALG
; RUN: not llc -mtriple=mcs251 %t/symleaf.ll -o - 2>&1 | FileCheck %s --check-prefix=SYMLEAF
; RUN: not llc -mtriple=mcs251 -filetype=obj %t/symleaf.ll -o - 2>&1 | FileCheck %s --check-prefix=SYMLEAF
;
; Mutable integer scalars/arrays/structs are supported by global-data.ll.
; X3 added the pointer initializer leaf (&global + constant addend, or null)
; through the 24-bit relocation channel, ELF objects only. Pointer EXPRESSION
; algebra keeps failing loudly instead of silently truncating a canonical
; 32-bit address into DSEG, and a symbol leaf outside the ELF object protocol
; is rejected (the REL writer and assembly text carry no such record).

; ALG: LLVM ERROR: MCS251 contract violation: global 'g': absolute-address (integer-to-pointer cast) pointer initialization is not supported; the supported static pointer forms are null and '&symbol' with a constant offset; access fixed device addresses through a macro such as '#define PB (*(volatile uint8_t *)0xFF00)' 

; SYMLEAF: LLVM ERROR: MCS251: global 'g': a pointer initializer requires ELF object output

;--- algebra.ll
@g = global ptr inttoptr (i32 4096 to ptr), align 1

define ptr @read_g() {
  %v = load ptr, ptr @g
  ret ptr %v
}

;--- symleaf.ll
@base = global i8 7, align 1
@g = global ptr @base, align 1

define void @f() {
  ret void
}
