; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables < %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables -filetype=obj -mcs251-object-format=elf < %s -o %t.o
; RUN: llvm-readelf -x .text %t.o | FileCheck %s --check-prefix=BYTES
; RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=RELOC
;
; BRJT S2 (design §3.2.1/§3.2.3, H1 golden bytes): instruction-level
; three-way view of the two dispatch primitives plus the table column.
;
;   * `mov dptr,#jt` = MOVDPTRri, 3 bytes `90 hi lo` (big-endian; QEMU
;     helper.c:982-986 / decode.c:75 / disas.c:438), the 16-bit field a zero
;     placeholder with R_MCS251_J16 (7, reused frozen number).
;   * `jmp @a+dptr` = JMPIAD, single bare byte `73` (low nibble 3 < 6, no A5
;     source-mode escape; QEMU helper.c:925-929 computes the full 16-bit
;     DPTR+A and jumps within the bank).
;   * Table column = N contiguous 3-byte ljmp entries `02 hi lo`, hi byte
;     first (matches lld's Put(U>>8); Put(U)), J16 field at entry offset +1.
;
; The dense i32 0..3 switch makes every shape visible; optsize keeps the
; body compact and the offsets stable at -O0.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @isa_probe(i32 noundef %x) nounwind optsize {
; ASM-LABEL: isa_probe:
; The mnemonic view (InstPrinter spellings).
; ASM:      mov dptr, #.LJTI0_0
; ASM-NEXT: jmp @a+dptr
; The column: `.byte 2` + hi byte + lo byte per entry, 4 entries.
; ASM: .LJTI0_0:
; ASM-NEXT: .byte 2
; ASM: .byte .LBB{{[0-9_]+}}>>8
; ASM: .byte 2
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

; The encoded view.  Dispatch tail at 0x39-0x3E: `a5 e8` (mov a,r0), `90 00
; 00` (mov dptr, zero J16 field), `73` (jmp @a+dptr); the byte right after
; 0x73 is the first case body (0x7e = a mov opcode).  The column starts
; directly after the last eret (0xde = aa, 0xdf = 02) and fills 0xdf..0xe9.
; BYTES: 0x00000030 {{.*}}01a5e890 0000737e
; BYTES: 0x000000d0 {{.*}}1bfeaa02
; BYTES: 0x000000e0 {{.*}}00000200 00020000 020000

; The relocation view: base field at 0x3C (addend = column start 0xdf), then
; the four entry fields at 0xe0/0xe3/0xe6/0xe9 (3-byte pitch, +1 into each
; entry) targeting the case blocks.
; RELOC-DAG: 0x3C R_MCS251_J16 .text 0xDF
; RELOC-DAG: 0xE0 R_MCS251_J16 .text 0x3F
; RELOC-DAG: 0xE3 R_MCS251_J16 .text 0x5C
; RELOC-DAG: 0xE6 R_MCS251_J16 .text 0x7C
; RELOC-DAG: 0xE9 R_MCS251_J16 .text 0x9C

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1, !2}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_isa_probe", i32 1, i32 0}
!2 = !{!"_glob", i32 2, i32 0}
