; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Phase 11: canonical pointer loads. Addressing forms (encodings verified against sdas251
; V05.50.4):
;   * @dr      : register-indirect, full low-24-bit address
;   * @dr+dis16: displaced; a global folds its offset into `mov wr,#(_sym+off)`
;   * dir8     : direct, constant address <= 0xff (0x00-0x7f page-zero edata,
;                0x80-0xff SFR -- the SFRs are ONLY reachable this way; the
;                same numeric address via @dr is region-00 edata)
; IR i16 objects use two byte loads with
; BIG-ENDIAN lane mapping (mem[base] -> hi lane / sub_hi8, mem[base+1] ->
; lo lane / sub_lo8). The load16 tests lock that order: displacement 0x0000
; must land in the register that reaches dph (i16 return: dpl=lo, dph=hi).

; The globals below are external on purpose: these tests lock the symbol
; address materialization encodings, which are identical for external and
; defined globals, and defined global data is (loudly) rejected until data
; areas exist -- see global-data-error.ll.

@gv8  = external global i8
@gv16 = external global i16

;-----------------------------------------------------------------------------
; Byte loads
;-----------------------------------------------------------------------------

define i8 @load8_ptr(ptr %p) {
; CHECK-LABEL: _load8_ptr:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}
; CHECK:         mov dpl, r{{[0-9]+}}
  %v = load i8, ptr %p
  ret i8 %v
}

define i8 @load8_ptr_off(ptr %p) {
; CHECK-LABEL: _load8_ptr_off:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
  %q = getelementptr i8, ptr %p, i16 1
  %v = load i8, ptr %q
  ret i8 %v
}

; An i16 GEP index is sign-extended to i32: 65534 denotes -2, not +65534.
define i8 @load8_ptr_bigdisp(ptr %p) {
; CHECK-LABEL: _load8_ptr_bigdisp:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}-0x0002
  %q = getelementptr i8, ptr %p, i16 65534
  %v = load i8, ptr %q
  ret i8 %v
}

; Negative offset uses the signed DR indexed form.
define i8 @load8_ptr_neg(ptr %p) {
; CHECK-LABEL: _load8_ptr_neg:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}-0x0001
  %q = getelementptr i8, ptr %p, i16 -1
  %v = load i8, ptr %q
  ret i8 %v
}

define i8 @load8_g() {
; CHECK-LABEL: _load8_g:
; CHECK:         .db 0x7e, {{.*}}(_gv8) >> 8, (_gv8)
; CHECK-NEXT:    .db 0x7a, {{.*}}(_gv8) >> 16
; CHECK-NEXT:    mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %v = load i8, ptr @gv8
  ret i8 %v
}

; A global plus a constant offset stays symbolic: `mov wr,#(gv8+1)`
; (sdas251/sdld-verified form), not a dis16 byte pair.
define i8 @load8_g_off() {
; CHECK-LABEL: _load8_g_off:
; CHECK:         .db 0x7e, {{.*}}(_gv8+1) >> 8, (_gv8+1)
; CHECK-NEXT:    .db 0x7a, {{.*}}(_gv8+1) >> 16
; CHECK-NEXT:    mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %q = getelementptr i8, ptr @gv8, i16 1
  %v = load i8, ptr %q
  ret i8 %v
}

; Constant address <= 0xff uses the direct form: page-zero edata at 0x30.
define i8 @load8_direct() {
; CHECK-LABEL: _load8_direct:
; CHECK:         mov r{{[0-9]+}}, 0x30
  %p = inttoptr i16 48 to ptr
  %v = load i8, ptr %p
  ret i8 %v
}

; The same direct form at 0x99 reaches the SFR space (e.g. SBUF) -- this is
; the ONLY encoding that does: `mov wr,#0x99; mov r,@dr` would address
; region-00 edata instead (address-space trap, see MCS251ISelLowering.cpp).
define i8 @load8_sfr() {
; CHECK-LABEL: _load8_sfr:
; CHECK:         mov r{{[0-9]+}}, 0x99
  %p = inttoptr i16 153 to ptr
  %v = load i8, ptr %p
  ret i8 %v
}

; Constant address above the direct range: materialise + @dr.
define i8 @load8_abs16() {
; CHECK-LABEL: _load8_abs16:
; CHECK:         mov dr{{[0-9]+}}, #0x1234
; CHECK-NEXT:    mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %p = inttoptr i16 4660 to ptr
  %v = load i8, ptr %p
  ret i8 %v
}

; Volatile: same instruction sequence (ordering/un-merging is enforced by
; the MachineMemOperand volatile flag, not by a different encoding).
define i8 @load8_volatile(ptr %p) {
; CHECK-LABEL: _load8_volatile:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %v = load volatile i8, ptr %p
  ret i8 %v
}

;-----------------------------------------------------------------------------
; Word loads (two byte loads, big-endian)
;-----------------------------------------------------------------------------

define i16 @load16_ptr(ptr %p) {
; CHECK-LABEL: _load16_ptr:
; Displacement 0x0000 loads the HIGH byte; it must be the register that
; later reaches dph. Displacement 0x0001 is the LOW byte -> dpl.
; CHECK:         mov [[HI:r[0-9]+]], @dr{{[0-9]+}}
; CHECK-NEXT:    mov [[LO:r[0-9]+]], @dr{{[0-9]+}}+0x0001
; CHECK-NEXT:    mov dpl, [[LO]]
; CHECK-NEXT:    mov dph, [[HI]]
  %v = load i16, ptr %p
  ret i16 %v
}

define i16 @load16_g() {
; CHECK-LABEL: _load16_g:
; CHECK:         .db 0x7e, {{.*}}(_gv16) >> 8, (_gv16)
; CHECK-NEXT:    .db 0x7a, {{.*}}(_gv16) >> 16
; CHECK-NEXT:    mov [[HI:r[0-9]+]], @dr{{[0-9]+}}
; CHECK-NEXT:    mov [[LO:r[0-9]+]], @dr{{[0-9]+}}+0x0001
; CHECK-NEXT:    mov dpl, [[LO]]
; CHECK-NEXT:    mov dph, [[HI]]
  %v = load i16, ptr @gv16
  ret i16 %v
}

; A displaced i16 load: base+1 -> hi at +0x0001, lo at +0x0002.
define i16 @load16_ptr_off(ptr %p) {
; CHECK-LABEL: _load16_ptr_off:
; CHECK:         mov [[HI:r[0-9]+]], @dr{{[0-9]+}}+0x0001
; CHECK-NEXT:    mov [[LO:r[0-9]+]], @dr{{[0-9]+}}+0x0002
; CHECK-NEXT:    mov dpl, [[LO]]
; CHECK-NEXT:    mov dph, [[HI]]
  %q = getelementptr i8, ptr %p, i16 1
  %v = load i16, ptr %q
  ret i16 %v
}

;-----------------------------------------------------------------------------
; Widening load (zextload i8)
;-----------------------------------------------------------------------------

; The zextload expands to a byte load widened with a zero hi lane
; (zero + REG_SEQUENCE); on the return path dph must receive the zero.
define i16 @load8_zext(ptr %p) {
; CHECK-LABEL: _load8_zext:
; CHECK-DAG:     mov [[LO:r[0-9]+]], @dr{{[0-9]+}}
; CHECK-DAG:     mov [[HI:r[0-9]+]], #0x00
; CHECK:         mov dpl, [[LO]]
; CHECK-NEXT:    mov dph, [[HI]]
  %v = load i8, ptr %p
  %z = zext i8 %v to i16
  ret i16 %z
}
