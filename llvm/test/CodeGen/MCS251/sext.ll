; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NO16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=FI

; Pure register sign extensions: the argument byte/word is widened with no
; load involved, locking the rewritten LowerExtend signed branch (offset-
; binary bias identity, QEMU P-A2 forms (a)/(c)/(d)) -- the shape that had
; no positive coverage before (the old setcc+select path was replaced, with
; no baseline to regress against). The SIGN_EXTEND_INREG functions below
; lock the explicit Custom modelling of sext(trunc x): DAGCombiner folds
; sext(trunc) into SIGN_EXTEND_INREG, whose action table is keyed by the
; INNER type, so i8/i16 inner widths must route to the same widening core
; (they previously fell through the default action).
;
; Assertions are deliberately NOT made about the full set of ABI input byte
; copies or the ANY_EXTEND zero-high-word materialisation: dropping dead
; copies/materialisation of unused high lanes is a legal optimisation and
; not part of the I4 contract. The effective-lane rule (inner i8 -> low
; byte of the low word, inner i16 -> low word) and the bias chain are
; locked in the FI run at -stop-after=finalize-isel -- BEFORE
; TwoAddressInstructionPass eliminates the REG_SEQUENCEs and rewrites tied
; operands (its working-COPY shape is locked in sext-pressure.ll). The NO16
; run carries the width-routing sentinel whole-function (an i32 result must
; never take the 16-bit sequence).
;
; First-lit note (anticipated, per review): if the MIR printer spells the
; extraction subregisters as `.sub_lo16`-style operands instead of the
; colon form on COPY sources, only those line patterns need rewording; the
; captured vreg references stay as they are.

define i16 @sext8_16(i8 %a) {
; CHECK-LABEL: _sext8_16:
; CHECK: mov r{{[0-9]+}}, dpl
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK: add wr{{[0-9]+}}, #0xff80
; NO16-LABEL: _sext8_16:
  %e = sext i8 %a to i16
  ret i16 %e
}

define i32 @sext8_32(i8 %a) {
; CHECK-LABEL: _sext8_32:
; CHECK: mov r{{[0-9]+}}, dpl
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _sext8_32:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
; FI-LABEL: name: sext8_32
; FI: %[[BL:[0-9]+]]:gpr32 = MOVDRri 65408
; FI: %[[BIAS:[0-9]+]]:gpr32 = MOVHDRi %[[BL]], 65535
; FI: %{{[0-9]+}}:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%[[BIAS]]
; FI: ERET
  %e = sext i8 %a to i32
  ret i32 %e
}

; The i16 argument arrives as a dptr live-in (dpl = lo lane, dph = hi lane);
; the sign byte is the big-endian Bytes[0] = dph lane.
define i32 @sext16_32(i16 %a) {
; CHECK-LABEL: _sext16_32:
; CHECK-DAG: mov r{{[0-9]+}}, dpl
; CHECK-DAG: mov r{{[0-9]+}}, dph
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0x8000
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _sext16_32:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
; FI-LABEL: name: sext16_32
; FI: %[[BL:[0-9]+]]:gpr32 = MOVDRri 32768
; FI: %[[BIAS:[0-9]+]]:gpr32 = MOVHDRi %[[BL]], 65535
; FI: %{{[0-9]+}}:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%[[BIAS]]
; FI: ERET
  %e = sext i16 %a to i32
  ret i32 %e
}

;-----------------------------------------------------------------------------
; SIGN_EXTEND_INREG regressions (sext(trunc x) after DAGCombining).
; The FI checks bind the XOR input to the effective-lane extraction chain:
; inner i8 -> sub_lo16 -> sub_lo8; inner i16 -> sub_lo16 (then the (d) core
; flips its sub_hi8). The chain hangs off the assembled argument vreg, so
; it proves the lane selection.
;-----------------------------------------------------------------------------

; i32 -> i8 -> i32: inner i8, the value is the wide argument itself.
define i32 @inreg8_32(i32 %x) {
; CHECK-LABEL: _inreg8_32:
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _inreg8_32:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
; FI-LABEL: name: inreg8_32
; FI: %[[A:[0-9]+]]:gpr32 = REG_SEQUENCE
; FI: %[[LW:[0-9]+]]:gpr16 = COPY %[[A]].sub_lo16
; FI: %[[B:[0-9]+]]:gpr8 = COPY %[[LW]].sub_lo8
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[B]], -128
; FI: ERET
  %t = trunc i32 %x to i8
  %e = sext i8 %t to i32
  ret i32 %e
}

; i32 -> i16 -> i32: inner i16, effective lane = sub_lo16 of the argument;
; the (d) core then flips the low word's sub_hi8 (the sign byte).
define i32 @inreg16_32(i32 %x) {
; CHECK-LABEL: _inreg16_32:
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov dr{{[0-9]+}}, #0x8000
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _inreg16_32:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
; FI-LABEL: name: inreg16_32
; FI: %[[A:[0-9]+]]:gpr32 = REG_SEQUENCE
; FI: %[[LW:[0-9]+]]:gpr16 = COPY %[[A]].sub_lo16
; FI: %[[X:[0-9]+]]:gpr8 = COPY %[[LW]].sub_hi8
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[X]], -128
; FI: %[[BL:[0-9]+]]:gpr32 = MOVDRri 32768
; FI: %[[BIAS:[0-9]+]]:gpr32 = MOVHDRi %[[BL]], 65535
; FI: ERET
  %t = trunc i32 %x to i16
  %e = sext i16 %t to i32
  ret i32 %e
}

; i32 -> i8 -> i16: the combiner truncates the operand to i16 first, the
; inreg result width is i16, so the widening core is form (a).
define i16 @inreg8_16(i32 %x) {
; CHECK-LABEL: _inreg8_16:
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: add wr{{[0-9]+}}, #0xff80
; NO16-LABEL: _inreg8_16:
; anchored: "name: inreg8_16" is a prefix of "name: inreg8_16_from16"
; FI-LABEL: name: inreg8_16{{$}}
; FI: %[[A:[0-9]+]]:gpr32 = REG_SEQUENCE
; FI: %[[LW:[0-9]+]]:gpr16 = COPY %[[A]].sub_lo16
; FI: %[[B:[0-9]+]]:gpr8 = COPY %[[LW]].sub_lo8
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[B]], -128
; FI: %{{[0-9]+}}:gpr16 = ADD16ri %{{[0-9]+}}, -128
; FI: ERET
  %t = trunc i32 %x to i8
  %e = sext i8 %t to i16
  ret i16 %e
}

; i16 -> i8 -> i16: same i16 result width, operand already i16 (no
; anyext/truncate inserted by the fold).
define i16 @inreg8_16_from16(i16 %w) {
; CHECK-LABEL: _inreg8_16_from16:
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: add wr{{[0-9]+}}, #0xff80
; NO16-LABEL: _inreg8_16_from16:
; closes the FI region of inreg8_16 (this function has no FI checks)
; FI-LABEL: name: inreg8_16_from16{{$}}
  %t = trunc i16 %w to i8
  %e = sext i8 %t to i16
  ret i16 %e
}

; i16 -> i8 -> i32: the fold widens the operand with ANY_EXTEND before the
; inreg (zero-high-word form); the effective byte still comes from the low
; word, so the widening core is form (c). Whether the unused ANY_EXTEND
; high word is materialised at all is a legal optimisation and not asserted.
define i32 @inreg8_32_from16(i16 %w) {
; CHECK-LABEL: _inreg8_32_from16:
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _inreg8_32_from16:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
  %t = trunc i16 %w to i8
  %e = sext i8 %t to i32
  ret i32 %e
}
