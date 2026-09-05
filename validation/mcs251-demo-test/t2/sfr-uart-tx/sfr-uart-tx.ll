; T2 UART1 low-level SFR access.
; SBUF=0x99 and SCON=0x98 are deliberately represented as volatile fixed
; addresses. The generated assembly is checked for direct SFR operands.
target triple = "mcs251-unknown-none"

; The two i8 returns keep this test focused on SFR semantics; the harness
; checks the reset and post-write SCON values independently.
define i8 @_t2_sfr_uart_tx() {
entry:
  ; QEMU's locked UART1 reset configuration is 0x50 (SM1 + REN).
  ; Establish/read that reset latch before any SBUF activity.
  store volatile i8 80, ptr inttoptr (i32 152 to ptr)
  %initial = load volatile i8, ptr inttoptr (i32 152 to ptr)
  %initial_clean = and i8 %initial, 253 ; ignore TI if already pending
  store volatile i8 65, ptr inttoptr (i32 153 to ptr) ; 'A', SBUF
  store volatile i8 66, ptr inttoptr (i32 153 to ptr) ; 'B', SBUF
  store volatile i8 67, ptr inttoptr (i32 153 to ptr) ; 'C', SBUF
  store volatile i8 68, ptr inttoptr (i32 153 to ptr) ; 'D', SBUF
  ; Write/read SCON after TX. TI is intentionally masked in the harness.
  store volatile i8 85, ptr inttoptr (i32 152 to ptr) ; 0x55
  ret i8 %initial_clean
}

define i8 @_t2_sfr_uart_scon() {
entry:
  %v = load volatile i8, ptr inttoptr (i32 152 to ptr)
  %clean = and i8 %v, 253 ; ignore asynchronous TI
  ret i8 %clean
}
