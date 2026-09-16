; G11-B: the `.mcs251.placement` NOTE table, byte for byte (design rev 7
; §3.2/§3.3).
;
; Envelope: namesz=7 ("MCS251\0" including the NUL; the name field's storage
; is padded to 8 bytes), descsz = the record table size, type=1 (placement
; v1); every multi-byte field is big-endian like the whole ELF32-BE object.
; Record v1 layout after the u32 record_size prologue:
;   u8 schema_version(=1), u8 storage_class, u8 entity, u8 ownership,
;   u32 address, u32 size, u32 align, u32 flags, u32 layout_hash (H_source),
;   u8 stable_len, stable bytes, zero padding to a 4-byte multiple.
; H_source = SHA-256 of BE(schema,class,entity,ownership,address,align,flags)
; truncated to the low 32 bits read big-endian -- size and the stable symbol
; never participate (rev 7 B1).  The XDATA record below is exactly the
; design §3.2 fixture (schema=1, XDATA object, A=0x10000, size=4, align=1,
; flags=0): its frozen hash 457bdcf1 is asserted by the decoder.
;
; The table is one section per TU: owned records follow the module's global
; order (functions would interleave by emission order), bind records are
; appended by the end-of-file writer scan.
;
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --sections --section-data %t.o | FileCheck %s --check-prefix=ELF
; ELF: Name: .mcs251.placement
; ELF: Type: SHT_NOTE
; ELF: Flags [ (0x0)
; ELF: Size: 148
; ELF: AddressAlignment: 4
; ELF: 0000: 00000007 00000080 00000001 4D435332
; ELF: 0010: 35310000 0000001C 01000000 00000030
; ELF: 0020: 00000004 00000001 00000000 2836C169
; ELF: 0030: 02647600 0000001C 01010000 00010000
; ELF: 0040: 00000004 00000001 00000000 457BDCF1
; ELF: 0050: 02787600 0000001C 01000000 00000090
; ELF: 0060: 00000004 00000004 00000000 A0B88BFD
; ELF: 0070: 02616C00 0000001C 01000001 0000002A
; ELF: 0080: 00000001 00000001 00000000 7CBFE354
; ELF: 0090: 02626F00
;
; Independent structural decode: envelope shape, per-record field decode,
; padding zeroing and the full SHA-256 H_source recomputation.
; RUN: %python %S/Inputs/check-placement-note.py %t.o \
; RUN:   dv    0 0 0 0x30    4 1 0 \
; RUN:   xv    1 0 0 0x10000 4 1 0 \
; RUN:   al    0 0 0 0x90    4 4 0 \
; RUN:   bo    0 0 1 0x2A    1 1 0
;
; PM ruling 2026-09-16: memory-contract v1 is EOL; G11-B is v2-only.  No v1
; RUN line is carried by this test (the emitter adds no v1-specific branch
; either -- the placement path is contract-agnostic and is exercised under
; the materialized default v2 contract).

target triple = "mcs251-unknown-none"

@dv = global i32 305419896, align 1 #0
@xv = addrspace(3) global i32 5, align 1 #1
@al = global i32 7, align 4 #2
@bo = external global i8 #3

!mcs251.signatures = !{}

attributes #0 = { "mcs251-place"="0x30,data,object,owned,0" "mcs251-stable-symbol"="dv" }
attributes #1 = { "mcs251-place"="0x10000,xdata,object,owned,0" "mcs251-stable-symbol"="xv" }
attributes #2 = { "mcs251-place"="0x90,data,object,owned,0" "mcs251-stable-symbol"="al" }
attributes #3 = { "mcs251-place"="0x2A,data,object,bind,0" "mcs251-stable-symbol"="bo" }
