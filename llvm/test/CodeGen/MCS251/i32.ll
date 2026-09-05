; RUN: llc -mtriple=mcs251 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O0 < %s | FileCheck %s --check-prefix=O0

; Phase 10: i32 values use DR registers. The ABI passes and returns the bytes
; least-significant byte first (DPL), DPH, B, then most-significant byte (A).
; DR and WR register lane order is the converse in printed register numbers,
; with the high lane first.

define i32 @c() {
; CHECK-LABEL: _c:
; CHECK:         mov [[D:dr[0-9]+]], #0x5678
; CHECK-NEXT:    movh [[D]], #0x1234
; CHECK:         mov dpl, r3
; CHECK-NEXT:    mov dph, r2
; CHECK-NEXT:    mov b, r1
; CHECK-NEXT:    mov a, r0
; CHECK-NEXT:    eret
; O0-LABEL: _c:
; Constant return through splitI32ToBytes: the four ABI bytes are extracted
; from the materialised DR down to i8 lanes. Unlike the WR-lane logical-op
; shape (see and_const below), this byte-lane form is measured correct at
; -O0 -- the assertion pins the four distinct loads in ABI order so a FastRA
; lane-merge regression cannot pass silently.
; O0:         mov [[D0:dr[0-9]+]], #0x5678
; O0-NEXT:    movh [[D0]], #0x1234
; O0:         mov dpl, [[P1:r[0-9]+]]
; O0-NEXT:    mov dph, [[P2:r[0-9]+]]
; O0-NEXT:    mov b, [[P3:r[0-9]+]]
; O0-NEXT:    mov a, [[P4:r[0-9]+]]
; O0-NEXT:    eret
  ret i32 305419896
}

define i32 @id(i32 %x) {
; CHECK-LABEL: _id:
; CHECK:         mov r{{[0-9]+}}, a
; CHECK:         mov r{{[0-9]+}}, b
; CHECK:         mov r{{[0-9]+}}, dph
; CHECK:         mov r{{[0-9]+}}, dpl
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         mov dph, r{{[0-9]+}}
; CHECK:         mov b, r{{[0-9]+}}
; CHECK:         mov a, r{{[0-9]+}}
; CHECK:         eret
  ret i32 %x
}

define i32 @f(i32 %x) {
  ret i32 %x
}

define i32 @call_i32() {
; CHECK-LABEL: _call_i32:
; CHECK:         mov dr{{[0-9]+}}, #0x5678
; CHECK-NEXT:    movh dr{{[0-9]+}}, #0x1234
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK-NEXT:    mov dph, r{{[0-9]+}}
; CHECK-NEXT:    mov b, r{{[0-9]+}}
; CHECK-NEXT:    mov a, r{{[0-9]+}}
; CHECK-NEXT:    ecall _f
; CHECK:         mov r3, dpl
; CHECK-NEXT:    mov r2, dph
; CHECK-NEXT:    mov r1, b
; CHECK-NEXT:    mov r0, a
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0-LABEL: _call_i32:
; Constant i32 argument loading (splitI32ToBytes) measured correct at -O0:
; four ABI byte stores before the ecall, four byte reads after it.
; O0:         mov dpl, [[Q1:r[0-9]+]]
; O0-NEXT:    mov dph, [[Q2:r[0-9]+]]
; O0-NEXT:    mov b, [[Q3:r[0-9]+]]
; O0-NEXT:    mov a, [[Q4:r[0-9]+]]
; O0-NEXT:    ecall _f
; O0:         mov {{r[0-9]+}}, dpl
; O0:         mov {{r[0-9]+}}, dph
; O0:         mov {{r[0-9]+}}, b
; O0:         mov {{r[0-9]+}}, a
  %r = call i32 @f(i32 305419896)
  %inc = add i32 %r, 1
  ret i32 %inc
}

define i32 @add1(i32 %x) {
; CHECK-LABEL: _add1:
; CHECK:         add dr{{[0-9]+}}, dr{{[0-9]+}}
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @neg1(i32 %x) {
; CHECK-LABEL: _neg1:
; CHECK:         sub dr{{[0-9]+}}, dr{{[0-9]+}}
  %r = add i32 %x, -1
  ret i32 %r
}

define i32 @sub1(i32 %x) {
; CHECK-LABEL: _sub1:
; CHECK:         sub dr{{[0-9]+}}, dr{{[0-9]+}}
  %r = sub i32 %x, 1
  ret i32 %r
}

; The constant lanes deliberately have distinct MOV16ri definitions. This
; avoids FastRA coalescing two DR subregister copies into the same high lane at
; -O0; each logical operation must still execute once per WR lane.
define i32 @and_self(i32 %x) {
; CHECK-LABEL: _and_self:
; CHECK-COUNT-2: anl wr{{[0-9]+}}, wr{{[0-9]+}}
  %y = freeze i32 %x
  %r = and i32 %x, %y
  ret i32 %r
}

define i32 @and_const(i32 %x) {
; CHECK-LABEL: _and_const:
; CHECK-COUNT-2: anl wr{{[0-9]+}}, wr{{[0-9]+}}
; O0-LABEL: _and_const:
; O0:         mov [[HI:wr[0-9]+]], #0x0000
; O0-NEXT:    anl {{wr[0-9]+}}, [[HI]]
; O0:         mov [[LO:wr[0-9]+]], #0xffff
; O0-NEXT:    anl {{wr[0-9]+}}, [[LO]]
  %r = and i32 %x, 65535
  ret i32 %r
}

define i32 @or_const(i32 %x) {
; CHECK-LABEL: _or_const:
; CHECK-COUNT-2: orl wr{{[0-9]+}}, wr{{[0-9]+}}
; O0-LABEL: _or_const:
; O0:         mov [[HI:wr[0-9]+]], #0x0000
; O0-NEXT:    orl {{wr[0-9]+}}, [[HI]]
; O0:         mov [[LO:wr[0-9]+]], #0xffff
; O0-NEXT:    orl {{wr[0-9]+}}, [[LO]]
  %r = or i32 %x, 65535
  ret i32 %r
}

define i32 @xor_const(i32 %x) {
; CHECK-LABEL: _xor_const:
; CHECK-COUNT-2: xrl wr{{[0-9]+}}, wr{{[0-9]+}}
; O0-LABEL: _xor_const:
; O0:         mov [[HI:wr[0-9]+]], #0x0000
; O0-NEXT:    xrl {{wr[0-9]+}}, [[HI]]
; O0:         mov [[LO:wr[0-9]+]], #0xffff
; O0-NEXT:    xrl {{wr[0-9]+}}, [[LO]]
  %r = xor i32 %x, 65535
  ret i32 %r
}
