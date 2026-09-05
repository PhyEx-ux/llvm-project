; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/sdiv8.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/sdiv8.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/sdiv16.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/sdiv32.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/urem8.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/urem16.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/urem32.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/srem8.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/srem16.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/srem32.ll -o /dev/null 2>&1 | FileCheck %s
; CHECK: LLVM ERROR: MCS251: signed division and remainder are not supported

;--- sdiv8.ll
define i8 @f(i8 %a, i8 %b) {
  %r = sdiv i8 %a, %b
  ret i8 %r
}
;--- sdiv16.ll
define i16 @f(i16 %a, i16 %b) {
  %r = sdiv i16 %a, %b
  ret i16 %r
}
;--- sdiv32.ll
define i32 @f(i32 %a, i32 %b) {
  %r = sdiv i32 %a, %b
  ret i32 %r
}
;--- urem8.ll
define i8 @f(i8 %a, i8 %b) {
  %r = urem i8 %a, %b
  ret i8 %r
}
;--- urem16.ll
define i16 @f(i16 %a, i16 %b) {
  %r = urem i16 %a, %b
  ret i16 %r
}
;--- urem32.ll
define i32 @f(i32 %a, i32 %b) {
  %r = urem i32 %a, %b
  ret i32 %r
}
;--- srem8.ll
define i8 @f(i8 %a, i8 %b) {
  %r = srem i8 %a, %b
  ret i8 %r
}
;--- srem16.ll
define i16 @f(i16 %a, i16 %b) {
  %r = srem i16 %a, %b
  ret i16 %r
}
;--- srem32.ll
define i32 @f(i32 %a, i32 %b) {
  %r = srem i32 %a, %b
  ret i32 %r
}
