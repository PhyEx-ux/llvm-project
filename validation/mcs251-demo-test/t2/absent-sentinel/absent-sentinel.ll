; T2 negative sentinel for QEMU's deliberately absent peripherals.
; REPORT.md establishes that these SFR writes are discarded and reads return 0.
; Addresses come from STC32G144K246.h: S2CON=9A, S2BUF=9B,
; ADC_CONTR=BC, ADC_RES=BD, ADC_RESL=BE, WDT_CONTR=C1.
target triple = "mcs251-unknown-none"

define i8 @_t2_absent_s2con() {
entry:
  store volatile i8 165, ptr inttoptr (i32 154 to ptr)
  %v = load volatile i8, ptr inttoptr (i32 154 to ptr)
  ret i8 %v
}

define i8 @_t2_absent_s2buf() {
entry:
  store volatile i8 90, ptr inttoptr (i32 155 to ptr)
  %v = load volatile i8, ptr inttoptr (i32 155 to ptr)
  ret i8 %v
}

define i8 @_t2_absent_adc_contr() {
entry:
  store volatile i8 128, ptr inttoptr (i32 188 to ptr)
  %v = load volatile i8, ptr inttoptr (i32 188 to ptr)
  ret i8 %v
}

define i8 @_t2_absent_adc_result() {
entry:
  store volatile i8 90, ptr inttoptr (i32 189 to ptr)
  %v = load volatile i8, ptr inttoptr (i32 189 to ptr)
  ret i8 %v
}

define i8 @_t2_absent_adc_resl() {
entry:
  store volatile i8 165, ptr inttoptr (i32 190 to ptr)
  %v = load volatile i8, ptr inttoptr (i32 190 to ptr)
  ret i8 %v
}

define i8 @_t2_absent_wdt() {
entry:
  store volatile i8 52, ptr inttoptr (i32 193 to ptr)
  %v = load volatile i8, ptr inttoptr (i32 193 to ptr)
  ret i8 %v
}
