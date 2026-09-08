; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as4-constant-expr.ll -o %t/as4-constant-expr.o 2>&1 | FileCheck %s --check-prefix=AS4-CE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as4-explicit.ll -o %t/as4-explicit.o 2>&1 | FileCheck %s --check-prefix=AS4-EXPLICIT
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as6-constant-expr.ll -o %t/as6-constant-expr.o 2>&1 | FileCheck %s --check-prefix=AS6-CE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as6-explicit.ll -o %t/as6-explicit.o 2>&1 | FileCheck %s --check-prefix=AS6-EXPLICIT
;
; An XSmall/v2 ELF object keeps the v1 identity only for AS4 direct calls and
; a constant AS6 byte SFR memory base. It must not serialize a CODE/SFR pointer
; capability into ordinary data. Check both the ConstantExpr bypass shape and
; its explicit instruction equivalent, with a legal SFR store in every module.
;
; AS4-CE: LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented
; AS4-EXPLICIT: LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented
; AS6-CE: LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented
; AS6-EXPLICIT: LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented

;--- as4-constant-expr.ll
target triple = "mcs251"

declare void @external() addrspace(4)

define void @as4_constant_expr(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  store volatile i32 ptrtoint (ptr addrspace(4) @external to i32), ptr %p, align 1
  ret void
}

;--- as4-explicit.ll
target triple = "mcs251"

declare void @external() addrspace(4)

define void @as4_explicit(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  %bits = ptrtoint ptr addrspace(4) @external to i32
  store volatile i32 %bits, ptr %p, align 1
  ret void
}

;--- as6-constant-expr.ll
target triple = "mcs251"

define void @as6_constant_expr(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  store volatile i16 ptrtoint (ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)) to i16), ptr %p, align 1
  ret void
}

;--- as6-explicit.ll
target triple = "mcs251"

define void @as6_explicit(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  %bits = ptrtoint ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)) to i16
  store volatile i16 %bits, ptr %p, align 1
  ret void
}
