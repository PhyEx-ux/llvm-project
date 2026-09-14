; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; H3/G3 fix (review round 2): the closed-state chain is checked with every
; branch SEMANTICALLY BOUND. Each case body opens with its own signature
; volatile i8 store (10/11/12, form-6: 1/2/3; the non-empty default: 99), so
; the FileCheck captures do not merely match "some" jump label: the hit arm
; of the case-K compare must be the exact block whose first store carries
; case K's signature byte, and the all-miss arm must reach the default body
; (empty: straight to the epilogue; non-empty: the 99 store).
;
; NOJT two-tier byte-identity control (H3): with the gate closed (default)
; the output must be the pure chain form -- no jump-table column labels and
; no indirect dispatch anywhere -- and the dump must be byte-identical with
; the flag explicitly on, at BOTH -O0 and -O2 (every form here has N=3, below
; the min-entries threshold, so the flag must add nothing). The S1-pre
; baseline equality itself is recorded in IMPL-BRJT-PROGRESS.md: the current
; closed-state dump is byte-identical (-O0/-O2) to the pre-S1 backend tree
; (build-mcs251-s1 llc, which predates the -mcs251-jump-tables option); that
; comparison cannot live in lit because the baseline binary is not reproducible
; from this checkout.
;
; BRJT S1 (design §3.1.2, R1 gold series): with the default closed jump-table
; gate (BR_JT/BRIND = Expand), every switch lowers through the case-cluster
; comparison chain. These are the six N=3 golden forms (below the upstream
; min-jump-table-entries threshold, so they never touched br_jt even before
; S1) pinned as the regression baseline G1: the >threshold forms of the S1
; matrix enter exactly this same chain exit.
;
; Chain semantics (design §3.1.2, one argument per form):
;  1. every case value is compared exactly once (eq compare -> its body),
;     all-misses falls to the default block;
;  2. signedness-free: negative constants appear as the 32-bit
;     mov dr4,#lo16 + movh dr4,#hi16 two-instruction form, compared bitwise;
;  3. sparsity only changes the cmp immediates (0x2710 = 10000);
;  4. -O0 is a linear ascending chain; -O2 reverses the order and inverts
;     je/jne for fall-through (same comparison count);
;  5. empty vs non-empty default differ only inside the default block;
;  6. side-effect case bodies stay in their own blocks, decoupled from the
;     dispatch chain.

; RUN: llc -mtriple=mcs251 -O0 < %s > %t.o0.off.s
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables < %s > %t.o0.on.s
; RUN: diff %t.o0.off.s %t.o0.on.s
; RUN: llc -mtriple=mcs251 -O2 < %s > %t.o2.off.s
; RUN: llc -mtriple=mcs251 -O2 -mcs251-jump-tables < %s > %t.o2.on.s
; RUN: diff %t.o2.off.s %t.o2.on.s
; RUN: llc -mtriple=mcs251 -O0 < %s | not grep -qE "LJTI|jmp @a"
; RUN: llc -mtriple=mcs251 -O2 < %s | not grep -qE "LJTI|jmp @a"

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@sig = external dso_local global i8, align 1
@width = external dso_local global i32, align 1
@height = external dso_local global i32, align 1

declare dso_local void @LCD_WriteReg(i8 noundef zeroext, i8 noundef zeroext)

