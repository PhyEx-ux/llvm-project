; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
;
; X2: AS4 (`__code`) LOAD lowering through the 24-bit DR unified-space read
; channel (`mov r,@dr`; STC32G manual L80558: XFR/extended access is exactly
; this MOV @DRk family; L125052: the EEPROM FE: window is the same MOV
; channel and explicitly NOT reachable by MOVC on STC32G).
;
; Channel ruling (X2, after the G144K246 code/ecode evidence): AS4 keeps the
; DR channel instead of MOVC @A+DPTR. MOVC would only cover the classic
; 16-bit code window (FF:xxxx), while the DR read covers the classic window,
; ecode (80:0000~FF:FFFF), XFR and the FE: EEPROM mapping with one sequence;
; it also consumes the 32-bit AS4 pointer the frozen layout mandates
; (p4:32:8:8:32) without re-splitting it, and reuses the ISR-campaign DR
; write sequence + the far @dr access forms already verified against
; sdas251 (e.g. "mov r5,@dr4" = 7E 1B 50, "mov r10,@dr4+0x1234" = 29 A1 12 34).
;
; AS4 STORES stay fail-closed (see code-store-error.ll). Tests run on the llc
; default (v2 xsmall) layout: p4 is 32-bit.

@tab   = external addrspace(4) global i8
@tab16 = external addrspace(4) global i16

define i8 @ld4_g() {
; CHECK-LABEL: _ld4_g:
; CHECK:         .db 0x7e, {{.*}}(_tab) >> 8, (_tab)
; CHECK-NEXT:    .db 0x7a, {{.*}}(_tab) >> 16
; CHECK-NEXT:    mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %v = load i8, ptr addrspace(4) @tab
  ret i8 %v
}

; The EEPROM-style FE: constant -- DR write sequence (mov takes the low
; 16 bits of 0x00fe1234, movh the high word) then the unified-space read.
define i8 @ld4_const_fe() {
; CHECK-LABEL: _ld4_const_fe:
; CHECK:         mov dr{{[0-9]+}}, #0x1234
; CHECK-NEXT:    movh dr{{[0-9]+}}, #0x00fe
; CHECK-NEXT:    mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %p = inttoptr i32 16650804 to ptr addrspace(4)
  %v = load i8, ptr addrspace(4) %p
  ret i8 %v
}

; A constant whose high word is zero needs no movh: the mov form already
; zero-fills the upper DR half.
define i8 @ld4_const_low() {
; CHECK-LABEL: _ld4_const_low:
; CHECK:         mov dr{{[0-9]+}}, #0x1234
; CHECK-NOT:     movh
; CHECK-NEXT:    mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %p = inttoptr i32 4660 to ptr addrspace(4)
  %v = load i8, ptr addrspace(4) %p
  ret i8 %v
}

define i8 @ld4_ptr(ptr addrspace(4) %p) {
; CHECK-LABEL: _ld4_ptr:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}
  %v = load i8, ptr addrspace(4) %p
  ret i8 %v
}

; A small positive offset uses the displaced @dr+dis16 form (29 dst dr lo hi);
; larger or negative offsets fold into the 24-bit address first.
define i8 @ld4_ptr_off(ptr addrspace(4) %p) {
; CHECK-LABEL: _ld4_ptr_off:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
  %q = getelementptr i8, ptr addrspace(4) %p, i32 1
  %v = load i8, ptr addrspace(4) %q
  ret i8 %v
}

define i8 @ld4_ptr_neg(ptr addrspace(4) %p) {
; CHECK-LABEL: _ld4_ptr_neg:
; CHECK:         mov r{{[0-9]+}}, @dr{{[0-9]+}}-0x0002
  %q = getelementptr i8, ptr addrspace(4) %p, i32 -2
  %v = load i8, ptr addrspace(4) %q
  ret i8 %v
}

; i16 constants load as two big-endian byte reads through the same channel.
define i16 @ld16_g() {
; CHECK-LABEL: _ld16_g:
; CHECK:         .db 0x7e, {{.*}}(_tab16) >> 8, (_tab16)
; CHECK-NEXT:    .db 0x7a, {{.*}}(_tab16) >> 16
; CHECK-NEXT:    mov [[HI:r[0-9]+]], @dr{{[0-9]+}}
; CHECK-NEXT:    mov [[LO:r[0-9]+]], @dr{{[0-9]+}}+0x0001
; CHECK-NEXT:    mov dpl, [[LO]]
; CHECK-NEXT:    mov dph, [[HI]]
  %v = load i16, ptr addrspace(4) @tab16
  ret i16 %v
}
