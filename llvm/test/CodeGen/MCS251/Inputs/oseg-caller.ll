source_filename = "oseg-caller.ll"

declare i8 @bytes(i8, i8, i8)
declare i32 @mixed(i16, i8, i32)
declare i32 @wide(i32, i32, i16)
declare i32 @nested(i32, i32, i16)
declare i32 @outer(i32, i32)
declare i32 @oracle(i16, i8, i32)
declare i16 @callback(i16, i16)

define i8 @call_bytes() {
  %r = call i8 @bytes(i8 90, i8 165, i8 3)
  ret i8 %r
}

define i32 @call_mixed() {
  %r = call i32 @mixed(i16 4951, i8 165, i32 2309737967)
  ret i32 %r
}

define i32 @call_wide() {
  %r = call i32 @wide(i32 305419896, i32 2309737967, i16 9320)
  ret i32 %r
}

define i32 @call_nested() {
  %r = call i32 @nested(i32 305419896, i32 2309737967, i16 9320)
  ret i32 %r
}

define i32 @call_outer() {
  %r = call i32 @outer(i32 305419896, i32 16909060)
  ret i32 %r
}

define i32 @call_oracle() {
  %r = call i32 @oracle(i16 4951, i8 165, i32 2309737967)
  ret i32 %r
}

define i16 @remote(i16 %a, i16 %b) noinline {
  %x = call i16 @callback(i16 %a, i16 %b)
  %r = add i16 %x, %b
  ret i16 %r
}

; Single-register and no-argument ABI regressions.
define i32 @single(i32 %a) {
  %r = xor i32 %a, 1431677610
  ret i32 %r
}
define i16 @zero() {
  ret i16 48879
}