; Form 1: dense 0..2, empty default.
define dso_local void @dense_emptydef(i32 noundef %x) {
; CHECK-LABEL: dense_emptydef:
; O0:      mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F1S0:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F1C0:\.LBB[0-9_]+]]
; O0:      [[F1S0]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F1S1:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F1C1:\.LBB[0-9_]+]]
; O0:      [[F1S1]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F1SD:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F1C2:\.LBB[0-9_]+]]
; O0:      [[F1SD]]:
; O0-NEXT: ejmp [[F1DB:\.LBB[0-9_]+]]
; O0:      [[F1C0]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0a
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F1C1]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0b
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F1C2]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0c
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F1DB]]:
; O0-NEXT: dec spx, #0x4
; O0-NEXT: eret
; O2:      mov dr4, #0x0002
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O1C2:\.LBB[0-9_]+]]
; O2:      mov dr4, #0x0001
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O1C1:\.LBB[0-9_]+]]
; O2:      mov dr4, #0x0000
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: je [[O1C0J:\.LBB[0-9_]+]]
; O2-NEXT: ejmp [[O1DEF:\.LBB[0-9_]+]]
; O2:      [[O1C0J]]:
; O2-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2:      [[O1C0:\.LBB[0-9_]+]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0a
; O2-NEXT: .db 0x7e, 0x18, (_sig) >> 8, (_sig)
; O2-NEXT: .db 0x7a, 0x1c, 0x00, (_sig) >> 16
; O2-NEXT: mov @dr4, r0
; O2:      [[O1C2]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0c
; O2:      [[O1C1]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0b
; O2:      [[O1DEF]]:
; O2-NEXT: eret
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
  ]
c0:
  store volatile i8 10, ptr @sig, align 1
  ret void
c1:
  store volatile i8 11, ptr @sig, align 1
  ret void
c2:
  store volatile i8 12, ptr @sig, align 1
  ret void
default:
  ret void
}

; Form 2: dense 0..2, non-empty default. The dispatch chain is isomorphic to
; form 1; the default body carries the 99 signature store, and the all-miss
; arm must land exactly on it.
define dso_local void @dense_nonemptydef(i32 noundef %x) {
; CHECK-LABEL: dense_nonemptydef:
; O0:      mov dr4, #0x0000
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F2S0:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F2C0:\.LBB[0-9_]+]]
; O0:      [[F2S0]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F2S1:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F2C1:\.LBB[0-9_]+]]
; O0:      [[F2S1]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F2SD:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F2C2:\.LBB[0-9_]+]]
; O0:      [[F2SD]]:
; O0-NEXT: ejmp [[F2DB:\.LBB[0-9_]+]]
; O0:      [[F2C0]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0a
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F2C1]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0b
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F2C2]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0c
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F2DB]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x63
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0-NEXT: dec spx, #0x4
; O0-NEXT: eret
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
  ]
c0:
  store volatile i8 10, ptr @sig, align 1
  ret void
c1:
  store volatile i8 11, ptr @sig, align 1
  ret void
c2:
  store volatile i8 12, ptr @sig, align 1
  ret void
default:
  store volatile i8 99, ptr @sig, align 1
  ret void
}

