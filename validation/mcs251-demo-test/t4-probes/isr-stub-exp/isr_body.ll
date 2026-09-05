; isr_body.ll - LLVM ISR body for the T4 stub-form experiments.
; Increments CNT (idata 0x31), sets FLAG (idata 0x30), stops T0
; (TCON &= ~0x10, SFR 0x88) so each arming produces exactly one fire,
; then prints 'I' (SBUF 0x99).  Fixed-address volatile pointers only.
; Convention matches d1_tint.asm: FLAG=0x30, CNT=0x31.
; NOTE: this llc fork's IR parser rejects a space between 'inttoptr' and '('.
define void @_isr_body() {
entry:
  %v = load volatile i8, ptr inttoptr(i32 49 to ptr)
  %i = add i8 %v, 1
  store volatile i8 %i, ptr inttoptr(i32 49 to ptr)
  store volatile i8 1, ptr inttoptr(i32 48 to ptr)
  %t = load volatile i8, ptr inttoptr(i32 136 to ptr)
  %t2 = and i8 %t, -17
  store volatile i8 %t2, ptr inttoptr(i32 136 to ptr)
  store volatile i8 73, ptr inttoptr(i32 153 to ptr)
  ret void
}
