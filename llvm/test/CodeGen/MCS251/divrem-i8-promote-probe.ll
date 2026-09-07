; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s

; i8 IR-level probe (division design v4 §8.1): C's int8_t arithmetic
; promotes to 32-bit, so the i8 IR contract is NOT covered by any C kernel.
; This file is hand-written IR and pins the Promote-to-i16 lowering.
;
; ALL operands are volatile-fed (both halves), so no constant-divisor fold
; can remove the node: sdiv X,-1 folds to 0-X with a non-constant dividend
; (DAGCombiner visitSDIV), which is why the signed probe below must keep the
; divisor dynamic too. With both operands dynamic the node provably reaches
; the operation legalizer's PromoteNode and then the i16 libcall.
;
; UB classification (LangRef) applies to the IR inputs: the runtime fixture
; (validation side, §8.7) feeds the -128 / -1 extreme pair, but this lit
; file asserts ONLY the code-generation shape (ecall __<i16 helper>), never
; an executed value (§8.3: no trap/value/termination contract for UB input).

@a8 = external global i8
@b8 = external global i8
@c8 = external global i8

define i8 @sdiv8_probe() {
; CHECK-LABEL: _sdiv8_probe:
; CHECK: __divsint_PARM_2
; CHECK: ecall __divsint
; CHECK: eret
  %a = load volatile i8, ptr @a8, align 1
  %b = load volatile i8, ptr @b8, align 1
  %r = sdiv i8 %a, %b
  ret i8 %r
}

define i8 @srem8_probe() {
; CHECK-LABEL: _srem8_probe:
; CHECK: __modsint_PARM_2
; CHECK: ecall __modsint
; CHECK: eret
  %a = load volatile i8, ptr @a8, align 1
  %b = load volatile i8, ptr @b8, align 1
  %r = srem i8 %a, %b
  ret i8 %r
}

define i8 @urem8_probe() {
; CHECK-LABEL: _urem8_probe:
; CHECK: __moduint_PARM_2
; CHECK: ecall __moduint
; CHECK: eret
  %a = load volatile i8, ptr @a8, align 1
  %b = load volatile i8, ptr @b8, align 1
  %r = urem i8 %a, %b
  ret i8 %r
}

; Signed extreme-pair probe: the runtime fixture feeds -128 and -1 through
; @a8/@c8 (UB at the IR level -- no value contract asserted here, §8.3/§10-Q2).
; Both operands are volatile so the IR-level sdiv survives to codegen and is
; selected as the promoted i16 libcall; the constant -1 is supplied only via
; the volatile global, never as an IR constant, to keep the node alive.
define i8 @sdiv8_extreme() {
; CHECK-LABEL: _sdiv8_extreme:
; CHECK: __divsint_PARM_2
; CHECK: ecall __divsint
; CHECK: eret
  %a = load volatile i8, ptr @a8, align 1   ; -128 at runtime
  %m = load volatile i8, ptr @c8, align 1   ; -1 at runtime
  %r = sdiv i8 %a, %m
  ret i8 %r
}
