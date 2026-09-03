; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 5: the two-address ALU instructions (dst <- dst op src) at byte
; (r, GPR8) and word (wr, GPR16) granularity. The MCS-251 bitwise mnemonics
; are anl/orl/xrl (NOT and/or/xor); encodings verified against sdas251
; V05.50.4 and QEMU stc32g144k246.
;
; The register-form tests only have the single ABI argument available, so
; the second operand has to be computed from it. The operands are chosen so
; that the DAG combiner cannot fold the mix: sub x,x / xor x,x -> 0,
; add x,x -> shl, and x,(and x,c) -> and x,c and sub x,(and x,c) ->
; and x,~c are all real folds.
;
; The immediate forms of sub never reach the SUB8ri/SUB16ri patterns: the
; generic DAG combiner unconditionally canonicalises (sub x, c) into
; (add x, -c) before instruction selection (same for every target; X86 for
; instance undoes it in a target-specific combine, which this backend does
; not implement). The emitted add-of-two's-complement is value-equivalent
; because psw flags are not modelled in this phase.

;-----------------------------------------------------------------------------
; 8-bit immediate forms
;-----------------------------------------------------------------------------

define i8 @add8ri(i8 %a) {
; CHECK-LABEL: add8ri:
; CHECK:         add r{{[0-9]+}}, #0x11
  %r = add i8 %a, 17
  ret i8 %r
}

define i8 @sub8ri(i8 %a) {
; The generic DAG combiner canonicalises (sub x, c) -> (add x, -c)
; unconditionally (DAGCombiner visitSUB, upstream), so the immediate forms
; of sub surface as adds of the two's-complement constant. That is value-
; equivalent on the MCS-251 (psw flags are not modelled in this phase).
; CHECK-LABEL: sub8ri:
; CHECK:         add r{{[0-9]+}}, #0xef
  %r = sub i8 %a, 17
  ret i8 %r
}

define i8 @and8ri(i8 %a) {
; CHECK-LABEL: and8ri:
; CHECK:         anl r{{[0-9]+}}, #0x0f
  %r = and i8 %a, 15
  ret i8 %r
}

define i8 @or8ri(i8 %a) {
; CHECK-LABEL: or8ri:
; CHECK:         orl r{{[0-9]+}}, #0x03
  %r = or i8 %a, 3
  ret i8 %r
}

define i8 @xor8ri(i8 %a) {
; CHECK-LABEL: xor8ri:
; CHECK:         xrl r{{[0-9]+}}, #0x05
  %r = xor i8 %a, 5
  ret i8 %r
}

; Negative constants print as their byte pattern (printImm8 masks the sign
; extension); (sub x, -1) canonicalises to (add x, 1).
define i8 @sub8ri_neg(i8 %a) {
; CHECK-LABEL: sub8ri_neg:
; CHECK:         add r{{[0-9]+}}, #0x01
  %r = sub i8 %a, -1
  ret i8 %r
}

;-----------------------------------------------------------------------------
; 16-bit immediate forms
;-----------------------------------------------------------------------------

define i16 @add16ri(i16 %a) {
; CHECK-LABEL: add16ri:
; CHECK:         add wr{{[0-9]+}}, #0x0011
  %r = add i16 %a, 17
  ret i16 %r
}

define i16 @sub16ri(i16 %a) {
; Same (sub x, c) -> (add x, -c) canonicalisation at word granularity.
; CHECK-LABEL: sub16ri:
; CHECK:         add wr{{[0-9]+}}, #0xffef
  %r = sub i16 %a, 17
  ret i16 %r
}

define i16 @and16ri(i16 %a) {
; CHECK-LABEL: and16ri:
; CHECK:         anl wr{{[0-9]+}}, #0x000f
  %r = and i16 %a, 15
  ret i16 %r
}

define i16 @or16ri(i16 %a) {
; CHECK-LABEL: or16ri:
; CHECK:         orl wr{{[0-9]+}}, #0x0003
  %r = or i16 %a, 3
  ret i16 %r
}

