; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/sdiv8.ll -o - | FileCheck %s --check-prefixes=SDIV8
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/sdiv8.ll -o - | FileCheck %s --check-prefixes=SDIV8
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/sdiv16.ll -o - | FileCheck %s --check-prefixes=SDIV16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/sdiv16.ll -o - | FileCheck %s --check-prefixes=SDIV16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/sdiv32.ll -o - | FileCheck %s --check-prefixes=SDIV32
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/sdiv32.ll -o - | FileCheck %s --check-prefixes=SDIV32
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/urem8.ll -o - | FileCheck %s --check-prefixes=UREM8
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/urem8.ll -o - | FileCheck %s --check-prefixes=UREM8
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/urem16.ll -o - | FileCheck %s --check-prefixes=UREM16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/urem16.ll -o - | FileCheck %s --check-prefixes=UREM16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/urem32.ll -o - | FileCheck %s --check-prefixes=UREM32
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/urem32.ll -o - | FileCheck %s --check-prefixes=UREM32
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/srem8.ll -o - | FileCheck %s --check-prefixes=SREM8
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/srem8.ll -o - | FileCheck %s --check-prefixes=SREM8
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/srem16.ll -o - | FileCheck %s --check-prefixes=SREM16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/srem16.ll -o - | FileCheck %s --check-prefixes=SREM16
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/srem32.ll -o - | FileCheck %s --check-prefixes=SREM32
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/srem32.ll -o - | FileCheck %s --check-prefixes=SREM32
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %t/sdiv32.ll -o %t.sdiv32.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %t/sdiv32.ll -o %t.sdiv32.O2.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %t/srem32.ll -o %t.srem32.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %t/srem32.ll -o %t.srem32.O2.rel

; Former divrem-errors.ll (9 fragments / 10 negative RUNs): every
; SDIV/SREM/UREM rejection is now a libcall lowering (division design v4
; §7.5/§8.1; SPEC 2026-09-07 §1.1). All ten negative RUNs flipped positive;
; O0/O2 coverage completed to 18 positive RUNs. Libcall shape follows
; udiv.ll: `ecall __<fn>` with the second argument in the big-endian
; `__<fn>_PARM_2` static slot, exactly two underscores (the target's global
; prefix applied once to the SDCC-compatible C-level name).

; The i8 fragments assert the Promote-to-i16 path: no i8 helper exists
; (SDCC promotes char arithmetic too), so i8 reuses the i16 symbols. The
; promotion is the operation legalizer's PromoteNode (sign-extend for
; SDIV/SREM, zero-extend for UDIV/UREM, truncate the result), so these
; CHECKs only pin the resulting i16 libcall shape.

;--- sdiv8.ll
define i8 @f(i8 %a, i8 %b) {
; SDIV8-LABEL: _f:
; SDIV8: __divsint_PARM_2
; SDIV8: ecall __divsint
; SDIV8: eret
  %r = sdiv i8 %a, %b
  ret i8 %r
}
;--- sdiv16.ll
define i16 @f(i16 %a, i16 %b) {
; SDIV16-LABEL: _f:
; SDIV16: __divsint_PARM_2
; SDIV16: ecall __divsint
; SDIV16: eret
  %r = sdiv i16 %a, %b
  ret i16 %r
}
;--- sdiv32.ll
define i32 @f(i32 %a, i32 %b) {
; SDIV32-LABEL: _f:
; SDIV32: __divslong_PARM_2
; SDIV32: ecall __divslong
; SDIV32: eret
  %r = sdiv i32 %a, %b
  ret i32 %r
}
;--- urem8.ll
define i8 @f(i8 %a, i8 %b) {
; UREM8-LABEL: _f:
; UREM8: __moduint_PARM_2
; UREM8: ecall __moduint
; UREM8: eret
  %r = urem i8 %a, %b
  ret i8 %r
}
;--- urem16.ll
define i16 @f(i16 %a, i16 %b) {
; UREM16-LABEL: _f:
; UREM16: __moduint_PARM_2
; UREM16: ecall __moduint
; UREM16: eret
  %r = urem i16 %a, %b
  ret i16 %r
}
;--- urem32.ll
define i32 @f(i32 %a, i32 %b) {
; UREM32-LABEL: _f:
; UREM32: __modulong_PARM_2
; UREM32: ecall __modulong
; UREM32: eret
  %r = urem i32 %a, %b
  ret i32 %r
}
;--- srem8.ll
define i8 @f(i8 %a, i8 %b) {
; SREM8-LABEL: _f:
; SREM8: __modsint_PARM_2
; SREM8: ecall __modsint
; SREM8: eret
  %r = srem i8 %a, %b
  ret i8 %r
}
;--- srem16.ll
define i16 @f(i16 %a, i16 %b) {
; SREM16-LABEL: _f:
; SREM16: __modsint_PARM_2
; SREM16: ecall __modsint
; SREM16: eret
  %r = srem i16 %a, %b
  ret i16 %r
}
;--- srem32.ll
define i32 @f(i32 %a, i32 %b) {
; SREM32-LABEL: _f:
; SREM32: __modslong_PARM_2
; SREM32: ecall __modslong
; SREM32: eret
  %r = srem i32 %a, %b
  ret i32 %r
}
