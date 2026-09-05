; fail16-main.ll - selfstart-check case 3 (fail phase, u16 width format).
; Expected transcript: BFAIL expected=0x1357 got=0x0000\n then halt.
@_harness_expect16 = external global i16
declare void @_harness_check_u16(i16)

define void @_main() {
entry:
  store volatile i8 66, ptr inttoptr(i32 153 to ptr)   ; 'B'
  store volatile i16 4951, ptr @_harness_expect16      ; expected 0x1357
  call void @_harness_check_u16(i16 0)                 ; got 0x0000 -> FAIL
  ret void                  ; unreachable
}
