; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj < %s 2>&1 | FileCheck %s --check-prefix=REL-GATE
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %s -o %t.o
; RUN: llvm-readobj --file-headers --sections --section-data %t.o | FileCheck %s --check-prefix=V2-ELF
;
; REL-GATE: LLVM ERROR: MCS251: the v2 object identity (pointer static slots) requires ELF object output
;
; Static pointer slots are a v2 extension. Their payload width follows the full
; pointer type, not the physical region where the slot or pointee is placed.
; Since A4 a whitelisted slot module publishes the v2 ELF identity (e_flags
; 0x102 and exactly one .mcs251.attributes; the v1 note is not emitted), while
; the REL format still has no v2 identity carrier.
;
; V2-ELF: Format: elf32-mcs251
; V2-ELF: Flags [ (0x102)
; V2-ELF-NOT: .note.mcs251.abi
; V2-ELF: Name: .mcs251.attributes
; V2-ELF: Type: Unknown (0x70000003)
; V2-ELF: Size: 240
; V2-ELF: AddressAlignment: 1
; Envelope: 0x41, VendorSize 0xEF (239 = 16+223), "MCS251\0", scope 1,
; ScopeSize 0xE4 (228 = 5+223).
; V2-ELF: 0000: 41000000 EF4D4353 32353100 01000000
; V2-ELF-NEXT: 0010: E4048104 00000002 05810400 00000206
; The registered A4 value set: object protocol 2, call ABI 2/1 (minor 1 is
; the sole "static pointer slots" carrier), register parameter variant 3.
; V2-ELF-NEXT: 0020: 81040000 00010781 04000000 03088104
; Tag 13 default_placement 8 (XSmall) and the MIX profile atoms (32, 8).
; V2-ELF: 0D810400 0000080E 81040000 00020F81
; V2-ELF: 00148104 00000000 18830C01 04000000
; V2-ELF-NEXT: 0090: 20010400 00000819 81040000 00011A81

define void @default_pointer_slot(i8 %tag, ptr %p) {
; CHECK: .globl _default_pointer_slot_PARM_2
; CHECK: _default_pointer_slot_PARM_2:
; CHECK-NEXT: .ds 4
; CHECK-LABEL: _default_pointer_slot:
  ret void
}

define void @near_pointer_slot(i8 %tag, ptr addrspace(8) %p) {
; CHECK: .globl _near_pointer_slot_PARM_2
; CHECK: _near_pointer_slot_PARM_2:
; CHECK-NEXT: .ds 2
; CHECK-LABEL: _near_pointer_slot:
  ret void
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_default_pointer_slot", i32 1, i32 0, i32 0, i32 0}
!10001 = !{!"_near_pointer_slot", i32 1, i32 0, i32 0, i32 0}
