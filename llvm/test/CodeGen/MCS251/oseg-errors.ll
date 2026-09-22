; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/pointer-formal.ll -o - 2>&1 | FileCheck %s --check-prefix=PTR
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/pointer-call.ll -o - 2>&1 | FileCheck %s --check-prefix=PTR
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/aggregate-formal.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/aggregate-call.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/empty.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/i64.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/f64.ll -o - 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/indirect.ll -o - 2>&1 | FileCheck %s --check-prefix=INDIRECT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/weak.ll -o - 2>&1 | FileCheck %s --check-prefix=LINKAGE
;
; Pinned to the v1 compatibility contract: this is a legacy-layout suite. The
; llc no-flag default is the xsmall/v2-Small model (clang cc1 default).
; PTR: LLVM ERROR: MCS251: static pointer parameters are not supported by the compatibility ABI
; TYPE: LLVM ERROR: MCS251: arguments must be unsplit i8/i16/i32 scalars
; INDIRECT: LLVM ERROR: MCS251 contract violation: multi-argument indirect calls are not supported
; LINKAGE: LLVM ERROR: MCS251 contract violation: weak function definitions are not supported: the current linking model implements no weak resolution and static parameter slots require a local/external owner; provide one strong definition (an unused weak declaration is accepted)

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
