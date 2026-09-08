; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -verify-machineinstrs -stop-after=finalize-isel -o - %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -verify-machineinstrs -o - %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %S/Inputs/call-v2-direct.ll -o %t.o
; RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=ELF
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %s -o %t.indirect.o 2>&1 | FileCheck %s --check-prefix=OBJECT-INDIRECT
;
; ProgramAS is AS4 in v2, while AS0 remains a 32-bit data pointer in this
; profile.  Direct calls must therefore preserve the function's AS4 identity
; and select ECALL + a full R_MCS251_24 relocation; no AS0 scalar coercion is
; permitted.  Function-pointer calls in AS4 select ECALLr on GPR32.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

declare void @external() addrspace(4)

define void @local() addrspace(4) {
  ret void
}

define void @call_external() addrspace(4) {
; ASM-LABEL: _call_external:
; ASM: ecall _external
; MIR-LABEL: name: call_external
; MIR: ECALL @external, csr_mcs251
  call addrspace(4) void @external()
  ret void
}

define void @call_local() addrspace(4) {
; ASM-LABEL: _call_local:
; ASM: ecall _local
; MIR-LABEL: name: call_local
; MIR: ECALL @local, csr_mcs251
  call addrspace(4) void @local()
  ret void
}

define void @call_indirect(ptr addrspace(4) %fn) addrspace(4) {
; ASM-LABEL: _call_indirect:
; ASM: ecall @dr{{[0-9]+}}
; MIR-LABEL: name: call_indirect
; MIR: ECALLr {{.*}}csr_mcs251
  call addrspace(4) void %fn()
  ret void
}

; ELF: R_MCS251_24 _external 0x0
; ELF: R_MCS251_24 _local 0x0
; OBJECT-INDIRECT: LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented
