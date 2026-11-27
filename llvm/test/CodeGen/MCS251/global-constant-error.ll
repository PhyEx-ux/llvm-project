; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 %t/zero.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 %t/record.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 %t/reloc.ll -o - 2>&1 | FileCheck %s --check-prefix=RELOC
; RUN: not llc -mtriple=mcs251 %t/aligned.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 %t/section.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 %t/undef.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %t/zero.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %t/record.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %t/reloc.ll -o - 2>&1 | FileCheck %s --check-prefix=RELOC
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %t/aligned.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %t/section.ll -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=rel %t/weak.ll -o - 2>&1 | FileCheck %s --check-prefix=WEAK
; RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel %t/undef.ll -o - 2>&1 | FileCheck %s
; X3: the constant pointer table IS supported in ELF objects (the 24-bit
; relocation channel); outside that protocol it still fails, now with the
; pointer-leaf boundary message instead of the generic RO rejection.
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/reloc.ll -o %t/reloc.o
; Weak global definitions are supported in ELF object output; legacy REL objects
; fail closed with an explicit ELF-only diagnostic.
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/weak.ll -o %t/weak.o
;
; CHECK: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only CSEG
; RELOC: LLVM ERROR: MCS251: global 'g': a pointer initializer requires ELF object output
; WEAK: LLVM ERROR: MCS251: weak/COMDAT global 'g' requires ELF object output

;--- zero.ll
@g = constant [4 x i8] zeroinitializer, align 1
!mcs251.signatures = !{}

;--- record.ll
@g = constant {i8, i16} {i8 1, i16 2}, align 1
!mcs251.signatures = !{}

;--- reloc.ll
@ref = external global i8
@g = constant ptr @ref, align 1
!mcs251.signatures = !{}

;--- aligned.ll
@g = constant i16 4951, align 2
!mcs251.signatures = !{}

;--- section.ll
@g = constant i8 1, section "XSEG", align 1
!mcs251.signatures = !{}

;--- weak.ll
@g = weak constant i8 1, align 1
!mcs251.signatures = !{}

;--- undef.ll
@g = constant [2 x i8] [i8 1, i8 undef], align 1
!mcs251.signatures = !{}

