; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
;
; X2-1 adversarial: RUNTIME canonical 24-bit XDATA pointers.  The phase-1
; lowering contributed only the pointer's low 16 bits (an `add wr,#0x0000`
; for +65536 -- the silent bank drop Alice's runtime.s replay caught); the
; X2-1 sequence now folds offsets in full 32-bit arithmetic (carry lands in
; the bank byte) and re-points DPXL from the post-fold bits [23:16] before
; every movx.

; Alice's runtime.s replay shape: p + 65536 must reach 01:p[15:0], not wrap
; back onto p.  The +0x10000 materialises as a full-width DR constant and a
; 32-bit add; the bank byte is the post-add DR lane.
define i8 @runtime(ptr addrspace(3) %p) {
; CHECK-LABEL: _runtime:
; CHECK:         mov dr[[C:[0-9]+]], #0x0000
; CHECK-NEXT:    movh dr[[C]], #0x0001
; CHECK:         add dr[[P:[0-9]+]], dr[[C]]
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) %p, i32 65536
  %v = load volatile i8, ptr addrspace(3) %q
  ret i8 %v
}

; A register+register index: the 32-bit add keeps the carry into the bank
; byte (an i16 index add would silently alias i and i+64K).
declare void @take(ptr addrspace(3))
define i8 @ld_index(ptr addrspace(3) %p, i32 %i) {
; CHECK-LABEL: _ld_index:
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) %p, i32 %i
  %v = load volatile i8, ptr addrspace(3) %q
  ret i8 %v
}

; i32 pointer high-byte pollution: garbage in bits [31:24] must not reach
; DPXL -- the bank byte is exactly bits [23:16] of the canonical container.
define i8 @ld_polluted(ptr addrspace(3) %p) {
; CHECK-LABEL: _ld_polluted:
; the OR materialises the pollution mask (0x15000000), but the byte written
; to 0x84 is the pointer's bank lane, not the polluted top byte.
; CHECK:         orl wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %pi = ptrtoint ptr addrspace(3) %p to i32
  %po = or i32 %pi, 352321536
  %q = inttoptr i32 %po to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %q
  ret i8 %v
}

; Two independent volatile loads in one block: each access carries its own
; DPXL re-point (Defs=[DPXL]/Uses=[DPXL] make the region switch a scheduling
; dependence), so no scheduler interleaving can run a movx behind the other
; chain's bank byte.
define i8 @ld_two_ptrs(ptr addrspace(3) %p, ptr addrspace(3) %q) {
; CHECK-LABEL: _ld_two_ptrs:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
  %v = load volatile i8, ptr addrspace(3) %p
  %w = load volatile i8, ptr addrspace(3) %q
  %x = xor i8 %v, %w
  ret i8 %x
}

; A constant offset that pushes the window across the bank boundary at
; runtime: bank byte comes from the post-add DR lane (p + 0xffff with p in
; bank 01h must land in bank 02h, not wrap within 01h).
define i8 @ld_cross(ptr addrspace(3) %p) {
; CHECK-LABEL: _ld_cross:
; CHECK:         mov dr[[C2:[0-9]+]], #0xffff
; CHECK:         add dr{{[0-9]+}}, dr[[C2]]
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  %q = getelementptr i8, ptr addrspace(3) %p, i32 65535
  %v = load volatile i8, ptr addrspace(3) %q
  ret i8 %v
}

; Runtime store: the write half of the same discipline.
define void @st_bank(ptr addrspace(3) %p, i8 %v) {
; CHECK-LABEL: _st_bank:
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
  %q = getelementptr i8, ptr addrspace(3) %p, i32 131072
  store volatile i8 %v, ptr addrspace(3) %q
  ret void
}
