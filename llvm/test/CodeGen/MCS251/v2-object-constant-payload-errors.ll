; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as4-constant-expr.ll -o %t/as4-constant-expr.o
; RUN: llvm-readobj --file-headers --sections %t/as4-constant-expr.o | FileCheck %s --check-prefix=AS4-CE
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as4-explicit.ll -o %t/as4-explicit.o
; RUN: llvm-readobj --file-headers --sections %t/as4-explicit.o | FileCheck %s --check-prefix=AS4-EXPLICIT
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as6-constant-expr.ll -o %t/as6-constant-expr.o 2>&1 | FileCheck %s --check-prefix=AS6-CE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/as6-explicit.ll -o %t/as6-explicit.o 2>&1 | FileCheck %s --check-prefix=AS6-EXPLICIT
;
; W3b (PM ruling 2026-09-13 #2): the identity is the contract generation, so
; an XSmall/v2 ELF object is v2 whatever its content. A CODE (AS4) pointer
; serialized into ordinary data is a REGISTERED v2 capability (D.5 set in
; bodies and constants): both the ConstantExpr and the explicit-instruction
; shapes now link as regular v2 objects. The SFR (AS6) space stays outside
; the D.5 set: serializing an AS6 pointer into data remains fail-closed.
; Every module keeps a legal direct-SFR store.
;
; AS4-CE: Flags [ (0x102)
; AS4-CE: Name: .mcs251.attributes
; AS4-CE-NOT: .note.mcs251.abi
; AS4-EXPLICIT: Flags [ (0x102)
; AS4-EXPLICIT: Name: .mcs251.attributes
; AS4-EXPLICIT-NOT: .note.mcs251.abi
; AS6-CE: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
; AS6-EXPLICIT: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity

;--- as4-constant-expr.ll
target triple = "mcs251"

declare void @external() addrspace(4)

define void @as4_constant_expr(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  store volatile i32 ptrtoint (ptr addrspace(4) @external to i32), ptr %p, align 1
  ret void
}


!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_external", i32 2, i32 0}
!10001 = !{!"_as4_constant_expr", i32 1, i32 0, i32 0}
;--- as4-explicit.ll
target triple = "mcs251"

declare void @external() addrspace(4)

define void @as4_explicit(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  %bits = ptrtoint ptr addrspace(4) @external to i32
  store volatile i32 %bits, ptr %p, align 1
  ret void
}


!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_external", i32 2, i32 0}
!10001 = !{!"_as4_explicit", i32 1, i32 0, i32 0}
;--- as6-constant-expr.ll
target triple = "mcs251"

define void @as6_constant_expr(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  store volatile i16 ptrtoint (ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)) to i16), ptr %p, align 1
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_as6_constant_expr", i32 1, i32 0, i32 0}
;--- as6-explicit.ll
target triple = "mcs251"

define void @as6_explicit(ptr %p) {
  store volatile i8 65, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  %bits = ptrtoint ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)) to i16
  store volatile i16 %bits, ptr %p, align 1
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_as6_explicit", i32 1, i32 0, i32 0}