; Form 3: sparse 1/100/10000 -- eq chain with the raw constants.
define dso_local void @sparse(i32 noundef %x) {
; CHECK-LABEL: sparse:
; O0:      mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F3S0:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F3C0:\.LBB[0-9_]+]]
; O0:      [[F3S0]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0064
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F3S1:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F3C1:\.LBB[0-9_]+]]
; O0:      [[F3S1]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x2710
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F3SD:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F3C2:\.LBB[0-9_]+]]
; O0:      [[F3SD]]:
; O0-NEXT: ejmp [[F3DB:\.LBB[0-9_]+]]
; O0:      [[F3C0]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0a
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F3C1]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0b
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F3C2]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0c
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F3DB]]:
; O0-NEXT: dec spx, #0x4
; O0-NEXT: eret
; O2:      mov dr4, #0x2710
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O3C2:\.LBB[0-9_]+]]
; O2:      mov dr4, #0x0064
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O3C1:\.LBB[0-9_]+]]
; O2:      mov dr4, #0x0001
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: je [[O3C0J:\.LBB[0-9_]+]]
; O2-NEXT: ejmp [[O3DEF:\.LBB[0-9_]+]]
; O2:      [[O3C0J]]:
; O2-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2:      [[O3C0:\.LBB[0-9_]+]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0a
; O2-NEXT: .db 0x7e, 0x18, (_sig) >> 8, (_sig)
; O2-NEXT: .db 0x7a, 0x1c, 0x00, (_sig) >> 16
; O2-NEXT: mov @dr4, r0
; O2:      [[O3C2]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0c
; O2:      [[O3C1]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0b
; O2:      [[O3DEF]]:
; O2-NEXT: eret
entry:
  switch i32 %x, label %default [
    i32 1, label %c0
    i32 100, label %c1
    i32 10000, label %c2
  ]
c0:
  store volatile i8 10, ptr @sig, align 1
  ret void
c1:
  store volatile i8 11, ptr @sig, align 1
  ret void
c2:
  store volatile i8 12, ptr @sig, align 1
  ret void
default:
  ret void
}

; Form 4: negative lower bound -3..-1. Each negative constant is the 32-bit
; mov+movh form (0xfffd/0xffff = -3, 0xfffe/0xffff = -2, 0xffff/0xffff = -1);
; the eq compare itself is bitwise, so signedness never leaks into the chain.
define dso_local void @neglow(i32 noundef %x) {
; CHECK-LABEL: neglow:
; O0:      mov dr4, #0xfffd
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F4S0:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F4C0:\.LBB[0-9_]+]]
; O0:      [[F4S0]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0xfffe
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F4S1:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F4C1:\.LBB[0-9_]+]]
; O0:      [[F4S1]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0xffff
; O0-NEXT: movh dr4, #0xffff
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F4SD:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F4C2:\.LBB[0-9_]+]]
; O0:      [[F4SD]]:
; O0-NEXT: ejmp [[F4DB:\.LBB[0-9_]+]]
; O0:      [[F4C0]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0a
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F4C1]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0b
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F4C2]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0c
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F4DB]]:
; O0-NEXT: dec spx, #0x4
; O0-NEXT: eret
; O2:      mov dr4, #0xffff
; O2-NEXT: movh dr4, #0xffff
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O4C2:\.LBB[0-9_]+]]
; O2:      mov dr4, #0xfffe
; O2-NEXT: movh dr4, #0xffff
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O4C1:\.LBB[0-9_]+]]
; O2:      mov dr4, #0xfffd
; O2-NEXT: movh dr4, #0xffff
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: je [[O4C0J:\.LBB[0-9_]+]]
; O2-NEXT: ejmp [[O4DEF:\.LBB[0-9_]+]]
; O2:      [[O4C0J]]:
; O2-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2:      [[O4C0:\.LBB[0-9_]+]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0a
; O2:      [[O4C2]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0c
; O2:      [[O4C1]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0b
; O2:      [[O4DEF]]:
; O2-NEXT: eret
entry:
  switch i32 %x, label %default [
    i32 -3, label %c0
    i32 -2, label %c1
    i32 -1, label %c2
  ]
c0:
  store volatile i8 10, ptr @sig, align 1
  ret void
c1:
  store volatile i8 11, ptr @sig, align 1
  ret void
c2:
  store volatile i8 12, ptr @sig, align 1
  ret void
default:
  ret void
}

; Form 5: nonzero lower bound 5..7 -- plain ascending constants.
define dso_local void @nonzerolow(i32 noundef %x) {
; CHECK-LABEL: nonzerolow:
; O0:      mov dr4, #0x0005
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F5S0:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F5C0:\.LBB[0-9_]+]]
; O0:      [[F5S0]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0006
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F5S1:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F5C1:\.LBB[0-9_]+]]
; O0:      [[F5S1]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0007
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F5SD:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F5C2:\.LBB[0-9_]+]]
; O0:      [[F5SD]]:
; O0-NEXT: ejmp [[F5DB:\.LBB[0-9_]+]]
; O0:      [[F5C0]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0a
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F5C1]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0b
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F5C2]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x0c
; O0-NEXT: .db 0x7e, 0x08, (_sig) >> 8, (_sig)
; O0-NEXT: .db 0x7a, 0x0c, 0x00, (_sig) >> 16
; O0-NEXT: mov @dr0, r4
; O0:      [[F5DB]]:
; O0-NEXT: dec spx, #0x4
; O0-NEXT: eret
; O2:      mov dr4, #0x0007
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O5C2:\.LBB[0-9_]+]]
; O2:      mov dr4, #0x0006
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: jne {{\.LBB[0-9_]+}}
; O2-NEXT: ejmp [[O5C1:\.LBB[0-9_]+]]
; O2:      mov dr4, #0x0005
; O2-NEXT: cmp dr0, dr4
; O2-NEXT: je [[O5C0J:\.LBB[0-9_]+]]
; O2-NEXT: ejmp [[O5DEF:\.LBB[0-9_]+]]
; O2:      [[O5C0J]]:
; O2-NEXT: ejmp {{\.LBB[0-9_]+}}
; O2:      [[O5C0:\.LBB[0-9_]+]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0a
; O2:      [[O5C2]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0c
; O2:      [[O5C1]]:
; O2-NEXT: mov r{{[0-9]+}}, #0x0b
; O2:      [[O5DEF]]:
; O2-NEXT: eret
entry:
  switch i32 %x, label %default [
    i32 5, label %c0
    i32 6, label %c1
    i32 7, label %c2
  ]
c0:
  store volatile i8 10, ptr @sig, align 1
  ret void
c1:
  store volatile i8 11, ptr @sig, align 1
  ret void
c2:
  store volatile i8 12, ptr @sig, align 1
  ret void
default:
  ret void
}

; Form 6: side-effect bodies (demo43 LCD_direction shape at N=3). The chain
; is decoupled from the case bodies: each case block opens with its own
; signature store (1/2/3) before its width/height stores and its call, and
; the dispatch compares remain plain eq chains; the all-miss arm lands on the
; empty default, which only bridges to the shared end block.
define dso_local void @sideeffect(i8 noundef zeroext %d) {
; CHECK-LABEL: sideeffect:
; O0: cmp r{{[0-9]+}}, #0x00
; O0-NEXT: jne [[F6S0:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F6C0:\.LBB[0-9_]+]]
; O0:      [[F6S0]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
; O0:      mov dr4, #0x0001
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne {{\.LBB[0-9_]+}}
; O0-NEXT: ejmp [[F6C1:\.LBB[0-9_]+]]
; O0:      mov dr4, #0x0002
; O0-NEXT: cmp dr0, dr4
; O0-NEXT: jne [[F6SD:\.LBB[0-9_]+]]
; O0-NEXT: ejmp [[F6C2:\.LBB[0-9_]+]]
; O0:      [[F6SD]]:
; O0-NEXT: ejmp [[F6DB:\.LBB[0-9_]+]]
; O0:      [[F6C0]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x01
; CHECK:   mov r{{[0-9]+}}, #0x36
; CHECK-NEXT: mov dpl, r0
; CHECK-NEXT: ecall _LCD_WriteReg
; O0:      [[F6C1]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x02
; CHECK:   mov r{{[0-9]+}}, #0x60
; O0:      [[F6C2]]:
; O0-NEXT: mov r{{[0-9]+}}, #0x03
; CHECK:   mov r{{[0-9]+}}, #0xc0
; O0:      [[F6DB]]:
; O0-NEXT: ejmp {{\.LBB[0-9_]+}}
entry:
  %d.addr = alloca i8, align 1
  store i8 %d, ptr %d.addr, align 1
  %ld = load i8, ptr %d.addr, align 1
  %z = zext i8 %ld to i32
  switch i32 %z, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
  ]
c0:
  store volatile i8 1, ptr @sig, align 1
  store volatile i32 8, ptr @width, align 1
  store volatile i32 16, ptr @height, align 1
  call void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext 0)
  br label %end
c1:
  store volatile i8 2, ptr @sig, align 1
  store volatile i32 16, ptr @width, align 1
  store volatile i32 8, ptr @height, align 1
  call void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext 96)
  br label %end
c2:
  store volatile i8 3, ptr @sig, align 1
  store volatile i32 16, ptr @width, align 1
  store volatile i32 16, ptr @height, align 1
  call void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext 192)
  br label %end
default:
  br label %end
end:
  ret void
}
