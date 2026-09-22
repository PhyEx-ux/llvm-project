; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 %t/near.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 %t/far.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
; RUN: llvm-as %t/near.ll -o %t/near.bc
; RUN: llvm-as %t/far.ll -o %t/far.bc
; RUN: not llc -mtriple=mcs251 %t/near.bc -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 %t/far.bc -filetype=null 2>&1 | FileCheck %s --check-prefix=CONFLICT
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract= %t/no-layout.ll -filetype=null 2>&1 | FileCheck %s --check-prefix=EMPTY
;
; An input layout is an ABI contract, not a hint. llc may supply a layout only
; when the input omitted it; it must not silently replace a conflicting text-IR
; or bitcode layout. Presence of an explicitly empty numeric contract is also an
; error rather than the compatibility fallback.
;
; CONFLICT: error: {{.*}}input MCS251 data layout conflicts with the selected memory contract
; EMPTY: LLVM ERROR: invalid -mcs251-memory-contract

;--- near.ll
target datalayout = "E-m:s-p:16:8:8:16-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"
define void @near() addrspace(4) { ret void }

;--- far.ll
target datalayout = "E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8"
target triple = "mcs251"
define void @far() { ret void }

;--- no-layout.ll
target triple = "mcs251"
define void @empty() { ret void }
