; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=tiny %t/tiny.ll -filetype=null
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=xtiny %t/tiny.ll -filetype=null
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=small %t/small.ll -filetype=null
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=xsmall %t/small.ll -filetype=null
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=large %t/small.ll -filetype=null
; RUN: not llc -mtriple=mcs251 -mcs251-memory-model=small %t/tiny.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-model=tiny %t/small.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
;
; The unspecified command line selects the same xsmall contract as the clang
; cc1 default, so clang-produced v2-Small IR compiles with a bare llc
; invocation instead of conflicting with the legacy compatibility layout.
; RUN: llc -mtriple=mcs251 %t/small.ll -filetype=null
; RUN: not llc -mtriple=mcs251 %t/tiny.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
;
; Legacy compatibility-layout input keeps its explicit numeric contract.
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 %t/compat.ll -filetype=null
; RUN: not llc -mtriple=mcs251 %t/compat.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
;
; Named and numeric selections are mutually exclusive; unknown names and an
; explicitly empty value are malformed.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-model=tiny -mcs251-memory-contract=1,2,16,1,1 %t/tiny.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=EXCLUSIVE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-model=compact %t/tiny.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=INVALID
; RUN: not llc -mtriple=mcs251 -mcs251-memory-model= %t/tiny.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=INVALID
;
; CONFLICT: error: {{.*}}input MCS251 data layout conflicts with the selected memory contract
; EXCLUSIVE: LLVM ERROR: -mcs251-memory-model and -mcs251-memory-contract are mutually exclusive
; INVALID: LLVM ERROR: invalid -mcs251-memory-model

;--- tiny.ll
target datalayout = "E-m:s-p:16:8:8:16-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"
define i16 @pointer_size() {
  %p = getelementptr ptr, ptr null, i16 1
  %n = ptrtoint ptr %p to i16
  ret i16 %n
}

;--- small.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"
define i16 @pointer_size() {
  %p = getelementptr ptr, ptr null, i16 1
  %n = ptrtoint ptr %p to i16
  ret i16 %n
}

;--- compat.ll
target datalayout = "E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8"
target triple = "mcs251"
define i16 @pointer_size() {
  %p = getelementptr ptr, ptr null, i16 1
  %n = ptrtoint ptr %p to i16
  ret i16 %n
}
