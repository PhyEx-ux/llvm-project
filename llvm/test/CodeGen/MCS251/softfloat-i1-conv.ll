; G7 S1' review fix (Alice 2026-09-15): the <=32-bit integer whitelist in
; MCS251ContractCheck.cpp admits the signed i1 source, but the lowering path
; used to die at ISel with
;
;   LLVM ERROR: Cannot select: ... sign_extend_inreg ..., ValueType:ch:i1
;
; for `i32 -> trunc i1 -> sitofp float` at both O0 and O2.  The signed i1
; source must first sign-extend the boolean to the i32 helper argument, and
; DAGCombiner folds that `sext i1` into SIGN_EXTEND_INREG with inner type i1:
; Legal by default on this target yet unselectable.  MCS251ISelLowering now
; registers SIGN_EXTEND_INREG/MVT::i1 as Expand, so the generic expander
; rewrites the boolean negation as AND 1 / SUB 0, both of which the i8/i16/i32
; cores already select.  This test freezes that shape.
;
; Semantics being pinned: LLVM/IR defines `sitofp i1 true` as -1.0 (the 1-bit
; value is a full signed integer: true == -1) and `uitofp i1 true` as +1.0.
; So the signed direction must produce the masked negate (anl + sub) before
; calling __floatsisf, while the unsigned direction only masks and calls
; __floatunsisf -- the two helper families stay distinct even at width 1.
;
; Every operand is non-constant so nothing folds the conversion away.  Two
; clang-reachable sources are covered: `trunc i32` (the reported repro) and
; `select i1, float, float` / `icmp`-guarded shapes (what clang emits for
; `(float)((_Bool)x ? -1 : 0)` and similar).
;
; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/i1.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/i1.ll -o - | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/i1.ll -filetype=obj -o /dev/null

; The converted-to-signed-float shape: mask the boolean, negate it (true is
; -1), then call the signed helper.  The `sub` between the `anl` and the call
; is the expanded SIGN_EXTEND_INREG/MVT::i1.
; The regression's actual requirement: the i1 INREG node must not reach ISel
; (the pre-fix failure was `Cannot select: sign_extend_inreg ... i1`). The
; emitted shape differs per level -- O2 folds the icmp->sitofp into a
; constant select of -1.0 (0xBF800000, materialised as `movh dr0, #0xbf80`),
; O0 keeps the branch form -- so assert on the ABSENCE of the faulty node
; plus the presence of the signed -1.0 constant, not on one instruction pair.
; O0-LABEL: _cvt_i1_f32:
; O0-NOT: sign_extend_inreg
; O2-LABEL: _cvt_i1_f32:
; O2: movh dr{{[0-9]+}}, #0xbf80

; O0-LABEL: _s2f_i1_trunc:
; O0: anl {{r|wr}}
; O0: sub {{r|dr}}
; O0: {{ecall|call}} __floatsisf

; The unsigned direction never negates: mask only, then __floatunsisf.
; O0-LABEL: _u2f_i1_trunc:
; O0: anl {{r|wr}}
; O0: {{ecall|call}} __floatunsisf

; A bare `sext i1` must select at both widths; i8 uses the 8-bit ANL/SUB
; pair, i32 uses the word pair.
; O0-LABEL: _sext_i1_i8:
; O0: anl
; O0: sub
; O0-LABEL: _sext_i1_i32:
; O0: anl {{r|wr}}
; O0: sub {{r|dr}}

; The narrow destination still routes through the i32 helper then truncates.
; O0-LABEL: _f2s_i1:
; O0: {{ecall|call}} __fixsfsi

; The clang `select i1, float, float` shape with 0.0/1.0 arms (NOT -1/0 --
; Alice correction; the signed -1 rendering is exercised by cvt_i1_f32 below).
; It shares the same masked-negate core and is helper-free.
; O0-LABEL: _sel_i1_f32:
; O0: anl {{r|wr}}
; O0: sub {{r|dr}}

; O2 mirrors the same shapes: the ANL/SUB pair before the signed helper call
; proves the i1 INREG node no longer reaches ISel.
; O2-LABEL: _s2f_i1_trunc:
; O2: anl {{r|wr}}
; O2: sub {{r|dr}}
; O2: {{ecall|call}} __floatsisf
; O2-LABEL: _u2f_i1_trunc:
; O2: anl {{r|wr}}
; O2: {{ecall|call}} __floatunsisf
; O2-LABEL: _sext_i1_i8:
; O2: anl
; O2: sub
; O2-LABEL: _sext_i1_i32:
; O2: anl {{r|wr}}
; O2: sub {{r|dr}}
; O2-LABEL: _f2s_i1:
; O2: {{ecall|call}} __fixsfsi
; O2-LABEL: _sel_i1_f32:
; O2: anl {{r|wr}}
; O2: sub {{r|dr}}

;--- i1.ll
; The exact clang shape for a SIGNED i1 conversion: `icmp` produces the i1
; (no trunc), and sitofp consumes it. Alice review: this was the form the
; original coverage claimed but did not contain. LLVM's signed i1 semantics
; make true == -1 (NOT C `_Bool`, which promotes to +1). The emitted shape
; differs per level: at O2 the whole icmp->sitofp folds into a constant
; selection of -1.0 (0xBF800000, `movh dr0, #0xbf80`), while O0 keeps the
; branch form. The regression requirement is therefore the ABSENCE of the
; unselectable i1 INREG node (O0-NOT: sign_extend_inreg) plus the signed
; -1.0 constant at O2 -- see the assertions above.
define float @cvt_i1_f32(i32 %x) {
  %c = icmp eq i32 %x, 0
  %r = sitofp i1 %c to float
  ret float %r
}

define float @s2f_i1_trunc(i32 %a) {
  %t = trunc i32 %a to i1
  %r = sitofp i1 %t to float
  ret float %r
}

define float @u2f_i1_trunc(i32 %a) {
  %t = trunc i32 %a to i1
  %r = uitofp i1 %t to float
  ret float %r
}

define i8 @sext_i1_i8(i32 %a) {
  %t = trunc i32 %a to i1
  %r = sext i1 %t to i8
  ret i8 %r
}

define i32 @sext_i1_i32(i32 %a) {
  %t = trunc i32 %a to i1
  %r = sext i1 %t to i32
  ret i32 %r
}

define i1 @f2s_i1(float %a) {
  %r = fptosi float %a to i1
  ret i1 %r
}

; A select that consumes a non-constant i1 (icmp producer, so it cannot fold)
; and feeds a float: 0.0 / 1.0 (Alice correction: NOT the -1/0 shape; the
; signed -1 semantics belong to LLVM's i1 interpretation, exercised below in
; cvt_i1_f32, while C `_Bool` promotes to +1).
define float @sel_i1_f32(i32 %x) {
  %a = and i32 %x, 1
  %c = icmp eq i32 %a, 0
  %r = select i1 %c, float 0.0, float 1.0
  ret float %r
}
