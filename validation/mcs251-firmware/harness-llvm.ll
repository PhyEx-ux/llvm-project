; harness-llvm.ll - self-hosted judgment harness for the all-LLVM/asm chain
; (Step 4b).  Drop-in replacement for harness-template.c's check/print
; protocol, compilable by llc today.
;
; MULTI-ARG WORKAROUND CONVENTION (backend has no >=2-arg calls yet - OSEG):
; harness_check_uN(expected, got) is split into
;   1. a volatile store of `expected` into a fixed idata cell, and
;   2. a single-argument call _harness_check_uN(got).
; Call-site shape in a test _main.ll:
;   store volatile i8 165, ptr @_harness_expect8     ; expected = 0xA5
;   %g = call i8 @kernel_under_test()
;   call void @_harness_check_u8(i8 %g)
; The cells live in harness-llvm-cells.asm (absolute idata equates,
; provider.asm _p13_mem pattern).  Marker characters and the 'B' banner are
; the test module's own one-line SBUF stores (see selfstart-check/).
;
; BACKEND-GAP-FREE IR SHAPES (probed 2026-09-05, post-e098d07d2 llc):
; no shifts except i32 shl (i8/i16 shifts and i32 lshr/ashr are unselectable)
; -> byte/nibble extraction uses subtract-and-count loops (phi loops, reg-reg
; icmp, sub are all proven).  Parser quirks of this llc fork honoured here:
; no space between inttoptr and '('; every block label must follow a
; terminator (no fall-through into a bare label).
;
; Protocol (byte-identical to harness-template.c):
;   check success:  silent return
;   check failure:  "FAIL expected=0x.. got=0x..\n" then halt (no return)
;   _harness_pass:  "PASS\n" then halt (no return)
;
; All serial output via fixed-address volatile store to SBUF (0x99), the
; T2-proven idiom.  No SDCC asset is required anywhere in the image.

@_harness_expect8  = external global i8
@_harness_expect16 = external global i16
@_harness_expect32 = external global i32

define internal void @hputc(i8 %c) {
  store volatile i8 %c, ptr inttoptr(i32 153 to ptr)  ; SBUF
  ret void
}

define internal void @hnib(i8 %n) {
  %small = icmp ult i8 %n, 10
  br i1 %small, label %dig, label %hex
dig:
  %d = add i8 %n, 48                                  ; '0'
  call void @hputc(i8 %d)
  ret void
hex:
  %h = add i8 %n, 55                                  ; 'A' - 10
  call void @hputc(i8 %h)
  ret void
}

; v / 16 by subtract-and-count (no shift on i8 in this backend).
define internal i8 @div16(i8 %v) {
entry:
  br label %loop
loop:
  %q = phi i8 [ 0, %entry ], [ %q1, %body ]
  %r = phi i8 [ %v, %entry ], [ %r1, %body ]
  %go = icmp uge i8 %r, 16
  br i1 %go, label %body, label %done
body:
  %r1 = sub i8 %r, 16
  %q1 = add i8 %q, 1
  br label %loop
done:
  ret i8 %q
}

define void @_harness_hex8(i8 %v) {
  %hi = call i8 @div16(i8 %v)
  call void @hnib(i8 %hi)
  %lo = and i8 %v, 15
  call void @hnib(i8 %lo)
  ret void
}

define void @_harness_hex16(i16 %v) {
entry:
  br label %loop
loop:
  %hi = phi i16 [ 0, %entry ], [ %hi1, %body ]
  %r  = phi i16 [ %v, %entry ], [ %r1, %body ]
  %go = icmp uge i16 %r, 256
  br i1 %go, label %body, label %done
body:
  %r1 = sub i16 %r, 256
  %hi1 = add i16 %hi, 1
  br label %loop
done:
  %hib = trunc i16 %hi to i8
  call void @_harness_hex8(i8 %hib)
  %lo16 = and i16 %r, 255
  %lob = trunc i16 %lo16 to i8
  call void @_harness_hex8(i8 %lob)
  ret void
}

