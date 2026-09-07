; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=FI

; Sign extension and truncating-store roundtrips.
;
; DAGCombiner folds trunc(sext) back to the source before any MachineNode
; exists ("fold (truncate (ext x))", DAGCombiner.cpp:17681-17694), so a
; trunc directly applied to a sext never exercises the extension result's
; lanes: the narrow store receives the folded-back ORIGINAL value. The
; first three functions keep that IR shape as source coverage (each wide
; value also carries an independent observable use so the sext itself
; survives); no ordering between the narrow store and the extension core is
; asserted, and lane identity is not asserted in asm at all.
;
; The rt_volatile_reload_* functions build a REAL truncation chain instead:
; sext -> full-width volatile store -> volatile reload -> truncate -> narrow
; store. The volatile reload defeats both the trunc(ext) fold and
; store-to-load forwarding, so the stored bytes are runtime lanes of the
; reloaded widened value. The FI run (-stop-after=finalize-isel, before
; TwoAddressInstructionPass eliminates the REG_SEQUENCEs) binds the whole
; chain: the reload's LAST byte load, its vreg as the corresponding lane
; INPUT of the word/dword assembly, the truncation extraction chain off
; that assembly, and the narrow store's data operand.
;
; Width combinations covered: i32->i8, i16->i8, i32->i16.
;
; First-lit notes (settled against the measured output): COPY subregister
; sources print in the dot form (`%27.sub_lo16`), operands may carry
; `killed`, and makeDR emits its REG_SEQUENCE operands as [Lo, sub_lo16,
; Hi, sub_hi16] -- the low word pairs with sub_lo16 FIRST; the FI patterns
; below use these measured forms, and the LW -> low-word binding is the
; semantic content they lock.

@gv8  = external global i8
@gv16 = external global i16
@gv32 = external global i32

;-----------------------------------------------------------------------------
; Fold-back shapes (source coverage; see header)
;-----------------------------------------------------------------------------

define i32 @rt_i8_sext32_store8(ptr %p) {
; CHECK-LABEL: _rt_i8_sext32_store8:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK-DAG: mov dr{{[0-9]+}}, #0xff80
; CHECK-DAG: movh dr{{[0-9]+}}, #0xffff
; CHECK-DAG: add dr{{[0-9]+}}, dr{{[0-9]+}}
; the narrow store exists (its operand is the folded-back source byte)
; CHECK-DAG: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
  %c = load i8, ptr %p
  %v = sext i8 %c to i32
  %t = trunc i32 %v to i8
  store i8 %t, ptr @gv8
  %r = add i32 %v, 1
  ret i32 %r
}

define i16 @rt_i8_sext16_store8(ptr %p) {
; CHECK-LABEL: _rt_i8_sext16_store8:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK-DAG: add wr{{[0-9]+}}, #0xff80
; CHECK-DAG: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
  %c = load i8, ptr %p
  %w = sext i8 %c to i16
  %t = trunc i16 %w to i8
  store i8 %t, ptr @gv8
  %r = add i16 %w, 1
  ret i16 %r
}

define i32 @rt_i16_sext32_store16(ptr %p) {
; CHECK-LABEL: _rt_i16_sext32_store16:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-NEXT: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK-DAG: mov dr{{[0-9]+}}, #0x8000
; CHECK-DAG: movh dr{{[0-9]+}}, #0xffff
; CHECK-DAG: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-DAG: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
  %s = load i16, ptr %p
  %v = sext i16 %s to i32
  %t = trunc i32 %v to i16
  store i16 %t, ptr @gv16
  %r = add i32 %v, 1
  ret i32 %r
}

;-----------------------------------------------------------------------------
; True truncation chains through memory (volatile full-width park/reload)
;-----------------------------------------------------------------------------

