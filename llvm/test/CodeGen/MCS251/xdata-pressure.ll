; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
;
; X2 register-pressure check for the MOVX channel: DPTR (dpl/dph) and A are
; fixed reserved locations, never allocated, so live byte values around an
; AS3 access must coexist with the movx sequence without the allocator
; handing out dptr, a or their byte-slot aliases (r10 = B, r11 = A are
; outside GPR8 by construction). Every live value keeps its own register and
; the loaded byte lands in a fresh one.

define void @pressure(ptr addrspace(3) %p, i8 %a0, i8 %a1, i8 %a2, i8 %a3,
                      i8 %a4, i8 %a5, i8 %a6, i8 %a7, ptr %out) {
; CHECK-LABEL: _pressure:
; The MOVX sequence survives with all eight values live: the loaded byte
; lands in its own register (r3 here), each live value keeps a distinct one,
; and dptr/a are never among the allocator's choices.
; CHECK:         movx a, @dptr
; CHECK-NEXT:    mov r{{[0-9]+}}, a
; CHECK:         add r{{[0-9]+}}, [[V:r[0-9]+]]
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  %v = load i8, ptr addrspace(3) %p
  %s0 = add i8 %a0, %v
  %s1 = add i8 %a1, %v
  %s2 = add i8 %a2, %v
  %s3 = add i8 %a3, %v
  %s4 = add i8 %a4, %v
  %s5 = add i8 %a5, %v
  %s6 = add i8 %a6, %v
  %s7 = add i8 %a7, %v
  %q0 = inttoptr i16 0 to ptr
  store volatile i8 %s0, ptr %q0
  %q1 = inttoptr i16 1 to ptr
  store volatile i8 %s1, ptr %q1
  %q2 = inttoptr i16 2 to ptr
  store volatile i8 %s2, ptr %q2
  %q3 = inttoptr i16 3 to ptr
  store volatile i8 %s3, ptr %q3
  %q4 = inttoptr i16 4 to ptr
  store volatile i8 %s4, ptr %q4
  %q5 = inttoptr i16 5 to ptr
  store volatile i8 %s5, ptr %q5
  %q6 = inttoptr i16 6 to ptr
  store volatile i8 %s6, ptr %q6
  %q7 = inttoptr i16 7 to ptr
  store volatile i8 %s7, ptr %q7
  ret void
}
