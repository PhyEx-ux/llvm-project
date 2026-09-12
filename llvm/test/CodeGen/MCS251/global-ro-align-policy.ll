; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 %t/ptrtbl.ll -o - 2>&1 | FileCheck %s --check-prefix=PTR
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/ptrtbl.ll -o - 2>&1 | FileCheck %s --check-prefix=PTR
; RUN: not --crash llc -mtriple=mcs251 %t/mutalign.ll -o - 2>&1 | FileCheck %s --check-prefix=MUT
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/mutalign.ll -o - 2>&1 | FileCheck %s --check-prefix=MUT
; RUN: not --crash llc -mtriple=mcs251 %t/sect.ll -o - 2>&1 | FileCheck %s --check-prefix=RO
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %t/sect.ll -o - 2>&1 | FileCheck %s --check-prefix=RO
;
; The ro-align relaxation covers integer arrays only.  Policy boundaries
; stay loud: mutable aligned storage (byte-alignment is still required in
; DSEG) and custom sections are rejected with their established messages.
; X3: pointer tables of any declared alignment are supported in ELF objects
; (byte-aligned image, 24-bit relocation leaves); outside that protocol the
; pointer-leaf boundary fires.

; RO: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only CSEG
; MUT: LLVM ERROR: MCS251: defined global data requires byte-aligned default-address-space
; PTR: LLVM ERROR: MCS251: global 'tbl': a pointer initializer requires ELF object output
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/ptrtbl.ll -o %t/ptrtbl.o

;--- ptrtbl.ll
@f = external global i8
@g = external global i8
@tbl = constant [2 x ptr] [ptr @f, ptr @g], align 16
;--- mutalign.ll
@m = global [2 x i16] [i16 1, i16 2], align 4
;--- sect.ll
@s = constant [2 x i16] [i16 1, i16 2], section "XSEG", align 16
