; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables < %s -o - | FileCheck %s --check-prefixes=CHECK,JT
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables -verify-machineinstrs < %s -o - 2>&1 | FileCheck %s --check-prefix=JT
; RUN: llc -mtriple=mcs251 -O2 -mcs251-jump-tables < %s -o - | FileCheck %s --check-prefixes=CHECK,JT
; RUN: llc -mtriple=mcs251 -O0 < %s -o - | FileCheck %s --check-prefixes=CHECK,NOJT
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables -filetype=obj -mcs251-object-format=elf < %s -o %t.o && llvm-readobj --relocations --symbols %t.o | FileCheck %s --check-prefix=OBJ
;
; BRJT S3 (design §3.2.1/§3.2.3, H1): the opt-in CODE-space ljmp jump table.
; Dispatch = the fixed six-slot sequence ending in `mov dptr,#jt; jmp
; @a+dptr`; the table column is N contiguous 3-byte ljmp entries
; `02 hi lo` emitted right after the function body in the same CSEG
; section; every 16-bit field is a J16 relocation (mov-dptr base + one per
; entry).  With the flag off (D4) the same IR lowers through the comparison
; chain and neither the dispatch nor the table column exists (H3 zero
; perturbation).
;
; The cluster is dense 0..3 over i32: N=4 >= min entries, Range=4 <= 86,
; density 100%.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @dense4_jt(i32 noundef %x) {
; CHECK-LABEL: dense4_jt:
; The JT header (upstream): idx = cond - Low, then the unsigned bound check
; against High-Low with the default as the out-of-range target.
; JT:      mov dr4, #0x0003
; JT-NEXT: cmp dr0, dr4
; JT-NEXT: jle [[SKIP:\.LBB[0-9_]+]]
; JT-NEXT: ejmp [[DEF:\.LBB[0-9_]+]]
; The dispatch tail (design §3.2.1 slots 4-6).
; JT:      mov a, r{{[0-9]+}}
; JT-NEXT: mov dptr, #[[JT:.LJTI[0-9_]+]]
; JT-NEXT: jmp @a+dptr
; The table column: 3-byte ljmp entries, one per case block in MBB order,
; target field = hi byte first (entry offset +1 ..+2).
; JT: [[JT]]:
; JT-NEXT: .byte 2
; JT-NEXT: .byte [[C0:\.LBB[0-9_]+]]>>8
; JT-NEXT: .byte [[C0]]
; JT-NEXT: .byte 2
; JT-NEXT: .byte [[C1:\.LBB[0-9_]+]]>>8
; JT-NEXT: .byte [[C1]]
; JT-NEXT: .byte 2
; JT-NEXT: .byte [[C2:\.LBB[0-9_]+]]>>8
; JT-NEXT: .byte [[C2]]
; JT-NEXT: .byte 2
; JT-NEXT: .byte [[C3:\.LBB[0-9_]+]]>>8
; JT-NEXT: .byte [[C3]]
; Flag off: no dispatch, no table -- the comparison chain only (H3).
; NOJT-NOT: {{\.LJTI}}
; NOJT-NOT: jmp @a+dptr
; NOJT: cmp dr0, dr4
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

; ELF object layer (H1): R_MCS251_J16 = 7 (reused frozen number) for the
; mov-dptr base field and one per table entry.  The five fields show the
; geometry: the base J16 at 0x3C (the `mov dptr` immediate), then the four
; entry fields at 0xE0/0xE3/0xE6/0xE9 -- 3-byte entry pitch, each +1 past its
; entry's `02` opcode byte, targeting the case-block labels.  (Offsets are
; -O0 deterministic.)
; OBJ-DAG: 0x3C R_MCS251_J16 .text
; OBJ-DAG: 0xE0 R_MCS251_J16 .text
; OBJ-DAG: 0xE3 R_MCS251_J16 .text
; OBJ-DAG: 0xE6 R_MCS251_J16 .text
; OBJ-DAG: 0xE9 R_MCS251_J16 .text
; The table column itself is the RELA addend of the mov-dptr base: the base
; field resolves to .text+0xDF, the first byte of the column.

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1, !2}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_dense4_jt", i32 1, i32 0}
!2 = !{!"_glob", i32 2, i32 0}
