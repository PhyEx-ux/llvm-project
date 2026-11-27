; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %s -o %t.o
; RUN: llvm-readobj --file-headers --sections --section-data %t.o | FileCheck %s --check-prefix=V2-ELF
;
; Pointer continuation parameters are passed on the stack under the stack-based
; reentrant ABI (no static `_PARM_2` slots). The v2 ELF identity (`e_flags`
; 0x102 and `.mcs251.attributes`, no v1 note) is unchanged.
;
; V2-ELF: Format: elf32-mcs251
; V2-ELF: Flags [ (0x102)
; V2-ELF-NOT: .note.mcs251.abi
; V2-ELF: Name: .mcs251.attributes
; V2-ELF: Type: SHT_MCS251_ATTRIBUTES (0x70000003)
; V2-ELF: Size: 240
; V2-ELF: AddressAlignment: 1
; V2-ELF: 0000: 41000000 EF4D4353 32353100 01000000
; V2-ELF-NEXT: 0010: E4048104 00000002 05810400 00000206
; V2-ELF-NEXT: 0020: 81040000 00010781 04000000 03088104
; V2-ELF: 0D810400 0000080E 81040000 00020F81
; V2-ELF: 00148104 00000000 18830C01 04000000
; V2-ELF-NEXT: 0090: 20010400 00000819 81040000 00011A81

define void @default_pointer_slot(i8 %tag, ptr %p) {
; CHECK-NOT: _default_pointer_slot_PARM_2
; CHECK-LABEL: _default_pointer_slot:
; CHECK: eret
  ret void
}

define void @near_pointer_slot(i8 %tag, ptr addrspace(8) %p) {
; CHECK-NOT: _near_pointer_slot_PARM_2
; CHECK-LABEL: _near_pointer_slot:
; CHECK: eret
  ret void
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_default_pointer_slot", i32 1, i32 0, i32 0, i32 0}
!10001 = !{!"_near_pointer_slot", i32 1, i32 0, i32 0, i32 0}
