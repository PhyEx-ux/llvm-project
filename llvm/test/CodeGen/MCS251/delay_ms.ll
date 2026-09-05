; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; The official delay_ms example shape: an outer loop over the millisecond
; count and an inner 8000-iteration countdown loop, driven by `add -1` /
; `icmp ne 0` / `br`. Structure to check: two compare-and-branch pairs
; (cmp wr,#imm + jcc + ejmp) and loop back edges carried by ejmp.
; At the default opt level the loop passes rotate the inner counter, so
; 8000 appears as the wrap-around constant 0xe0c0 (65536 - 8000) counting
; up to zero; the iteration count is unchanged.

define void @delay_ms(i16 %ms) {
; CHECK-LABEL: _delay_ms:
; outer: ms-1 == 0 -> done (canonicalised eq, skip jump jne)
; CHECK: add wr{{[0-9]+}}, #0xffff
; CHECK: cmp wr{{[0-9]+}}, #0x0000
; CHECK: jne .LBB{{[0-9_]+}}
; CHECK: ejmp .LBB{{[0-9_]+}}
; inner preheader: 8000 iterations as 65536-8000 = 0xe0c0
; CHECK: mov wr{{[0-9]+}}, #0xe0c0
; inner: counter == 0 -> back to outer (skip jump je)
; CHECK: cmp wr{{[0-9]+}}, #0x0000
; CHECK: je .LBB{{[0-9_]+}}
; back edge of the inner loop
; CHECK: ejmp .LBB{{[0-9_]+}}
; outer back edge after the inner loop exits
; CHECK: ejmp .LBB{{[0-9_]+}}
; CHECK: eret
entry:
  br label %outer
outer:
  %o = phi i16 [ %ms, %entry ], [ %o.next, %inner ]
  %o.next = add i16 %o, -1
  %oc = icmp eq i16 %o.next, 0
  br i1 %oc, label %done, label %inner
inner:
  %i = phi i16 [ 8000, %outer ], [ %i.next, %inner ]
  %i.next = add i16 %i, -1
  %ic = icmp ne i16 %i.next, 0
  br i1 %ic, label %inner, label %outer
done:
  ret void
}
