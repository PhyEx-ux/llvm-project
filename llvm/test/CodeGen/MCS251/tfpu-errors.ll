; G7 S3 (G7-FLOAT-DESIGN-draft.md §2.3 P-4 negatives / §4 S3 matrix): the
; IR-level gates around the TFPU intrinsic family. One mutated factor per
; fixture (split-file), so each diagnostic is attributable to exactly one
; shape.
;
;   1. Generic float math intrinsics (llvm.sin.f32) stay RC-5-rejected with
;      the frozen soft-float message -- the TFPU family does NOT open a
;      generic math hole.
;   2. An UNREGISTERED llvm.mcs251.* name carrying f32 operands is equally
;      RC-5-rejected (the G7 probe rev1_ir_unknown_target_intrinsic
;      behaviour, now pinned as a regression test).
;   3. A REGISTERED TFPU name with the WRONG signature (a float-typed
;      spelling of llvm.mcs251.tfpu.sin) is caught by the generic IR
;      verifier on the ordinary path ("input module cannot be verified"),
;      and by the ContractCheck ID whitelist + exact i32-signature gate
;      when the verifier is disabled (the P09 A.2 "disable-verify entry is
;      still policed by the target check" pattern).
;
; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/generic_math.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=RC5
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/generic_math.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=RC5
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/unknown_name.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=RC5
; RUN: not llc -mtriple=mcs251 -O2 %t/wrong_signature.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=VERIF
; RUN: not --crash llc -mtriple=mcs251 -O2 -disable-verify %t/wrong_signature.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG

; RC5: LLVM ERROR: MCS251 contract violation: f32/f64 intrinsic operation is not yet implemented; soft-float runtime is not connected
; VERIF: intrinsic return type expected i32, but got float
; SIG: LLVM ERROR: MCS251 contract violation: MCS251 TFPU intrinsic: invalid signature (the connected form is the i32 bit-pattern intrinsic; float-typed or wrong-arity spellings are rejected)

;--- generic_math.ll
target triple = "mcs251"
define float @generic_math(float %x) {
  %r = call float @llvm.sin.f32(float %x)
  ret float %r
}
declare float @llvm.sin.f32(float)

;--- unknown_name.ll
; An unregistered target-namespaced name (same f32 shape the G7 probe
; used): also the RC-5 classification -- the ID does not resolve to the
; whitelisted family.
target triple = "mcs251"
define float @unknown_name(float %x) {
  %r = call float @llvm.mcs251.tfpu.sine(float %x)
  ret float %r
}
declare float @llvm.mcs251.tfpu.sine(float)

;--- wrong_signature.ll
; The registered name with a wrong (float-typed) signature: a float-typed
; spelling resolves to the same intrinsic ID, so the signature gate -- not
; the RC-5 float gate -- must be what answers when the generic verifier is
; out of the way.
target triple = "mcs251"
define float @wrong_signature(float %x) {
  %r = call float @llvm.mcs251.tfpu.sin(float %x)
  ret float %r
}
declare float @llvm.mcs251.tfpu.sin(float)
