; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
;
; X2-1 adversarial: CONSTANT canonical 24-bit XDATA addresses.  The phase-1
; window rejection is gone (it silently contradicted the frozen AS3 contract,
; DESIGN.md B.2: `__xdata` 32/8 keeps the complete 24-bit effective address):
; every constant folds into a DPXL bank byte (address bits [23:16]) plus a
; 16-bit MOVX @DPTR window offset, emitted even for bank 01h -- the
; self-healing sequence never assumes the DPXL reset value (01h) survives.
;
; Boundaries pinned here: bank 00h / bank 01h / bank 02h / bank FFh, the
; window top 0xffff, an i16 object straddling the 00h/01h bank boundary, and
; the 24-bit top 0xffffff.

; Constant inside the classic 64K window: bank 00h, window 0x1234.
define i8 @ld_bank00() {
; CHECK-LABEL: _ld_bank00:
; CHECK:         mov wr{{[0-9]+}}, #0x1234
; CHECK:         mov r{{[0-9]+}}, #0x00
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %p = inttoptr i32 4660 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

; 0x11234: bank 01h -- the DPXL reset value is still written, not assumed.
define i8 @ld_bank01() {
; CHECK-LABEL: _ld_bank01:
; CHECK:         mov wr{{[0-9]+}}, #0x1234
; CHECK:         mov r{{[0-9]+}}, #0x01
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %p = inttoptr i32 70196 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

; 0x21234: bank 02h.
define i8 @ld_bank02() {
; CHECK-LABEL: _ld_bank02:
; CHECK:         mov wr{{[0-9]+}}, #0x1234
; CHECK:         mov r{{[0-9]+}}, #0x02
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %p = inttoptr i32 135732 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

; 0xff0000: bank FFh boundary.
define i8 @ld_bankff() {
; CHECK-LABEL: _ld_bankff:
; CHECK:         mov wr{{[0-9]+}}, #0x0000
; CHECK:         mov r{{[0-9]+}}, #0xff
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %p = inttoptr i32 16711680 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

; Window-top boundary 0x00ffff: bank 00h, window 0xffff.
define i8 @ld_window_top() {
; CHECK-LABEL: _ld_window_top:
; CHECK:         mov wr{{[0-9]+}}, #0xffff
; CHECK:         mov r{{[0-9]+}}, #0x00
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %p = inttoptr i32 65535 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

; An i16 object at 0x00ffff straddles the bank boundary: byte 0 (the HIGH
; byte of the big-endian object) sits at bank 00h window 0xffff, byte 1 wraps
; into bank 01h window 0x0000.  Each byte gets its own folded bank byte, so
; the sequence reads both halves of the object, not a wrapped same-bank
; address.
define i16 @ld_straddle() {
; CHECK-LABEL: _ld_straddle:
; CHECK:         mov wr{{[0-9]+}}, #0xffff
; CHECK:         mov r[[B0:[0-9]+]], #0x00
; CHECK-NEXT:    mov 0x84, r[[B0]]
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov wr{{[0-9]+}}, #0x0000
; CHECK:         mov r[[B1:[0-9]+]], #0x01
; CHECK-NEXT:    mov 0x84, r[[B1]]
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK-NEXT:    mov dph, r{{[0-9]+}}
  %p = inttoptr i32 65535 to ptr addrspace(3)
  %v = load volatile i16, ptr addrspace(3) %p
  ret i16 %v
}

; 24-bit top 0xffffff (bank FFh window 0xffff); the i16 tail wraps mod 2^32
; into bank 00h window 0x0000 -- the i32 container's own wrap, both bytes
; still explicitly banked.
define i16 @ld_top_straddle() {
; CHECK-LABEL: _ld_top_straddle:
; CHECK:         mov wr{{[0-9]+}}, #0xffff
; CHECK:         mov r[[T0:[0-9]+]], #0xff
; CHECK-NEXT:    mov 0x84, r[[T0]]
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov wr{{[0-9]+}}, #0x0000
; CHECK:         mov r[[T1:[0-9]+]], #0x00
; CHECK-NEXT:    mov 0x84, r[[T1]]
; CHECK:         movx a, @dptr
  %p = inttoptr i32 16777215 to ptr addrspace(3)
  %v = load volatile i16, ptr addrspace(3) %p
  ret i16 %v
}

; Constant bank store: same folded pair on the write side.
define void @st_bank01() {
; CHECK-LABEL: _st_bank01:
; CHECK:         mov wr{{[0-9]+}}, #0x1234
; CHECK:         mov r{{[0-9]+}}, #0x01
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
  %p = inttoptr i32 70196 to ptr addrspace(3)
  store volatile i8 7, ptr addrspace(3) %p
  ret void
}

; Two constant accesses to DIFFERENT banks in one block: each access re-points
; DPXL to its own bank before its movx (no cross-access carry-over).
define i8 @ld_two_banks() {
; CHECK-LABEL: _ld_two_banks:
; CHECK:         mov r{{[0-9]+}}, #0x01
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov r{{[0-9]+}}, #0x02
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
  %p1 = inttoptr i32 70196 to ptr addrspace(3)
  %p2 = inttoptr i32 135732 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p1
  %w = load volatile i8, ptr addrspace(3) %p2
  %x = xor i8 %v, %w
  ret i8 %x
}
