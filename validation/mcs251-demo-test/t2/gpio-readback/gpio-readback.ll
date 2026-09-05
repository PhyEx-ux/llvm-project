; T2 GPIO mode/output/readback test, based on the measured b_gpio probe.
; P0 input mode (M1=FF,M0=00) samples the undriven input as FF even when the
; output latch is written to 00. Push-pull (M1=00,M0=FF) reads the latch.
target triple = "mcs251-unknown-none"

define i8 @_t2_gpio_input() {
entry:
  store volatile i8 255, ptr inttoptr (i32 147 to ptr) ; P0M1=FF input
  store volatile i8 0, ptr inttoptr (i32 148 to ptr) ; P0M0=00 input
  store volatile i8 0, ptr inttoptr (i32 128 to ptr) ; P0 latch low
  %v = load volatile i8, ptr inttoptr (i32 128 to ptr)
  ret i8 %v
}

define i8 @_t2_gpio_p0_pushpull() {
entry:
  store volatile i8 0, ptr inttoptr (i32 147 to ptr) ; P0M1=00
  store volatile i8 255, ptr inttoptr (i32 148 to ptr) ; P0M0=FF push-pull
  store volatile i8 90, ptr inttoptr (i32 128 to ptr) ; P0=5A
  %v = load volatile i8, ptr inttoptr (i32 128 to ptr)
  ret i8 %v
}

define i8 @_t2_gpio_p1_pushpull() {
entry:
  store volatile i8 0, ptr inttoptr (i32 145 to ptr) ; P1M1=00
  store volatile i8 255, ptr inttoptr (i32 146 to ptr) ; P1M0=FF push-pull
  store volatile i8 195, ptr inttoptr (i32 144 to ptr) ; P1=C3
  %v = load volatile i8, ptr inttoptr (i32 144 to ptr)
  ret i8 %v
}