define void @_harness_hex32(i32 %v) {
entry:
  br label %l3
l3:                                                 ; byte3 = v / 2^24
  %b3 = phi i32 [ 0, %entry ], [ %b3n, %p3 ]
  %r3 = phi i32 [ %v, %entry ], [ %r3n, %p3 ]
  %g3 = icmp uge i32 %r3, 16777216
  br i1 %g3, label %p3, label %d3
p3:
  %r3n = sub i32 %r3, 16777216
  %b3n = add i32 %b3, 1
  br label %l3
d3:
  %b3t = trunc i32 %b3 to i8
  call void @_harness_hex8(i8 %b3t)
  br label %l2
l2:                                                 ; byte2 = rem / 2^16
  %b2 = phi i32 [ 0, %d3 ], [ %b2n, %p2 ]
  %r2 = phi i32 [ %r3, %d3 ], [ %r2n, %p2 ]
  %g2 = icmp uge i32 %r2, 65536
  br i1 %g2, label %p2, label %d2
p2:
  %r2n = sub i32 %r2, 65536
  %b2n = add i32 %b2, 1
  br label %l2
d2:
  %b2t = trunc i32 %b2 to i8
  call void @_harness_hex8(i8 %b2t)
  br label %l1
l1:                                                 ; byte1 = rem / 256
  %b1 = phi i32 [ 0, %d2 ], [ %b1n, %p1 ]
  %r1 = phi i32 [ %r2, %d2 ], [ %r1n, %p1 ]
  %g1 = icmp uge i32 %r1, 256
  br i1 %g1, label %p1, label %d1
p1:
  %r1n = sub i32 %r1, 256
  %b1n = add i32 %b1, 1
  br label %l1
d1:
  %b1t = trunc i32 %b1 to i8
  call void @_harness_hex8(i8 %b1t)
  %b0 = trunc i32 %r1 to i8                         ; remainder is byte0
  call void @_harness_hex8(i8 %b0)
  ret void
}

define internal void @fail_pre() {
  call void @hputc(i8 70)   ; F
  call void @hputc(i8 65)   ; A
  call void @hputc(i8 73)   ; I
  call void @hputc(i8 76)   ; L
  call void @hputc(i8 32)   ; ' '
  call void @hputc(i8 101)  ; e
  call void @hputc(i8 120)  ; x
  call void @hputc(i8 112)  ; p
  call void @hputc(i8 101)  ; e
  call void @hputc(i8 99)   ; c
  call void @hputc(i8 116)  ; t
  call void @hputc(i8 101)  ; e
  call void @hputc(i8 100)  ; d
  call void @hputc(i8 61)   ; =
  call void @hputc(i8 48)   ; 0
  call void @hputc(i8 120)  ; x
  ret void
}

define internal void @fail_mid() {
  call void @hputc(i8 32)   ; ' '
  call void @hputc(i8 103)  ; g
  call void @hputc(i8 111)  ; o
  call void @hputc(i8 116)  ; t
  call void @hputc(i8 61)   ; =
  call void @hputc(i8 48)   ; 0
  call void @hputc(i8 120)  ; x
  ret void
}

define internal void @fail_end() {
  call void @hputc(i8 10)   ; '\n'
  br label %spin            ; (this llc fork's parser requires a terminator
spin:                       ;  before a bare label; no fall-through blocks)
  br label %spin
}

define internal void @fail8(i8 %got) {
  call void @fail_pre()
  %e = load volatile i8, ptr @_harness_expect8
  call void @_harness_hex8(i8 %e)
  call void @fail_mid()
  call void @_harness_hex8(i8 %got)
  call void @fail_end()
  ret void                  ; unreachable (fail_end spins)
}

define internal void @fail16(i16 %got) {
  call void @fail_pre()
  %e = load volatile i16, ptr @_harness_expect16
  call void @_harness_hex16(i16 %e)
  call void @fail_mid()
  call void @_harness_hex16(i16 %got)
  call void @fail_end()
  ret void                  ; unreachable
}

define internal void @fail32(i32 %got) {
  call void @fail_pre()
  %e = load volatile i32, ptr @_harness_expect32
  call void @_harness_hex32(i32 %e)
  call void @fail_mid()
  call void @_harness_hex32(i32 %got)
  call void @fail_end()
  ret void                  ; unreachable
}

define void @_harness_check_u8(i8 %got) {
  %e = load volatile i8, ptr @_harness_expect8
  %ok = icmp eq i8 %e, %got
  br i1 %ok, label %good, label %bad
good:
  ret void
bad:
  call void @fail8(i8 %got)
  ret void                  ; unreachable
}

define void @_harness_check_u16(i16 %got) {
  %e = load volatile i16, ptr @_harness_expect16
  %ok = icmp eq i16 %e, %got
  br i1 %ok, label %good, label %bad
good:
  ret void
bad:
  call void @fail16(i16 %got)
  ret void                  ; unreachable
}

define void @_harness_check_u32(i32 %got) {
  %e = load volatile i32, ptr @_harness_expect32
  %ok = icmp eq i32 %e, %got
  br i1 %ok, label %good, label %bad
good:
  ret void
bad:
  call void @fail32(i32 %got)
  ret void                  ; unreachable
}

define void @_harness_pass() {
  call void @hputc(i8 80)   ; P
  call void @hputc(i8 65)   ; A
  call void @hputc(i8 83)   ; S
  call void @hputc(i8 83)   ; S
  call void @hputc(i8 10)   ; '\n'
  br label %spin
spin:
  br label %spin
}
