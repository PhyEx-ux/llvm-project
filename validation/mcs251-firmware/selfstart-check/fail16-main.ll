; fail16-main.ll - selfstart-check case 3 (fail phase, u16 width format).
; Expected transcript: BFAIL expected=0x1357 got=0x0000\n then halt.
@harness_expect16 = external global i16
declare void @harness_check_u16(i16)

define void @main() {
entry:
  store volatile i8 66, ptr inttoptr(i32 153 to ptr)   ; 'B'
  store volatile i16 4951, ptr @harness_expect16      ; expected 0x1357
  call void @harness_check_u16(i16 0)                 ; got 0x0000 -> FAIL
  ret void                  ; unreachable
}
