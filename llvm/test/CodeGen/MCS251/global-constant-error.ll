; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 %t/zero.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 %t/record.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 %t/reloc.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 %t/aligned.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 %t/section.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 %t/weak.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 %t/undef.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/zero.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/record.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/reloc.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/aligned.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/section.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/weak.ll -o - 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/undef.ll -o - 2>&1 | FileCheck %s
;
; CHECK: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only CSEG

;--- zero.ll
@g = constant [4 x i8] zeroinitializer, align 1
;--- record.ll
@g = constant {i8, i16} {i8 1, i16 2}, align 1
;--- reloc.ll
@ref = external global i8
@g = constant ptr @ref, align 1
;--- aligned.ll
@g = constant i16 4951, align 2
;--- section.ll
@g = constant i8 1, section "XSEG", align 1
;--- weak.ll
@g = weak constant i8 1, align 1
;--- undef.ll
@g = constant [2 x i8] [i8 1, i8 undef], align 1