; i32 -> i8: the reload's disp-3 byte is the dword's least significant
; byte; the FI chain binds it as the sub_lo8 input of the low word, the low
; word as the sub_lo16 input of the dword, the truncation extraction chain
; off that dword, and the byte store's data operand.
define i32 @rt_volatile_reload_trunc8(ptr %p) {
; CHECK-LABEL: _rt_volatile_reload_trunc8:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; volatile wide store, then volatile reload of the same slot (chain order)
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}+0x0002, r{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}+0x0003, r{{[0-9]+}}
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; FI-LABEL: name: rt_volatile_reload_trunc8
; FI: %[[B3:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 3
; FI: %[[LW:[0-9]+]]:gpr16 = REG_SEQUENCE {{(killed )?}}%{{[0-9]+}}, %subreg.sub_hi8, {{(killed )?}}%[[B3]], %subreg.sub_lo8
; FI: %[[R:[0-9]+]]:gpr32 = REG_SEQUENCE {{(killed )?}}%[[LW]], %subreg.sub_lo16, {{(killed )?}}%{{[0-9]+}}, %subreg.sub_hi16
; FI: %[[W:[0-9]+]]:gpr16 = COPY %[[R]].sub_lo16
; FI: %[[B:[0-9]+]]:gpr8 = COPY %[[W]].sub_lo8
; FI: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 0, {{(killed )?}}%[[B]]
; FI: ERET
entry:
  %c = load i8, ptr %p
  %v = sext i8 %c to i32
  store volatile i32 %v, ptr @gv32
  %r = load volatile i32, ptr @gv32
  %t = trunc i32 %r to i8
  store i8 %t, ptr @gv8
  %s = add i32 %r, 1
  ret i32 %s
}

; i16 -> i8: the i16 extension parked/reloaded through @gv16; the reload's
; disp-1 byte is bound as the word's sub_lo8 input, the truncation reads
; the assembled word's sub_lo8, and that vreg is the store's data operand.
define i16 @rt_volatile_reload_trunc8_16(ptr %p) {
; CHECK-LABEL: _rt_volatile_reload_trunc8_16:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK: add wr{{[0-9]+}}, #0xff80
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; FI-LABEL: name: rt_volatile_reload_trunc8_16
; FI: %[[B1:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 1
; FI: %[[R:[0-9]+]]:gpr16 = REG_SEQUENCE {{(killed )?}}%{{[0-9]+}}, %subreg.sub_hi8, {{(killed )?}}%[[B1]], %subreg.sub_lo8
; FI: %[[B:[0-9]+]]:gpr8 = COPY %[[R]].sub_lo8
; FI: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 0, {{(killed )?}}%[[B]]
; FI: ERET
entry:
  %c = load i8, ptr %p
  %w = sext i8 %c to i16
  store volatile i16 %w, ptr @gv16
  %r = load volatile i16, ptr @gv16
  %t = trunc i16 %r to i8
  store i8 %t, ptr @gv8
  %s = add i16 %r, 1
  ret i16 %s
}

; i32 -> i16: the i16 source widened to i32, parked/reloaded through
; @gv32, then truncated to the stored word. Same reload-assembly binding
; as the i32->i8 case, plus BOTH stored lanes (sub_hi8/sub_lo8 of the
; truncated word) bound to the store.
define i32 @rt_volatile_reload_trunc16(ptr %p) {
; CHECK-LABEL: _rt_volatile_reload_trunc16:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-NEXT: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0x8000
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}+0x0002, r{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}+0x0003, r{{[0-9]+}}
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; FI-LABEL: name: rt_volatile_reload_trunc16
; FI: %[[B3:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 3
; FI: %[[LW:[0-9]+]]:gpr16 = REG_SEQUENCE {{(killed )?}}%{{[0-9]+}}, %subreg.sub_hi8, {{(killed )?}}%[[B3]], %subreg.sub_lo8
; FI: %[[R:[0-9]+]]:gpr32 = REG_SEQUENCE {{(killed )?}}%[[LW]], %subreg.sub_lo16, {{(killed )?}}%{{[0-9]+}}, %subreg.sub_hi16
; FI: %[[W:[0-9]+]]:gpr16 = COPY %[[R]].sub_lo16
; FI: %[[H:[0-9]+]]:gpr8 = COPY %[[W]].sub_hi8
; FI: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 0, {{(killed )?}}%[[H]]
; FI: %[[L:[0-9]+]]:gpr8 = COPY %[[W]].sub_lo8
; FI: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 1, {{(killed )?}}%[[L]]
; FI: ERET
entry:
  %s = load i16, ptr %p
  %v = sext i16 %s to i32
  store volatile i32 %v, ptr @gv32
  %r = load volatile i32, ptr @gv32
  %t = trunc i32 %r to i16
  store i16 %t, ptr @gv16
  %w = add i32 %r, 1
  ret i32 %w
}

;-----------------------------------------------------------------------------
; Plain wide store + reload (volatile reload defeats forwarding): the four
; stored bytes are runtime lanes of the ADD32 result, the reload is the
; four-byte big-endian load chain. Kept from the original submission.
;-----------------------------------------------------------------------------

define i32 @rt_wide_store_reload(ptr %p) {
; CHECK-LABEL: _rt_wide_store_reload:
; closes the FI region of rt_volatile_reload_trunc16 (no FI checks here)
; FI-LABEL: name: rt_wide_store_reload{{$}}
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}+0x0002, r{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}+0x0003, r{{[0-9]+}}
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
  %c = load volatile i8, ptr %p
  %v = sext i8 %c to i32
  store i32 %v, ptr %p
  %r = load volatile i32, ptr %p
  %s = add i32 %r, 1
  ret i32 %s
}
