; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj %s -o %t.O2.rel

; Independent libcalls begin on the entry chain. CALLSEQ (even with no stack
; arguments) must prevent both static-parameter setups preceding the first
; call. This previously compiled without verifier errors but returned b/b+a/b
; instead of b/c+a/b at -O2. Check stores INSIDE each complete call sequence.
define i32 @div_twice(i32 %a, i32 %b, i32 %c) {
; MIR-LABEL: name: div_twice
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR: MOV8mrP
; MIR: ECALL &{{_+}}divulong
; MIR: ADJCALLSTACKUP 0, 0
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR: MOV8mrP
; MIR: ECALL &{{_+}}divulong
; MIR: ADJCALLSTACKUP 0, 0
  %x = udiv i32 %a, %b
  %y = udiv i32 %b, %c
  %z = add i32 %x, %y
  ret i32 %z
}

; Pure user calls still use the same canonical call lowering. Keep the
; noinline/memory(none) shape covered, not just visibly side-effecting calls.
declare i32 @pair(i32, i32) noinline nounwind memory(none)
define i32 @user_twice(i32 %a, i32 %b, i32 %c) {
; MIR-LABEL: name: user_twice
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR: MOV8mrP
; MIR: ECALL @pair
; MIR: ADJCALLSTACKUP 0, 0
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR: MOV8mrP
; MIR: ECALL @pair
; MIR: ADJCALLSTACKUP 0, 0
  %x = call i32 @pair(i32 %a, i32 %b)
  %y = call i32 @pair(i32 %b, i32 %c)
  %z = add i32 %x, %y
  ret i32 %z
}

; Two independent native products also expose their complete fixed-register
; uses/defs to the scheduler; no hidden A/B state may cross products.
define i8 @mul_twice(i8 %a, i8 %b, i8 %c) {
; MIR-LABEL: name: mul_twice
; MIR: MULAB implicit-def $a, implicit-def $b, implicit-def $psw, implicit $a, implicit $b
; MIR: COPY $a
; MIR: MULAB implicit-def $a, implicit-def $b, implicit-def $psw, implicit $a, implicit $b
; MIR: COPY $a
  %x = mul i8 %a, %b
  %y = mul i8 %b, %c
  %z = add i8 %x, %y
  ret i8 %z
}
