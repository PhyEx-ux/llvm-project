; RUN: split-file %s %t
; Under the stack-based reentrant ABI, pointer parameters, single-field/empty
; aggregates, and multi-argument indirect calls are all supported.
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %t/pointer-formal.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %t/pointer-call.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %t/aggregate-formal.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %t/aggregate-call.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %t/empty.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs %t/indirect.ll -o /dev/null
; Scalar widths >32 bits (i64/f64) and weak linkage on legacy REL remain rejected.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/i64.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/f64.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=rel %t/weak.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=LINKAGE
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/weak.ll -o %t/weak.o
;
; TYPE: LLVM ERROR: MCS251: arguments must be i8/i16/i32/f32/i1 scalars, ordinary pointers, or supported aggregates
; LINKAGE: LLVM ERROR: MCS251: weak/COMDAT function 'f' requires ELF object output

;--- pointer-formal.ll
 define i8 @f(i8 %a, ptr %b) { ret i8 %a }
;--- pointer-call.ll
 declare void @g(i8, ptr)
 define void @f() { call void @g(i8 1, ptr null) ret void }
;--- aggregate-formal.ll
 define void @f({i16} %a) { ret void }
;--- aggregate-call.ll
 declare void @g({i16})
 define void @f() { call void @g({i16} {i16 1}) ret void }
;--- empty.ll
 define void @f({} %a) { ret void }
;--- i64.ll
 define void @f(i64 %a) { ret void }
;--- f64.ll
 define void @f(double %a) { ret void }
;--- indirect.ll
 define void @f(ptr %p) { call void %p(i8 1, i16 2) ret void }
;--- weak.ll
 define weak i16 @f(i16 %a, i16 %b) { ret i16 %b }
