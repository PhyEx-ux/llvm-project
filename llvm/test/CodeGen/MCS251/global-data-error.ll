; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Mutable integer scalars/arrays/structs are supported by global-data.ll.
; Pointer initializers still require a relocation-bearing data ABI and must fail
; loudly instead of silently truncating a canonical 32-bit address into DSEG.

@g = global ptr null, align 1

define ptr @read_g() {
; CHECK: LLVM ERROR: MCS251: defined global data requires byte-aligned default-address-space
  %v = load ptr, ptr @g
  ret ptr %v
}
