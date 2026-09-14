; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; BRJT S1 matrix (design S3.1.3 / S4 G2+G3): every over-threshold dense shape
; (4/8/16 cases x i8/i16/i32 condition) that crashed with "Cannot select:
; br_jt" before S1 now compiles and lowers through the comparison chain.
;
; Pinned shapes:
;  -O0: linear ascending eq chain, exactly one compare per case value
;       (measured: cmp count == case count for all nine shapes below);
;  -O2: Kannan-Proebsting pivot tree. Every path from dispatch to a case body
;       is bounded by 2*ceil(log2(n)) compares (measured max path depth: 4 for
;       n=4/8, 5 for n=16, inside the G3 bound). We pin the root pivot and the
;       presence of every case value; per-path depth is a gate-record
;       measurement, not a lit pin. The i8 pivots are 16-bit signed compares
;       of the flip-offset form (BRCC8S, see the BRCC8S comment in
;       MCS251InstrInfo.td), so they pin as wr-vs-wr compares.
;
; All bodies carry a volatile store so no cluster can be value-folded away.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @m4_i8(i8 noundef %x) {
; CHECK-LABEL: m4_i8:
; O0: cmp r{{[0-9]+}}, #0x00
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x01
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x02
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x03
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; O2-NEXT: jle {{\.LBB[0-9_]+}}
; O2-DAG: cmp r{{[0-9]+}}, #0x00
; O2-DAG: cmp r{{[0-9]+}}, #0x01
; O2-DAG: cmp r{{[0-9]+}}, #0x02
; O2-DAG: cmp r{{[0-9]+}}, #0x03
entry:
  switch i8 %x, label %default [
    i8 0, label %c0
    i8 1, label %c1
    i8 2, label %c2
    i8 3, label %c3
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

define void @m4_i16(i16 noundef %x) {
; CHECK-LABEL: m4_i16:
; O0: cmp wr{{[0-9]+}}, #0x0000
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0001
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0002
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0003
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: cmp wr{{[0-9]+}}, #0x0001
; O2-NEXT: jsle {{\.LBB[0-9_]+}}
; O2-DAG: cmp wr{{[0-9]+}}, #0x0000
; O2-DAG: cmp wr{{[0-9]+}}, #0x0001
; O2-DAG: cmp wr{{[0-9]+}}, #0x0002
; O2-DAG: cmp wr{{[0-9]+}}, #0x0003
entry:
  switch i16 %x, label %default [
    i16 0, label %c0
    i16 1, label %c1
    i16 2, label %c2
    i16 3, label %c3
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

define void @m4_i32(i32 noundef %x) {
; CHECK-LABEL: m4_i32:
; O0: mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0003
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: mov dr4, #0x0001
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jsle {{\.LBB[0-9_]+}}
; O2-DAG: mov dr4, #0x0000
; O2-DAG: mov dr4, #0x0001
; O2-DAG: mov dr4, #0x0002
; O2-DAG: mov dr4, #0x0003
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

define void @m8_i8(i8 noundef %x) {
; CHECK-LABEL: m8_i8:
; O0: cmp r{{[0-9]+}}, #0x00
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x01
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x02
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x03
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x04
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x05
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x06
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x07
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; O2-NEXT: jle {{\.LBB[0-9_]+}}
; O2-DAG: cmp r{{[0-9]+}}, #0x00
; O2-DAG: cmp r{{[0-9]+}}, #0x01
; O2-DAG: cmp r{{[0-9]+}}, #0x02
; O2-DAG: cmp r{{[0-9]+}}, #0x03
; O2-DAG: cmp r{{[0-9]+}}, #0x04
; O2-DAG: cmp r{{[0-9]+}}, #0x05
; O2-DAG: cmp r{{[0-9]+}}, #0x06
; O2-DAG: cmp r{{[0-9]+}}, #0x07
entry:
  switch i8 %x, label %default [
    i8 0, label %c0
    i8 1, label %c1
    i8 2, label %c2
    i8 3, label %c3
    i8 4, label %c4
    i8 5, label %c5
    i8 6, label %c6
    i8 7, label %c7
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
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

define void @m8_i16(i16 noundef %x) {
; CHECK-LABEL: m8_i16:
; O0: cmp wr{{[0-9]+}}, #0x0000
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0001
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0002
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0003
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0004
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0005
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0006
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0007
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: cmp wr{{[0-9]+}}, #0x0003
; O2-NEXT: jsle {{\.LBB[0-9_]+}}
; O2-DAG: cmp wr{{[0-9]+}}, #0x0000
; O2-DAG: cmp wr{{[0-9]+}}, #0x0001
; O2-DAG: cmp wr{{[0-9]+}}, #0x0002
; O2-DAG: cmp wr{{[0-9]+}}, #0x0003
; O2-DAG: cmp wr{{[0-9]+}}, #0x0004
; O2-DAG: cmp wr{{[0-9]+}}, #0x0005
; O2-DAG: cmp wr{{[0-9]+}}, #0x0006
; O2-DAG: cmp wr{{[0-9]+}}, #0x0007
entry:
  switch i16 %x, label %default [
    i16 0, label %c0
    i16 1, label %c1
    i16 2, label %c2
    i16 3, label %c3
    i16 4, label %c4
    i16 5, label %c5
    i16 6, label %c6
    i16 7, label %c7
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
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

define void @m8_i32(i32 noundef %x) {
; CHECK-LABEL: m8_i32:
; O0: mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0003
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0004
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0005
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0006
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0007
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: mov dr4, #0x0003
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jsle {{\.LBB[0-9_]+}}
; O2-DAG: mov dr4, #0x0000
; O2-DAG: mov dr4, #0x0001
; O2-DAG: mov dr4, #0x0002
; O2-DAG: mov dr4, #0x0003
; O2-DAG: mov dr4, #0x0004
; O2-DAG: mov dr4, #0x0005
; O2-DAG: mov dr4, #0x0006
; O2-DAG: mov dr4, #0x0007
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
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
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

define void @m16_i8(i8 noundef %x) {
; CHECK-LABEL: m16_i8:
; O0: cmp r{{[0-9]+}}, #0x00
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x01
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x02
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x03
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x04
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x05
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x06
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x07
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x08
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x09
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x0a
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x0b
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x0c
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x0d
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x0e
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp r{{[0-9]+}}, #0x0f
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; O2-NEXT: jle {{\.LBB[0-9_]+}}
; O2-DAG: cmp r{{[0-9]+}}, #0x00
; O2-DAG: cmp r{{[0-9]+}}, #0x01
; O2-DAG: cmp r{{[0-9]+}}, #0x02
; O2-DAG: cmp r{{[0-9]+}}, #0x03
; O2-DAG: cmp r{{[0-9]+}}, #0x04
; O2-DAG: cmp r{{[0-9]+}}, #0x05
; O2-DAG: cmp r{{[0-9]+}}, #0x06
; O2-DAG: cmp r{{[0-9]+}}, #0x07
; O2-DAG: cmp r{{[0-9]+}}, #0x08
; O2-DAG: cmp r{{[0-9]+}}, #0x09
; O2-DAG: cmp r{{[0-9]+}}, #0x0a
; O2-DAG: cmp r{{[0-9]+}}, #0x0b
; O2-DAG: cmp r{{[0-9]+}}, #0x0c
; O2-DAG: cmp r{{[0-9]+}}, #0x0d
; O2-DAG: cmp r{{[0-9]+}}, #0x0e
; O2-DAG: cmp r{{[0-9]+}}, #0x0f
entry:
  switch i8 %x, label %default [
    i8 0, label %c0
    i8 1, label %c1
    i8 2, label %c2
    i8 3, label %c3
    i8 4, label %c4
    i8 5, label %c5
    i8 6, label %c6
    i8 7, label %c7
    i8 8, label %c8
    i8 9, label %c9
    i8 10, label %c10
    i8 11, label %c11
    i8 12, label %c12
    i8 13, label %c13
    i8 14, label %c14
    i8 15, label %c15
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
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c8:
  store volatile i32 8, ptr @glob, align 1
  ret void
c9:
  store volatile i32 9, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c11:
  store volatile i32 11, ptr @glob, align 1
  ret void
c12:
  store volatile i32 12, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c14:
  store volatile i32 14, ptr @glob, align 1
  ret void
c15:
  store volatile i32 15, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

define void @m16_i16(i16 noundef %x) {
; CHECK-LABEL: m16_i16:
; O0: cmp wr{{[0-9]+}}, #0x0000
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0001
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0002
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0003
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0004
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0005
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0006
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0007
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0008
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x0009
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x000a
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x000b
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x000c
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x000d
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x000e
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: cmp wr{{[0-9]+}}, #0x000f
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: cmp wr{{[0-9]+}}, #0x0007
; O2-NEXT: jsle {{\.LBB[0-9_]+}}
; O2-DAG: cmp wr{{[0-9]+}}, #0x0000
; O2-DAG: cmp wr{{[0-9]+}}, #0x0001
; O2-DAG: cmp wr{{[0-9]+}}, #0x0002
; O2-DAG: cmp wr{{[0-9]+}}, #0x0003
; O2-DAG: cmp wr{{[0-9]+}}, #0x0004
; O2-DAG: cmp wr{{[0-9]+}}, #0x0005
; O2-DAG: cmp wr{{[0-9]+}}, #0x0006
; O2-DAG: cmp wr{{[0-9]+}}, #0x0007
; O2-DAG: cmp wr{{[0-9]+}}, #0x0008
; O2-DAG: cmp wr{{[0-9]+}}, #0x0009
; O2-DAG: cmp wr{{[0-9]+}}, #0x000a
; O2-DAG: cmp wr{{[0-9]+}}, #0x000b
; O2-DAG: cmp wr{{[0-9]+}}, #0x000c
; O2-DAG: cmp wr{{[0-9]+}}, #0x000d
; O2-DAG: cmp wr{{[0-9]+}}, #0x000e
; O2-DAG: cmp wr{{[0-9]+}}, #0x000f
entry:
  switch i16 %x, label %default [
    i16 0, label %c0
    i16 1, label %c1
    i16 2, label %c2
    i16 3, label %c3
    i16 4, label %c4
    i16 5, label %c5
    i16 6, label %c6
    i16 7, label %c7
    i16 8, label %c8
    i16 9, label %c9
    i16 10, label %c10
    i16 11, label %c11
    i16 12, label %c12
    i16 13, label %c13
    i16 14, label %c14
    i16 15, label %c15
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
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c8:
  store volatile i32 8, ptr @glob, align 1
  ret void
c9:
  store volatile i32 9, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c11:
  store volatile i32 11, ptr @glob, align 1
  ret void
c12:
  store volatile i32 12, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c14:
  store volatile i32 14, ptr @glob, align 1
  ret void
c15:
  store volatile i32 15, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

define void @m16_i32(i32 noundef %x) {
; CHECK-LABEL: m16_i32:
; O0: mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0003
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0004
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0005
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0006
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0007
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0008
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x0009
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x000a
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x000b
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x000c
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x000d
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x000e
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0: mov dr4, #0x000f
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2: mov dr4, #0x0007
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jsle {{\.LBB[0-9_]+}}
; O2-DAG: mov dr4, #0x0000
; O2-DAG: mov dr4, #0x0001
; O2-DAG: mov dr4, #0x0002
; O2-DAG: mov dr4, #0x0003
; O2-DAG: mov dr4, #0x0004
; O2-DAG: mov dr4, #0x0005
; O2-DAG: mov dr4, #0x0006
; O2-DAG: mov dr4, #0x0007
; O2-DAG: mov dr4, #0x0008
; O2-DAG: mov dr4, #0x0009
; O2-DAG: mov dr4, #0x000a
; O2-DAG: mov dr4, #0x000b
; O2-DAG: mov dr4, #0x000c
; O2-DAG: mov dr4, #0x000d
; O2-DAG: mov dr4, #0x000e
; O2-DAG: mov dr4, #0x000f
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
    i32 8, label %c8
    i32 9, label %c9
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
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
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c8:
  store volatile i32 8, ptr @glob, align 1
  ret void
c9:
  store volatile i32 9, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c11:
  store volatile i32 11, ptr @glob, align 1
  ret void
c12:
  store volatile i32 12, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c14:
  store volatile i32 14, ptr @glob, align 1
  ret void
c15:
  store volatile i32 15, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}
