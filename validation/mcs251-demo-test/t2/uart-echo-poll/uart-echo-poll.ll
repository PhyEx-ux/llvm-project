; T2 UART1 receive/echo path, following the measured g_uartrx probe.
; SCON=0x98, SBUF=0x99.  RI is cleared by masking only bit 0; TI is
; cleared by masking only bit 1.  Both polls have finite wraparound timeouts.
target triple = "mcs251-unknown-none"

define i8 @_t2_uart_echo_poll() {
entry:
  ; REN=1.  This also lets QEMU accept the byte piped on stdin.
  store volatile i8 16, ptr inttoptr (i32 152 to ptr)
  br label %ri_poll

ri_poll:
  %ri_count = phi i8 [ 0, %entry ], [ %ri_next, %ri_wait ]
  %scon_ri = load volatile i8, ptr inttoptr (i32 152 to ptr)
  %ri_bit = and i8 %scon_ri, 1
  %ri_seen = icmp ne i8 %ri_bit, 0
  br i1 %ri_seen, label %received, label %ri_check_timeout

ri_check_timeout:
  %ri_next = add i8 %ri_count, 1
  %ri_timeout = icmp eq i8 %ri_next, 0
  br i1 %ri_timeout, label %timeout, label %ri_wait

ri_wait:
  br label %ri_poll

received:
  %rx = load volatile i8, ptr inttoptr (i32 153 to ptr)
  ; Clear only RI and retain all other SCON status bits.
  %scon_clear_ri = and i8 %scon_ri, 254
  store volatile i8 %scon_clear_ri, ptr inttoptr (i32 152 to ptr)
  ; UART1 TX emits the received byte and sets TI in QEMU.
  store volatile i8 %rx, ptr inttoptr (i32 153 to ptr)
  br label %ti_poll

ti_poll:
  %ti_count = phi i8 [ 0, %received ], [ %ti_next, %ti_wait ]
  %scon_ti = load volatile i8, ptr inttoptr (i32 152 to ptr)
  %ti_bit = and i8 %scon_ti, 2
  %ti_seen = icmp ne i8 %ti_bit, 0
  br i1 %ti_seen, label %clear_ti, label %ti_check_timeout

ti_check_timeout:
  %ti_next = add i8 %ti_count, 1
  %ti_timeout = icmp eq i8 %ti_next, 0
  br i1 %ti_timeout, label %timeout, label %ti_wait

ti_wait:
  br label %ti_poll

clear_ti:
  ; Clear only TI, retaining the current RI/REN/mode bits.
  %scon_clear_ti = and i8 %scon_ti, 253
  store volatile i8 %scon_clear_ti, ptr inttoptr (i32 152 to ptr)
  br label %ti_poll_after_clear

ti_poll_after_clear:
  %after_count = phi i8 [ 0, %clear_ti ], [ %after_next, %after_wait ]
  %scon_after = load volatile i8, ptr inttoptr (i32 152 to ptr)
  %after_bit = and i8 %scon_after, 2
  %after_clear = icmp eq i8 %after_bit, 0
  br i1 %after_clear, label %done, label %after_check_timeout

after_check_timeout:
  %after_next = add i8 %after_count, 1
  %after_timeout = icmp eq i8 %after_next, 0
  br i1 %after_timeout, label %timeout, label %after_wait

after_wait:
  br label %ti_poll_after_clear

timeout:
  ret i8 0

done:
  ret i8 %rx
}
