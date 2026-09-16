; G11-B: the retain section bit and the noinit record suppression (design
; rev 7 §2.2/§3.2).
;
; Retain: a `.mcu.fixed.*` section carries SHF_GNU_RETAIN if and only if the
; entity's NOTE flags.bit0 is 1 (the attribute bit is the only writer of
; the flag; the generic TLOF used/retain path is never taken).  Both
; directions are asserted here at the producer: keep (flags=1) has the bit,
; plain (flags=0) does not.  The linker-side bidirectional rejection
; (retain-drop / retain-extra, F1 rulings) is G11-C territory.
;
; noinit: an entity with flags.bit1=1 produces NO XINIT / xdata_init
; record -- its bytes are never cleared or copied at startup (the data
; sections stay NOBITS zero-fill).  The initialized neighbors keep their
; records, so the record tables shrink to exactly the non-noinit entries.
;
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; The noinit globals are asserted with GLOBAL prohibitions (review R4
; 2026-09-16: the previous interval ELF-NOTs left a blind spot -- a section
; or relocation printed outside the asserted interval escaped): no
; xdata_init carrier anywhere in the object, and no XINIT relocation whose
; destination is the noinit AS0 object.
; RUN: llvm-readobj --sections --relocations %t.o | FileCheck %s --check-prefix=ELF --implicit-check-not=xdata_init --implicit-check-not='R_MCS251_16 _nr'
;
; keep: NOBITS + ALLOC|WRITE|RETAIN for flags=1 (0x200003).
; ELF: Name: .mcu.fixed.keep
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x200003)
; ELF: SHF_GNU_RETAIN (0x200000)
; ELF: Size: 4
; keep (retain, zero init) and plain (initialized) keep their XINIT records
; (a 6-byte clear-only entry and a 10-byte payload entry = 16 bytes); the
; retain bit suppresses nothing -- only noinit does.
; ELF: Name: .mcs251.xinit
; ELF: Size: 16
; plain: NOBITS + ALLOC|WRITE for flags=0 -- no retain bit.
; ELF: Name: .mcu.fixed.plain
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 4
; nr (noinit) has NO XINIT record -- only a NOBITS section footprint.
; ELF: Name: .mcu.fixed.nr
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 4
; xnr/xk are noinit XDATA: no xdata_init section exists anywhere in the
; object (readobj prints Sections, Relocations, then Symbols).
; ELF-NOT: xdata_init
; ELF: Name: .mcu.fixed.xnr
; ELF: Type: SHT_NOBITS
; ELF: Size: 2
; ELF: Name: .mcu.fixed.xk
; ELF: Flags [ (0x200003)
; ELF: Size: 2
; The XINIT destination relocations name keep and plain -- never _nr.
; ELF: R_MCS251_16 _keep
; ELF: R_MCS251_16 _plain
; ELF-NOT: R_MCS251_16 _nr
;
; The NOTE records carry the flags byte verbatim (keep=1 retain,
; nr/xnr=2 noinit, xk=3 retain+noinit). The oracle additionally parses the
; init-record relocation tables and hard-asserts that NONE of the three
; noinit entities (_nr/_xnr/_xk) is ever an initialization destination.
; RUN: %python %S/Inputs/check-placement-note.py --no-init-for=_nr,_xnr,_xk %t.o \
; RUN:   keep  0 0 0 0x50     4 1 1 \
; RUN:   plain 0 0 0 0x58     4 1 0 \
; RUN:   nr    0 0 0 0x60     4 1 2 \
; RUN:   xnr   1 0 0 0x10020  2 1 2 \
; RUN:   xk    1 0 0 0x10028  2 1 3

target triple = "mcs251-unknown-none"

@keep = global i32 0, align 1 #0
@plain = global i32 9, align 1 #1
@nr = global i32 0, align 1 #2
@xnr = addrspace(3) global i16 0, align 1 #3
@xk = addrspace(3) global i16 0, align 1 #4

!mcs251.signatures = !{}

attributes #0 = { "mcs251-place"="0x50,data,object,owned,1" "mcs251-stable-symbol"="keep" }
attributes #1 = { "mcs251-place"="0x58,data,object,owned,0" "mcs251-stable-symbol"="plain" }
attributes #2 = { "mcs251-place"="0x60,data,object,owned,2" "mcs251-stable-symbol"="nr" }
attributes #3 = { "mcs251-place"="0x10020,xdata,object,owned,2" "mcs251-stable-symbol"="xnr" }
attributes #4 = { "mcs251-place"="0x10028,xdata,object,owned,3" "mcs251-stable-symbol"="xk" }
