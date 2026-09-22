; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/alias.ll -o %t/alias.o 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/ifunc.ll -o %t/ifunc.o 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/static-pointer-init.ll -o %t/init.o 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj %t/slots.ll -o %t/rel.o 2>&1 | FileCheck %s --check-prefix=REL-GATE
;
; W3b (PM ruling 2026-09-13 #2): the ELF identity is the CONTRACT GENERATION,
; so the W3 "slot trigger" is gone; what remains fail-closed under a v2
; contract is the capability set: alias/ifunc and static pointer initializer
; algebra are unregistered and keep the fatal, while an AS4 function-pointer
; call module WITHOUT any trailing pointer slot is now a regular v2 object
; (a registered v2 capability; see call-v2-addrspace.ll for the same
; conversion).  A slot module on the REL format is still rejected: REL has
; no v2 identity carrier.
;
; UNREGISTERED: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
; UNREGISTERED-SAME: static pointer initializer algebra stay unregistered
; REL-GATE: LLVM ERROR: MCS251: the v2 object identity (pointer static slots) requires ELF object output
;
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %t/no-slots-as4-call.ll -o %t/no.o
; RUN: llvm-readobj --file-headers --sections %t/no.o | FileCheck %s --check-prefix=NO-SLOTS-V2
;
; NO-SLOTS-V2: Flags [ (0x102)
; NO-SLOTS-V2: Name: .mcs251.attributes
; NO-SLOTS-V2: Size: 204
; NO-SLOTS-V2-NOT: .note.mcs251.abi

;--- slots.ll
define void @fill(i8 %v, ptr %buf) local_unnamed_addr {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_fill", i32 1, i32 0, i32 0, i32 0}
;--- alias.ll
@g = global i8 7
@a = alias i8, ptr @g
define void @fill(i8 %v, ptr %buf) local_unnamed_addr {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_fill", i32 1, i32 0, i32 0, i32 0}
;--- ifunc.ll
@f = ifunc void (i8, ptr), ptr addrspace(4) @resolver
define ptr addrspace(4) @resolver() {
  ret ptr addrspace(4) @impl
}
define void @impl(i8 %v, ptr %buf) {
  ret void
}

; W3b: an AS4 function pointer used for an indirect call is an existing
; lowering and a REGISTERED v2 capability; with the identity chosen by the
; contract (not content), this slot-less module is a regular v2 object.

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_resolver", i32 1, i32 0, i32 0}
!10001 = !{!"_impl", i32 1, i32 0, i32 0, i32 0}
;--- no-slots-as4-call.ll
define void @call_indirect(ptr addrspace(4) %fn) addrspace(4) {
  call addrspace(4) void %fn()
  ret void
}

; Static pointer initializer algebra next to legal slots: the X3 leaf rules
; stay exactly as they are in v2 (N8 keeps rejecting the addrspacecast
; initializer; CP-A is not landed).

!mcs251.signatures = !{!10000}
!10000 = !{!"_call_indirect", i32 1, i32 0, i32 0}
;--- static-pointer-init.ll
@g = addrspace(4) global [2 x i8] zeroinitializer
@p = global ptr addrspacecast (ptr addrspace(4) @g to ptr), align 1
define void @fill(i8 %v, ptr %buf) local_unnamed_addr {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_fill", i32 1, i32 0, i32 0, i32 0}
