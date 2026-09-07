; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s --check-prefixes=ALL
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefixes=ALL,O2

; Constant-divisor folding and division-by-zero codegen fixtures for the
; signed/remainder libcall family (division design v4 §7.5/§8.1/§8.3).
; Folding by legal constant divisors is allowed and may eliminate the call
; (SPEC 2026-09-07 §1.1: "合法常量折叠不要求保留调用"). The /1 and %1
; folds (DAGCombiner simplifyDivRem) are asserted at both levels; the
; sdiv X,-1 -> 0-X and urem X,-1 -> select folds are only asserted at -O2
; (no call), while -O0 keeps successful compilation and machine verification
; (the call may legitimately remain at -O0).
;
; The division-by-zero fixtures only pin code generation: the selected
; lowering must not synthesize a trap/crash sequence. Per LangRef, division
; by zero is undefined behavior; no runtime trap/value/termination contract
; is promised for the helpers (§8.3, §10-Q2). CHECK-NOT: trap here is
; strictly a codegen regression check for these fixtures, NOT a runtime
; behavior promise.

; sdiv by 1 folds to the operand (simplifyDivRem, both levels).
define i16 @sdiv16_one(i16 %a) {
; ALL-LABEL: _sdiv16_one:
; ALL-NOT: ecall
; ALL: eret
  %q = sdiv i16 %a, 1
  ret i16 %q
}
; srem by 1 folds to zero (simplifyDivRem, both levels).
define i16 @srem16_one(i16 %a) {
; ALL-LABEL: _srem16_one:
; ALL-NOT: ecall
; ALL: eret
  %q = srem i16 %a, 1
  ret i16 %q
}
; urem by 1 folds to zero (simplifyDivRem, both levels).
define i16 @urem16_one(i16 %a) {
; ALL-LABEL: _urem16_one:
; ALL-NOT: ecall
; ALL: eret
  %q = urem i16 %a, 1
  ret i16 %q
}
; sdiv by -1 folds to 0-X (visitSDIV; asserted at -O2 only).
define i16 @sdiv16_negone(i16 %a) {
; ALL-LABEL: _sdiv16_negone:
; O2-NOT: ecall
; ALL: eret
  %q = sdiv i16 %a, -1
  ret i16 %q
}
; urem by -1 folds to select(X==-1, 0, X) (visitREM; asserted at -O2 only).
define i32 @urem32_negone(i32 %a) {
; ALL-LABEL: _urem32_negone:
; O2-NOT: ecall
; ALL: eret
  %q = urem i32 %a, -1
  ret i32 %q
}
; Division by zero (UB): codegen must not synthesize a trap at either level
; (the node may or may not fold to undef; no value/trap contract is
; asserted -- udiv.ll div32_zero precedent).
define i16 @sdiv16_zero(i16 %a) {
; ALL-LABEL: _sdiv16_zero:
; ALL-NOT: trap
; ALL: eret
  %q = sdiv i16 %a, 0
  ret i16 %q
}
define i16 @srem16_zero(i16 %a) {
; ALL-LABEL: _srem16_zero:
; ALL-NOT: trap
; ALL: eret
  %q = srem i16 %a, 0
  ret i16 %q
}
define i8 @urem8_zero(i8 %a) {
; ALL-LABEL: _urem8_zero:
; ALL-NOT: trap
; ALL: eret
  %q = urem i8 %a, 0
  ret i8 %q
}
