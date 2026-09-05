; fail8-main.ll - selfstart-check case 2 (fail phase, u8).
; Expected transcript: BFAIL expected=0xA5 got=0x00\n then halt.
@harness_expect8 = external global i8
declare void @harness_check_u8(i8)

define void @main() {
entry:
  store volatile i8 66, ptr inttoptr(i32 153 to ptr)   ; 'B'
  store volatile i8 165, ptr @harness_expect8         ; expected 0xA5
  call void @harness_check_u8(i8 0)                   ; got 0x00 -> FAIL
  ret void                  ; unreachable: fail path spins
}
