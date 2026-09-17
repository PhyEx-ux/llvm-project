; G11-N4 zero-perturbation pin for the v1 carriers (design rev 8 §8.3:
; "保留 placement NOTE v1 字节布局与 layout_hash 算法不变"; the association NOTE
; is an ADDITION, never a mutation of v1).
;
; The v1 `.mcs251.placement` table (148 bytes) and the `.mcs251.xinit` startup
; table (20 bytes) of the canonical four-record fixture
; `placement-note-bytes.ll` are stored here as raw Inputs blobs captured from
; the FROZEN pre-N4 emission.  The N4 writer must reproduce both byte-for-byte:
; the association carrier is a second NOTE section, not a change to the v1
; record layout, the record order or the initialization records.  A cmp against
; an external blob also protects the in-file hexdump oracle of
; placement-note-bytes.ll from being relaxed silently.
;
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %S/placement-note-bytes.ll -o %t.o
; RUN: llvm-objcopy --dump-section=.mcs251.placement=%t.v1 %t.o
; RUN: cmp %t.v1 %S/Inputs/placement-v1-note-frozen.note
; RUN: llvm-objcopy --dump-section=.mcs251.xinit=%t.xinit %t.o
; RUN: cmp %t.xinit %S/Inputs/placement-xinit-frozen.bin
;
; Section geometry: the only difference from the pre-N4 object is the added
; association NOTE.  The v1 table keeps Size 148 / align 4 / flags 0, the
; xinit table keeps its 20 bytes, and the association carrier is SHT_NOTE,
; non-ALLOC (flags 0), align 4, 76 bytes for four 3-byte ELF names.
; RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=SEC
; SEC: Name: .mcs251.xinit
; SEC: Type: SHT_PROGBITS
; SEC: Size: 20
; SEC: Name: .mcs251.placement
; SEC: Type: SHT_NOTE
; SEC: Flags [ (0x0)
; SEC: Size: 148
; SEC: AddressAlignment: 4
; SEC: Name: .mcs251.placement.names
; SEC: Type: SHT_NOTE
; SEC: Flags [ (0x0)
; SEC: Size: 76
; SEC: AddressAlignment: 4
;
; The association carrier carries no runtime allocation property: SHT_NOTE
; without SHF_ALLOC and without a symbol-reference relocation.  This asserts
; the PRODUCER's shape only -- an ET_REL object has no program headers, so it
; does NOT by itself prove how the linker will place the section; the design's
; "关联节不进入输出镜像" is closed on the link side by the C consumer slice.
; RUN: llvm-readobj --program-headers %t.o | FileCheck %s --check-prefix=NOALLOC --implicit-check-not=LOAD
; NOALLOC: ProgramHeaders [
;
; The independent reader closes the association against the very same v1
; records: record_index order is the physical record order and the ELF names
; are the fixed sections' principal symbols (dv/xv/al) plus the undefined
; external (bo).
; RUN: %python %S/Inputs/check-placement-note.py %t.o \
; RUN:   dv 0 0 0 0x30    4 1 0 \
; RUN:   xv 1 0 0 0x10000 4 1 0 \
; RUN:   al 0 0 0 0x90    4 4 0 \
; RUN:   bo 0 0 1 0x2A    1 1 0