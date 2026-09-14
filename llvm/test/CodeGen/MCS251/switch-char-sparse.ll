; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; BRJT S1 (design §3.1.3): the sparse char-case shape (switch4-char, the
; original P1 crash corpus) must lower to the eq comparison chain under the
; closed jump-table gate: one eq compare per case value with the character's
; numeric constant, holes falling through to the final miss -> default.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

; 'A'=0x41, 'Q'=0x51, 'z'=0x7a, 0=0x30 -- sparse, unordered, with default.
define void @sparsechar(i8 noundef zeroext %c) {
; CHECK-LABEL: sparsechar:
; O0:      cmp r{{[0-9]+}}, #0x30
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      cmp r{{[0-9]+}}, #0x41
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      cmp r{{[0-9]+}}, #0x51
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      cmp r{{[0-9]+}}, #0x7a
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2-DAG: cmp r{{[0-9]+}}, #0x30
; O2-DAG: cmp r{{[0-9]+}}, #0x41
; O2-DAG: cmp r{{[0-9]+}}, #0x51
; O2-DAG: cmp r{{[0-9]+}}, #0x7a
entry:
  switch i8 %c, label %default [
    i8 65, label %cA
    i8 81, label %cQ
    i8 122, label %cz
    i8 48, label %c0
  ]
cA:
  store volatile i32 65, ptr @glob, align 1
  ret void
cQ:
  store volatile i32 81, ptr @glob, align 1
  ret void
cz:
  store volatile i32 122, ptr @glob, align 1
  ret void
c0:
  store volatile i32 48, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

; Hole in the middle of an otherwise ascending range: the hole simply falls
; through the chain to the default (no table, no hole fill entry).
define void @holed(i32 noundef %x) {
; CHECK-LABEL: holed:
; O0:      mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0005
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 2, label %c2
    i32 5, label %c5
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}
