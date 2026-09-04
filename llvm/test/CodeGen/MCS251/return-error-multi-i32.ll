; RUN: sed -n '/^define { i32, i32 }/,/^}/p' %s | not --crash llc -mtriple=mcs251 2>&1 | FileCheck %s --check-prefix=TWO-I32
; RUN: sed -n '/^define { i8, i32 }/,/^}/p' %s | not --crash llc -mtriple=mcs251 2>&1 | FileCheck %s --check-prefix=I8-I32

; Multiple return values must fail before CCState can assign independent values
; to separate candidates in the i32 gatekeeper entry. Otherwise LowerReturn
; would only transfer the first i32 value's four ABI byte lanes.

define { i32, i32 } @two_i32() {
; TWO-I32: LLVM ERROR: minimal MCS251 backend only supports zero or one i8/i16/i32 return value; multi-value returns are not supported
  ret { i32, i32 } { i32 1, i32 2 }
}

define { i8, i32 } @i8_i32() {
; I8-I32: LLVM ERROR: minimal MCS251 backend only supports zero or one i8/i16/i32 return value; multi-value returns are not supported
  ret { i8, i32 } { i8 1, i32 2 }
}
