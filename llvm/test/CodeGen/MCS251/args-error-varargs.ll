; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/vaarg-i32.ll -o %t/a.o 2>&1 | FileCheck %s --check-prefix=E
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/vaarg-f32.ll -o %t/b.o 2>&1 | FileCheck %s --check-prefix=E
; Under the stack-based reentrant ABI, >6 variadic arguments, indirect variadic
; calls, and F=0 variadic definitions are all supported.
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -mcs251-object-format=elf -filetype=obj %t/seven.ll -o %t/c.o
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -mcs251-object-format=elf -filetype=obj %t/indirect.ll -o %t/d.o
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -mcs251-object-format=elf -filetype=obj %t/nofixed.ll -o %t/e.o
;
; Residual llvm.va_arg reaching llc remains rejected because Clang lowers va_arg
; in MCS251ABIInfo::EmitVAArg.
; E: LLVM ERROR: MCS251: llvm.va_arg is not supported; va_arg is lowered by clang CodeGen (MCS251ABIInfo::EmitVAArg)

;--- vaarg-i32.ll
define i32 @f(ptr %ap) addrspace(4) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- vaarg-f32.ll
define float @g(ptr %ap) addrspace(4) {
  %v = va_arg ptr %ap, float
  ret float %v
}
!mcs251.signatures = !{!0}
!0 = !{!"_g", i32 9, i32 0, i32 0}

;--- seven.ll
declare void @sink(i32, ...) addrspace(4)
define void @caller() addrspace(4) {
  call void (i32, ...) @sink(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8)
  ret void
}
!mcs251.signatures = !{!0, !1}
!0 = !{!"_caller", i32 1, i32 0, i32 0}
!1 = !{!"_sink", i32 10, i32 0, i32 0}

;--- indirect.ll
define void @caller(ptr addrspace(4) %fp) addrspace(4) {
  call addrspace(4) void (i32, ...) %fp(i32 1, i32 2)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_caller", i32 1, i32 0, i32 0}

;--- nofixed.ll
define void @f0(...) addrspace(4) {
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f0", i32 9, i32 0, i32 0}
