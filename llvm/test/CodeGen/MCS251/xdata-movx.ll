; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
;
; X2-1: AS3 (`__xdata`) load/store lowering through the MOVX @DPTR channel
; with the FULL 24-bit address (DESIGN.md B.2: `__xdata` 32/8, "保持完整
; 24 位有效地址").  Byte-for-byte sdas251 V05.50.4 oracle: "movx a,@dptr" ->
; E0, "movx @dptr,a" -> F0 (single bytes; low nibble 0, no A5 escape); the
; exact E0/F0 object bytes are pinned in xdata-code-bytes.mir, together with
; the DPXL region write "mov 0x84,rN" (7A 21 84 gold).
;
; Every access byte re-points DPXL (SFR 0x84, the MOVX region register,
; Intel 251 manual 3.3.2.2) from address bits [23:16] immediately before the
; MOVX over the low 16 bits -- self-healing, no DPXL value is ever assumed
; to survive an earlier access (retention protocol: the XDATA-CODE design
; supplement in validation/mcs251-models/proposals/).  Constant banks fold
; at compile time (bank 01h is still emitted by design); runtime banks come
; from the pointer lanes or the full 32-bit add carry.
;
; Tests run on the llc default (v2 xsmall) layout: p3 is 32-bit.

@g8  = external addrspace(3) global i8
@g16 = external addrspace(3) global i16

;-----------------------------------------------------------------------------
; Byte loads
;-----------------------------------------------------------------------------

define i8 @ld8_const() {
; CHECK-LABEL: _ld8_const:
; 0x1234 = bank 0x00, window 0x1234: the folded bank byte goes to DPXL first
; (the window materialisation floats freely -- it feeds vreg lanes, only the
; SFR writes are pinned; see the X2-fix note atop xdata-o2-order.ll).
; CHECK:         mov wr{{[0-9]+}}, #0x1234
; CHECK:         mov r{{[0-9]+}}, #0x00
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         mov dph, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
  %p = inttoptr i32 4660 to ptr addrspace(3)
  %v = load i8, ptr addrspace(3) %p
  ret i8 %v
}

define i8 @ld8_g() {
; CHECK-LABEL: _ld8_g:
; The canonical 24-bit address of the global materialises through the
; byte-of-24 relocations (same shape as the AS9 far globals); the bank byte
; reaching DPXL is the linker-resolved bits [23:16] -- never a truncated
; XSEG address.
; CHECK:         .db 0x7e, {{.*}}(_g8) >> 8, (_g8)
; CHECK:         .db 0x7a, {{.*}}0x00, (_g8) >> 16
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %v = load i8, ptr addrspace(3) @g8
  ret i8 %v
}

define i8 @ld8_g_off() {
; CHECK-LABEL: _ld8_g_off:
; A constant GEP offset rides the relocation addend; the bank byte still
; comes from the resolved 24-bit address.
; CHECK:         .db 0x7e, {{.*}}(_g8+7) >> 8, (_g8+7)
; CHECK:         .db 0x7a, {{.*}}(_g8+7) >> 16
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) @g8, i32 7
  %v = load i8, ptr addrspace(3) %q
  ret i8 %v
}

; Runtime pointer: DPXL is re-pointed from the post-fold pointer lanes; any
; constant offset folds in FULL 32-bit arithmetic (a wr add would lose the
; carry into the bank byte at the 64K boundary).
define i8 @ld8_ptr_off(ptr addrspace(3) %p) {
; CHECK-LABEL: _ld8_ptr_off:
; CHECK:         mov dr{{[0-9]+}}, #0x0001
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) %p, i32 1
  %v = load i8, ptr addrspace(3) %q
  ret i8 %v
}

define i8 @ld8_ptr_neg(ptr addrspace(3) %p) {
; CHECK-LABEL: _ld8_ptr_neg:
; CHECK:         mov dr{{[0-9]+}}, #0xfffe
; CHECK-NEXT:    movh dr{{[0-9]+}}, #0xffff
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) %p, i32 -2
  %v = load i8, ptr addrspace(3) %q
  ret i8 %v
}

; A register+register index add stays in the 32-bit pointer so the carry
; into the bank byte is kept; DPXL then gets the post-add bank lane.
define i8 @ld8_ptr_index(ptr addrspace(3) %p, i32 %i) {
; CHECK-LABEL: _ld8_ptr_index:
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) %p, i32 %i
  %v = load i8, ptr addrspace(3) %q
  ret i8 %v
}

; Volatile uses the same instruction sequence (ordering comes from the
; MachineMemOperand, not a different encoding).
define i8 @ld8_volatile(ptr addrspace(3) %p) {
; CHECK-LABEL: _ld8_volatile:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

;-----------------------------------------------------------------------------
; Word/dword loads: one DPXL re-point + MOVX per byte, big-endian object
; layout (mem[base] is the HIGH byte; the returned i16 keeps dpl=lo,
; dph=hi).
;-----------------------------------------------------------------------------

define i16 @ld16_ptr(ptr addrspace(3) %p) {
; CHECK-LABEL: _ld16_ptr:
; the second byte address is the first-byte address +1 (the +1 add may be
; scheduled before the first access), then one DPXL re-point + MOVX per
; byte, big-endian: the value returned in dpl:dph keeps dpl = mem[base+1]
; (low), dph = mem[base].
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK-NEXT:    mov dph, r{{[0-9]+}}
  %v = load i16, ptr addrspace(3) %p
  ret i16 %v
}

;-----------------------------------------------------------------------------
; Byte/word/dword stores
;-----------------------------------------------------------------------------

define void @st8_const() {
; CHECK-LABEL: _st8_const:
; CHECK:         mov wr{{[0-9]+}}, #0x1234
; CHECK:         mov r{{[0-9]+}}, #0x00
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         mov dph, r{{[0-9]+}}
; CHECK:         mov a, r{{[0-9]+}}
; CHECK:         movx @dptr, a
  %p = inttoptr i32 4660 to ptr addrspace(3)
  store i8 42, ptr addrspace(3) %p
  ret void
}

define void @st8_ptr(ptr addrspace(3) %p, i8 %v) {
; CHECK-LABEL: _st8_ptr:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         mov a, r{{[0-9]+}}
; CHECK-NEXT:    movx @dptr, a
  store i8 %v, ptr addrspace(3) %p
  ret void
}

define void @st16_ptr(ptr addrspace(3) %p, i16 %v) {
; CHECK-LABEL: _st16_ptr:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
  store i16 %v, ptr addrspace(3) %p
  ret void
}

define void @st32_ptr(ptr addrspace(3) %p, i32 %v) {
; CHECK-LABEL: _st32_ptr:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
  store i32 %v, ptr addrspace(3) %p
  ret void
}