define i16 @xor16ri(i16 %a) {
; CHECK-LABEL: xor16ri:
; CHECK:         xrl wr{{[0-9]+}}, #0x0005
  %r = xor i16 %a, 5
  ret i16 %r
}

;-----------------------------------------------------------------------------
; 8-bit register forms
;-----------------------------------------------------------------------------

define i8 @add8rr(i8 %a) {
; CHECK-LABEL: add8rr:
; CHECK:         anl r{{[0-9]+}}, #0x0f
; CHECK:         add r{{[0-9]+}}, r{{[0-9]+}}
  %m = and i8 %a, 15
  %r = add i8 %a, %m
  ret i8 %r
}

define i8 @sub8rr(i8 %a) {
; The helper must not be (and x, c): sub x, (and x, c) folds to and x, ~c.
; sub x, (or x, c) has no such identity and survives.
; CHECK-LABEL: sub8rr:
; CHECK:         orl r{{[0-9]+}}, #0x03
; CHECK:         sub r{{[0-9]+}}, r{{[0-9]+}}
  %m = or i8 %a, 3
  %r = sub i8 %a, %m
  ret i8 %r
}

; and with (and x, c) would fold; use an arithmetic helper instead.
define i8 @and8rr(i8 %a) {
; CHECK-LABEL: and8rr:
; CHECK:         add r{{[0-9]+}}, #0x01
; CHECK:         anl r{{[0-9]+}}, r{{[0-9]+}}
  %n = add i8 %a, 1
  %r = and i8 %a, %n
  ret i8 %r
}

define i8 @or8rr(i8 %a) {
; CHECK-LABEL: or8rr:
; CHECK:         add r{{[0-9]+}}, #0x01
; CHECK:         orl r{{[0-9]+}}, r{{[0-9]+}}
  %n = add i8 %a, 1
  %r = or i8 %a, %n
  ret i8 %r
}

define i8 @xor8rr(i8 %a) {
; CHECK-LABEL: xor8rr:
; CHECK:         add r{{[0-9]+}}, #0x01
; CHECK:         xrl r{{[0-9]+}}, r{{[0-9]+}}
  %n = add i8 %a, 1
  %r = xor i8 %a, %n
  ret i8 %r
}

;-----------------------------------------------------------------------------
; 16-bit register forms
;-----------------------------------------------------------------------------

define i16 @add16rr(i16 %a) {
; CHECK-LABEL: add16rr:
; CHECK:         anl wr{{[0-9]+}}, #0x000f
; CHECK:         add wr{{[0-9]+}}, wr{{[0-9]+}}
  %m = and i16 %a, 15
  %r = add i16 %a, %m
  ret i16 %r
}

define i16 @sub16rr(i16 %a) {
; See sub8rr: or-helper avoids the sub x, (and x, c) -> and x, ~c fold.
; CHECK-LABEL: sub16rr:
; CHECK:         orl wr{{[0-9]+}}, #0x0003
; CHECK:         sub wr{{[0-9]+}}, wr{{[0-9]+}}
  %m = or i16 %a, 3
  %r = sub i16 %a, %m
  ret i16 %r
}

define i16 @and16rr(i16 %a) {
; CHECK-LABEL: and16rr:
; CHECK:         add wr{{[0-9]+}}, #0x0001
; CHECK:         anl wr{{[0-9]+}}, wr{{[0-9]+}}
  %n = add i16 %a, 1
  %r = and i16 %a, %n
  ret i16 %r
}

define i16 @or16rr(i16 %a) {
; CHECK-LABEL: or16rr:
; CHECK:         add wr{{[0-9]+}}, #0x0001
; CHECK:         orl wr{{[0-9]+}}, wr{{[0-9]+}}
  %n = add i16 %a, 1
  %r = or i16 %a, %n
  ret i16 %r
}

define i16 @xor16rr(i16 %a) {
; CHECK-LABEL: xor16rr:
; CHECK:         add wr{{[0-9]+}}, #0x0001
; CHECK:         xrl wr{{[0-9]+}}, wr{{[0-9]+}}
  %n = add i16 %a, 1
  %r = xor i16 %a, %n
  ret i16 %r
}
