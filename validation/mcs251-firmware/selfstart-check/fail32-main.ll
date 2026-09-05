; fail32-main.ll - selfstart-check case 4 (fail phase, u32 width format).
; Expected transcript: BFAIL expected=0x12345678 got=0x00000000\n then halt.
@_harness_expect32 = external global i32
declare void @_harness_check_u32(i32)

define void @_main() {
entry:
  store volatile i8 66, ptr inttoptr(i32 153 to ptr)   ; 'B'
  store volatile i32 305419896, ptr @_harness_expect32 ; expected 0x12345678
  call void @_harness_check_u32(i32 0)                 ; got 0 -> FAIL
  ret void                  ; unreachable
}
