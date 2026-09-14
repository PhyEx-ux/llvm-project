; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj %s -o %t.xsmall.o
; RUN: llvm-readobj --file-headers --sections --section-data %t.xsmall.o | FileCheck %s --check-prefix=XSMALL --check-prefix=COMMON
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,1,1 -mcs251-object-format=elf -filetype=obj %s -o %t.small.o
; RUN: llvm-readobj --file-headers --sections --section-data %t.small.o | FileCheck %s --check-prefix=SMALL --check-prefix=COMMON
; RUN: llvm-readobj --arch-specific %t.xsmall.o | FileCheck %s --check-prefix=TAGS
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,3,1 -mcs251-object-format=elf -filetype=obj %s -o %t.large.o 2>&1 | FileCheck %s --check-prefix=LARGE-GATE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -mcs251-object-format=elf -filetype=obj %s -o %t.tiny.o 2>&1 | FileCheck %s --check-prefix=TINY-GATE
;
; The A4 v2 object identity (design §3.1/§3.2, PM ruling 2026-09-13, re-ruled
; #2 by W3b): any module under a specified v2 contract emits e_flags=0x102
; and exactly one `.mcs251.attributes` whose bytes are the registered
; minimal identity set (all 21 RequiredTags, tags 21-23 omitted), and it
; carries NO v1 ABI note. XSmall pins the whole 172-byte section; Small
; differs only in the two placement fields (Tag 13 and the
; memory_model_profile placement atom).
;
; COMMON: Format: elf32-mcs251
; COMMON: Flags [ (0x102)
; COMMON-NOT: .note.mcs251.abi
; COMMON: Name: .mcs251.attributes
; COMMON: Type: Unknown (0x70000003)
; COMMON: Size: 172
; COMMON: AddressAlignment: 1
;
; Envelope: 0x41 | BE32 VendorSize=0xAB (16+155) | "MCS251\0" | scope 1 |
; BE32 ScopeSize=0xA0 (5+155).
; COMMON: 0000: 41000000 AB4D4353 32353100 01000000
; COMMON-NEXT: 0010: A0048104 00000002 05810400 00000206
; Tags 4..20, strictly increasing, every record Critical (0x81) with ULEB
; length 4. Registered values: object protocol 2, call ABI 2/1 (the minor
; value 1 is the sole "static pointer slots" carrier), register parameter
; variant 3, register set 0x0000f3ff, int/long 32, AS0 32, AS layout 2,
; placement 8 (XSmall) or 1 (Small), the four subprotocols 2, capabilities
; 0/0, abi_options 0.
; COMMON-NEXT: 0020: 81040000 00010781 04000000 03088104
; COMMON-NEXT: 0030: 0000F3FF 09810400 0000200A 81040000
; COMMON-NEXT: 0040: 00200B81 04000000 200C8104 00000002
; XSMALL: 0D810400 0000080E 81040000 00020F81
; SMALL: 0D810400 0000010E 81040000 00020F81
; COMMON-NEXT: 0060: 04000000 02108104 00000002 11810400
; COMMON-NEXT: 0070: 00000212 81040000 00001381 04000000
; COMMON-NEXT: 0080: 00148104 00000000 18830C01 04000000
; Tag 24 memory_model_profile MIX: two U32 atoms (32, 8) or (32, 1).
; XSMALL-NEXT: 0090: 20010400 00000819 81040000 00011A81
; SMALL-NEXT: 0090: 20010400 00000119 81040000 00011A81
; COMMON-NEXT: 00A0: 04000000 201B8104 00000000
;
; Per-tag readobj display: every required tag named, Critical, typed and
; valued with the registered set.
; TAGS: MCS251Attributes {
; TAGS: FormatVersion: 0x41
; TAGS: VendorSize: 0xAB
; TAGS: ScopeSize: 0xA0
; TAGS: Name: object_protocol_version
; TAGS-NEXT: Critical: Yes
; TAGS-NEXT: ValueType: U32 (1)
; TAGS-NEXT: Length: 4
; TAGS-NEXT: Value: 0x2
; TAGS: Name: call_abi_major
; TAGS: Value: 0x2
; TAGS: Name: call_abi_minor
; TAGS: Value: 0x1
; TAGS: Name: register_parameter_variant
; TAGS: Value: 0x3
; TAGS: Name: general_register_set
; TAGS: Value: 0xF3FF
; TAGS: Name: as_layout_version
; TAGS: Value: 0x2
; TAGS: Name: default_placement
; TAGS: Value: 0x8
; TAGS: Name: init_protocol_version
; TAGS: Value: 0x2
; TAGS: Name: placement_protocol_version
; TAGS: Value: 0x2
; TAGS: Name: stack_contract_version
; TAGS: Value: 0x2
; TAGS: Name: function_contract_version
; TAGS: Value: 0x2
; TAGS: Name: required_capabilities_lo
; TAGS: Value: 0x0
; TAGS: Name: required_capabilities_hi
; TAGS: Value: 0x0
; TAGS: Name: abi_options
; TAGS: Value: 0x0
; TAGS: Name: memory_model_profile
; TAGS: ValueType: MIX (3)
; TAGS: Atoms [
; TAGS: Value: 0x20
; TAGS: Value: 0x8
; TAGS: Name: code_model_profile
; TAGS: Value: 0x1
; TAGS: Name: code_pointer_bits
; TAGS: Value: 0x20
; TAGS: Name: object_protocol_minor
; TAGS: Value: 0x0
;
; Out-of-range models keep the fail-closed gate: Large (placement 3) is not
; a registered emission profile, and 16-bit contracts never reach objects.
; W3b: Large fails the registered-capability walk of the v2 identity.
; LARGE-GATE: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
; TINY-GATE: MCS251 16-bit pointer ABI cannot emit relocatable objects

define void @fill(i8 %tag, ptr %buf) local_unnamed_addr {
  ret void
}
