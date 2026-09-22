; G11-N4 (design rev 8 §8.3): the `.mcs251.placement.names` association NOTE
; carrier, byte for byte, plus the association contract it must satisfy.
;
; The v1 `.mcs251.placement` table is FROZEN: no ELF name, no symbol index and
; no symbol-reference relocation enter it (asm-labels and target mangling break
; any "external stable == declaration name" assumption), so the
; stable_symbol -> actual ELF symbol mapping travels in a SEPARATE dedicated
; NOTE.  Its schema (all multi-byte fields big-endian):
;   namesz=7 ("MCS251\0" incl. the NUL), the name field padded to 8 bytes;
;   type=2 (placement-name association -- NOT "schema version 2");
;   desc = u32 association_version (=1), u32 entry_count, then entry_count
;   entries of { u32 placement_record_index, u32 elf_name_len,
;                u8 elf_name[elf_name_len], zero pad to a 4-byte multiple }.
;
; This test freezes BOTH sections' bytes for a nine-record fixture whose ELF
; names are deliberately NOT derivable from the stable symbols:
;   record_index 0  function owned      stable "fn.stable"     ELF "_ownedfn"
;   record_index 1  AS0-DATA owned      stable "ob.stable"     ELF "_ob"
;   record_index 2  XDATA owned         stable "ox.stable"     ELF "_ox"
;   record_index 3  AS0-DATA owned      stable "dotted.stable" ELF "_asm.dotted"
;   record_index 4  AS0-DATA owned      stable "q.stable"      ELF "_q"
;   record_index 5  AS0-DATA owned      stable "abc.stable"    ELF "_abc"
;   record_index 6  AS0-DATA owned      stable "abcd.stable"   ELF "_abcd"
;   record_index 7  AS0-DATA bind       stable "bb.stable"     ELF "_bb"
;   record_index 8  function bind       stable "bf.stable"     ELF "_bf"
; Record order is the emitter's physical order: owned functions (emitted per
; machine function, before module finalization), then owned globals in module
; order, then the bind records appended by the end-of-file scan.  The entry
; name lengths 8/3/3/11/2/4/5/3/3 exercise the WHOLE 4-byte pad matrix
; (pad 0/1/1/1/2/0/3/1/1), including entries with zero padding and names of
; exactly one 4-byte unit.  NOTE the `_` prefix is the target's ELF symbol
; prefix: the association name is the final MC symbol name, never the stable
; symbol (e.g. "dotted.stable" -> "_asm.dotted" only via the MC symbol).
;
; The names section must NOT enter the output image: SHT_NOTE with sh_flags 0
; (no SHF_ALLOC) and sh_addralign 4.  The oracle asserts the same and is
; negatively controlled on this very object (--names-self-test tampers
; placement_record_index / ELF name / NOTE type / association_version and
; requires rejection).
;
; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/main.ll -o %t.o
; RUN: llvm-readobj --sections --section-data --symbols %t.o | FileCheck %s --check-prefix=ELF
;
; The v1 table is untouched: same frozen layout and the same physical record
; order the association indices refer to (9 records, 384 bytes).
; ELF: Name: .mcs251.placement ({{[0-9]+}})
; ELF: Type: SHT_NOTE
; ELF: Flags [ (0x0)
; ELF: Size: 384
; ELF: AddressAlignment: 4
; ELF: SectionData (
; ELF-NEXT: 0000: 00000007 0000016C 00000001 4D435332
; ELF-NEXT: 0010: 35310000 00000024 01020100 00FC6000
; ELF-NEXT: 0020: 00000001 00000001 00000000 896C76DB
; ELF-NEXT: 0030: 09666E2E 73746162 6C650000 00000024
; ELF-NEXT: 0040: 01000000 00000030 00000004 00000001
; ELF-NEXT: 0050: 00000000 2836C169 096F622E 73746162
; ELF-NEXT: 0060: 6C650000 00000024 01010000 00010000
; ELF-NEXT: 0070: 00000004 00000001 00000000 457BDCF1
; ELF-NEXT: 0080: 096F782E 73746162 6C650000 00000028
; ELF-NEXT: 0090: 01000000 00000040 00000001 00000001
; ELF-NEXT: 00A0: 00000000 4ADAFB61 0D646F74 7465642E
; ELF-NEXT: 00B0: 73746162 6C650000 00000024 01000000
; ELF-NEXT: 00C0: 00000050 00000001 00000001 00000000
; ELF-NEXT: 00D0: 4CE68EB5 08712E73 7461626C 65000000
; ELF-NEXT: 00E0: 00000024 01000000 00000060 00000001
; ELF-NEXT: 00F0: 00000001 00000000 60720886 0A616263
; ELF-NEXT: 0100: 2E737461 626C6500 00000024 01000000
; ELF-NEXT: 0110: 00000070 00000001 00000001 00000000
; ELF-NEXT: 0120: 0E89DAC8 0B616263 642E7374 61626C65
; ELF-NEXT: 0130: 00000024 01000001 0000002A 00000001
; ELF-NEXT: 0140: 00000001 00000000 7CBFE354 0962622E
; ELF-NEXT: 0150: 73746162 6C650000 00000024 01020101
; ELF-NEXT: 0160: 00FC4000 00000000 00000001 00000000
; ELF-NEXT: 0170: C2A66CC5 0962662E 73746162 6C650000
;
; The association NOTE itself: namesz=7, descsz=0x84, type=2, name "MCS251"
; with the trailing NUL plus one pad byte, association_version=1, entry_count=9,
; then the nine entries in index order.
; ELF: Name: .mcs251.placement.names
; ELF: Type: SHT_NOTE
; ELF: Flags [ (0x0)
; ELF: Size: 152
; ELF: AddressAlignment: 4
; ELF: SectionData (
; ELF-NEXT: 0000: 00000007 00000084 00000002 4D435332
; ELF-NEXT: 0010: 35310000 00000001 00000009 00000000
; ELF-NEXT: 0020: 00000008 5F6F776E 6564666E 00000001
; ELF-NEXT: 0030: 00000003 5F6F6200 00000002 00000003
; ELF-NEXT: 0040: 5F6F7800 00000003 0000000B 5F61736D
; ELF-NEXT: 0050: 2E646F74 74656400 00000004 00000002
; ELF-NEXT: 0060: 5F710000 00000005 00000004 5F616263
; ELF-NEXT: 0070: 00000006 00000005 5F616263 64000000
; ELF-NEXT: 0080: 00000007 00000003 5F626200 00000008
; ELF-NEXT: 0090: 00000003 5F626600
;
; Owned associations are the fixed section's unique principal symbol; bind
; associations exist as undefined STB_GLOBAL symbols even though no code
; references them, and they own no section and no storage.
; ELF: Name: _ob
; ELF: Size: 4
; ELF: Binding: Global
; ELF: Type: Object
; ELF: Section: .mcu.fixed.ob.stable
; ELF: Name: _bb
; ELF: Size: 0
; ELF: Binding: Global
; ELF: Type: None
; ELF: Section: Undefined
; ELF: Name: _bf
; ELF: Size: 0
; ELF: Binding: Global
; ELF: Type: None
; ELF: Section: Undefined
;
; Independent structural decode, association closure against .symtab (owned =
; unique principal symbol of `.mcu.fixed.<stable>`, bind = GLOBAL UNDEFINED),
; and the four negative injections on the real bytes.
; RUN: %python %S/Inputs/check-placement-note.py --names-self-test %t.o \
; RUN:   fn.stable     2 1 0 0xFC6000 1 1 0 \
; RUN:   ob.stable     0 0 0 0x30     4 1 0 \
; RUN:   ox.stable     1 0 0 0x10000  4 1 0 \
; RUN:   dotted.stable 0 0 0 0x40     1 1 0 \
; RUN:   q.stable      0 0 0 0x50     1 1 0 \
; RUN:   abc.stable    0 0 0 0x60     1 1 0 \
; RUN:   abcd.stable   0 0 0 0x70     1 1 0 \
; RUN:   bb.stable     0 0 1 0x2A     1 1 0 \
; RUN:   bf.stable     2 1 1 0xFC4000 0 1 0
;
; A placement-free module emits NEITHER the v1 table NOR the association
; carrier (the end-of-file writer returns before either section is created):
; the whole object is byte-for-byte the pre-N4 emission.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/free.ll -o %t.free.o
; RUN: llvm-readobj --sections --section-data %t.free.o | FileCheck %s --check-prefix=FREE --implicit-check-not=.mcs251.placement
; FREE: Name: .text
; FREE: Name: .mcs251.dseg
; FREE: Size: 4
;
; The writer is fail-closed on a bind carrier that is not an external
; declaration: a bind record must associate an UNDEFINED EXTERNAL symbol, so a
; definition carrying the bind spec cannot be expressed (rather than silently
; pairing it with whatever symbol happens to be there).
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/binddef.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BINDDEF
; BINDDEF: LLVM ERROR: MCS251: bind placement carrier 'bb' must be an external declaration

;--- main.ll
target triple = "mcs251-unknown-none"

@ob = global i32 305419896, align 1 #1
@ox = addrspace(3) global i32 7, align 1 #2
@"asm.dotted" = global i8 1, align 1 #3
@q = global i8 5, align 1 #4
@abc = global i8 6, align 1 #5
@abcd = global i8 7, align 1 #6
@bb = external global i8 #7

declare void @bf() addrspace(4) #8

define void @ownedfn() addrspace(4) #0 {
entry:
  ret void
}

!mcs251.signatures = !{!100, !101}
!100 = !{!"_ownedfn", i32 1, i32 0, i32 0}
!101 = !{!"_bf", i32 2, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0xFC6000,code,function,owned,0" "mcs251-stable-symbol"="fn.stable" }
attributes #1 = { "mcs251-place"="0x30,data,object,owned,0" "mcs251-stable-symbol"="ob.stable" }
attributes #2 = { "mcs251-place"="0x10000,xdata,object,owned,0" "mcs251-stable-symbol"="ox.stable" }
attributes #3 = { "mcs251-place"="0x40,data,object,owned,0" "mcs251-stable-symbol"="dotted.stable" }
attributes #4 = { "mcs251-place"="0x50,data,object,owned,0" "mcs251-stable-symbol"="q.stable" }
attributes #5 = { "mcs251-place"="0x60,data,object,owned,0" "mcs251-stable-symbol"="abc.stable" }
attributes #6 = { "mcs251-place"="0x70,data,object,owned,0" "mcs251-stable-symbol"="abcd.stable" }
attributes #7 = { "mcs251-place"="0x2A,data,object,bind,0" "mcs251-stable-symbol"="bb.stable" }
attributes #8 = { "mcs251-place"="0xFC4000,code,function,bind,0" "mcs251-stable-symbol"="bf.stable" }

;--- free.ll
target triple = "mcs251-unknown-none"

@fr = global i32 1, align 1
!mcs251.signatures = !{}

;--- binddef.ll
target triple = "mcs251-unknown-none"

@bb = global i8 0, align 1 #0
!mcs251.signatures = !{}

attributes #0 = { "mcs251-place"="0x2A,data,object,bind,0" "mcs251-stable-symbol"="bb" }

; The association contract is REACHABLE, not only an internal invariant: a
; target-prefixed owned definition @x and an exact-name (asm-label style)
; bind @"\01_x" both land on the MC symbol "_x", so the bind record's
; association would have to name the same symbol as an owned definition --
; the carrier must reject that instead of emitting a contradictory pair.
; (G11-B2 review S2: retracts the earlier "unreachable for legal IR" claim.)
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/collide.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=COLLIDE
; COLLIDE: LLVM ERROR: MCS251: bind placement record 1 (stable 'two') associates ELF symbol '_x' which is not an undefined external symbol of this input

;--- collide.ll
target triple = "mcs251-unknown-none"
!mcs251.signatures = !{}
@x = global i8 1, align 1 #0
@"\01_x" = external global i8 #1
attributes #0 = { "mcs251-place"="0x30,data,object,owned,0" "mcs251-stable-symbol"="one" }
attributes #1 = { "mcs251-place"="0x40,data,object,bind,0" "mcs251-stable-symbol"="two" }
