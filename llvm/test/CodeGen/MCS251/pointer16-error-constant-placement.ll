; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 %s -o - 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 %s -o - 2>&1 | FileCheck %s
;
; Ordinary C const remains AS0. Placing it in CSEG and materializing its symbol
; through a 16-bit RAM pointer truncates a high-bank ROM address. Until the v2
; RAM runtime-copy/XINIT route exists, reject rather than emit that combination.
;
; CHECK: LLVM ERROR: MCS251: ordinary AS0 constants are not supported by the 16-bit memory contract until RAM runtime-copy initialization is implemented

@table = constant [4 x i8] c"\11\22\33\44", align 1

define i8 @read_table(i16 %index) addrspace(4) {
  %p = getelementptr [4 x i8], ptr @table, i16 0, i16 %index
  %v = load i8, ptr %p, align 1
  ret i8 %v
}
