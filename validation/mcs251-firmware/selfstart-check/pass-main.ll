; pass-main.ll - selfstart-check case 1 (pass phase).
; B banner, three successful checks (u8/u16/u32) with markers r/w/d,
; then _harness_pass.  Expected transcript: BrwdPASS\n
@harness_expect8  = external global i8
@harness_expect16 = external global i16
@harness_expect32 = external global i32
declare void @harness_check_u8(i8)
declare void @harness_check_u16(i16)
declare void @harness_check_u32(i32)
declare void @harness_pass()

define internal i8 @produce8() {
  %r = add i8 100, 65            ; 0xA5 through a real call+ALU path
  ret i8 %r
}
define internal i16 @produce16() {
  %r = add i16 4096, 855         ; 0x1357 = 4951
  ret i16 %r
}
define internal i32 @produce32() {
  %r = add i32 305419000, 896    ; 0x12345678 = 305419896
  ret i32 %r
}

define void @main() {
entry:
  store volatile i8 66, ptr inttoptr(i32 153 to ptr)   ; 'B'
  store volatile i8 114, ptr inttoptr(i32 153 to ptr)  ; 'r'
  store volatile i8 165, ptr @harness_expect8         ; expected 0xA5
  %g8 = call i8 @produce8()
  call void @harness_check_u8(i8 %g8)
  store volatile i8 119, ptr inttoptr(i32 153 to ptr)  ; 'w'
  store volatile i16 4951, ptr @harness_expect16      ; expected 0x1357
  %g16 = call i16 @produce16()
  call void @harness_check_u16(i16 %g16)
  store volatile i8 100, ptr inttoptr(i32 153 to ptr)  ; 'd'
  store volatile i32 305419896, ptr @harness_expect32 ; expected 0x12345678
  %g32 = call i32 @produce32()
  call void @harness_check_u32(i32 %g32)
  call void @harness_pass()
  ret void                  ; unreachable: _harness_pass spins
}
