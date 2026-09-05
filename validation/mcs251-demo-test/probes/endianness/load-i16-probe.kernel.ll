@_u8g = external global i8
@_u16g = external global i16
define i8 @_read_u8() {
  %v = load i8, ptr @_u8g
  ret i8 %v
}
define i8 @_read_u16_hi() {
  %slot = alloca i16
  %v = load i16, ptr @_u16g
  %a8 = and i16 %v, 256
  %c8 = icmp ne i16 %a8, 0
  store volatile i16 0, ptr %slot
  %l8 = load volatile i16, ptr %slot
  %o8 = or i16 %l8, 1
  %y8 = select i1 %c8, i16 %o8, i16 %l8
  %a9 = and i16 %v, 512
  %c9 = icmp ne i16 %a9, 0
  store volatile i16 %y8, ptr %slot
  %l9 = load volatile i16, ptr %slot
  %o9 = or i16 %l9, 2
  %y9 = select i1 %c9, i16 %o9, i16 %l9
  %a10 = and i16 %v, 1024
  %c10 = icmp ne i16 %a10, 0
  store volatile i16 %y9, ptr %slot
  %l10 = load volatile i16, ptr %slot
  %o10 = or i16 %l10, 4
  %y10 = select i1 %c10, i16 %o10, i16 %l10
  %a11 = and i16 %v, 2048
  %c11 = icmp ne i16 %a11, 0
  store volatile i16 %y10, ptr %slot
  %l11 = load volatile i16, ptr %slot
  %o11 = or i16 %l11, 8
  %y11 = select i1 %c11, i16 %o11, i16 %l11
  %a12 = and i16 %v, 4096
  %c12 = icmp ne i16 %a12, 0
  store volatile i16 %y11, ptr %slot
  %l12 = load volatile i16, ptr %slot
  %o12 = or i16 %l12, 16
  %y12 = select i1 %c12, i16 %o12, i16 %l12
  %a13 = and i16 %v, 8192
  %c13 = icmp ne i16 %a13, 0
  store volatile i16 %y12, ptr %slot
  %l13 = load volatile i16, ptr %slot
  %o13 = or i16 %l13, 32
  %y13 = select i1 %c13, i16 %o13, i16 %l13
  %a14 = and i16 %v, 16384
  %c14 = icmp ne i16 %a14, 0
  store volatile i16 %y13, ptr %slot
  %l14 = load volatile i16, ptr %slot
  %o14 = or i16 %l14, 64
  %y14 = select i1 %c14, i16 %o14, i16 %l14
  %a15 = and i16 %v, 32768
  %c15 = icmp ne i16 %a15, 0
  store volatile i16 %y14, ptr %slot
  %l15 = load volatile i16, ptr %slot
  %o15 = or i16 %l15, 128
  %s = select i1 %c15, i16 %o15, i16 %l15
  %t = trunc i16 %s to i8
  ret i8 %t
}
define void @_write_u8_ff() {
  store i8 -1, ptr @_u8g
  ret void
}
define void @_write_u16_00ff() {
  store i16 255, ptr @_u16g
  ret void
}