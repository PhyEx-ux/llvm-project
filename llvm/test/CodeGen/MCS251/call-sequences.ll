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

; The signed/remainder libcalls (division design v4 §8.8): sdiv+srem and
; udiv+urem pairs in one function lower as two independent libcalls. The
; srem/urem operand feeds on the first call's result, which fixes the call
; order across schedules (the remaining scheduling freedom -- where the
; MOVADDR32 slot-address materialisation sits, and any copies/loads around
; it -- is preserved: no NOT below forbids MOVADDR32, MOV8rmP or COPY).
; The slot address is materialised by MOVADDR32 on the slot symbol; the
; i16 store reuses that single base for both byte stores (Disp 0 = hi,
; Disp 1 = lo). Every pair of adjacent positive matches below carries a
; NOT set: apart from the explicitly expected stores, calls and CALLSEQ
; boundaries, no ECALL, no MOV8mrP and no extra CALLSEQ edge may appear in
; any checked segment, including the pre-window region and the function
; tail after the last UP.
define i16 @sdiv_srem_pair(i16 %a, i16 %b, i16 %c) {
; MIR-LABEL: name: sdiv_srem_pair
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: %[[AS:[0-9]+]]:{{[a-z0-9]+}} = MOVADDR32 &{{[^ ]*}}divsint_PARM_2
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[AS]], 0,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[AS]], 1,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ECALL &{{_+}}divsint
; MIR-NOT: ECALL
; MIR-NOT: MOV8mrP
; MIR-NOT: ADJCALLSTACKDOWN
; MIR: ADJCALLSTACKUP 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: %[[MS:[0-9]+]]:{{[a-z0-9]+}} = MOVADDR32 &{{[^ ]*}}modsint_PARM_2
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[MS]], 0,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[MS]], 1,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ECALL &{{_+}}modsint
; MIR-NOT: ECALL
; MIR-NOT: MOV8mrP
; MIR-NOT: ADJCALLSTACKDOWN
; MIR: ADJCALLSTACKUP 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
  %q = sdiv i16 %a, %b
  %r = srem i16 %q, %c
  %sum = add i16 %q, %r
  %result = add i16 %sum, %a
  ret i16 %result
}

define i16 @udiv_urem_pair(i16 %a, i16 %b, i16 %c) {
; MIR-LABEL: name: udiv_urem_pair
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: %[[AU:[0-9]+]]:{{[a-z0-9]+}} = MOVADDR32 &{{[^ ]*}}divuint_PARM_2
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[AU]], 0,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[AU]], 1,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ECALL &{{_+}}divuint
; MIR-NOT: ECALL
; MIR-NOT: MOV8mrP
; MIR-NOT: ADJCALLSTACKDOWN
; MIR: ADJCALLSTACKUP 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ADJCALLSTACKDOWN 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: %[[MU:[0-9]+]]:{{[a-z0-9]+}} = MOVADDR32 &{{[^ ]*}}moduint_PARM_2
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[MU]], 0,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: MOV8mrP %[[MU]], 1,
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
; MIR: ECALL &{{_+}}moduint
; MIR-NOT: ECALL
; MIR-NOT: MOV8mrP
; MIR-NOT: ADJCALLSTACKDOWN
; MIR: ADJCALLSTACKUP 0, 0
; MIR-NOT: ECALL
; MIR-NOT: ADJCALLSTACK
; MIR-NOT: MOV8mrP
  %q = udiv i16 %a, %b
  %r = urem i16 %q, %c
  %sum = add i16 %q, %r
  %result = add i16 %sum, %a
  ret i16 %result
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
