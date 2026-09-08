; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 < %s | FileCheck %s
;
; _BitInt(N) N<=32 backend probe (C23): clang lowers _BitInt(N) to iN, and
; the legalizer promotes the odd widths through the existing i16/i32 paths:
;   i12 -> i16 register class, i24 -> i32, with explicit truncation masks.
; Mask semantics this file pins (C23 wrap-around at the _BitInt width):
;   mul i12 promotes to a native wr multiply, then the result is masked back
;   to 12 bits (and 0x0fff) -- (4000*3) mod 4096 == 3808, not 12000.
;   sdiv i24 sign-extends both operands to i32 and reuses the __divslong
;   libcall; sext i24 -> i32 expands through the shl/sra in-register form.
; Feeding values are derived from legal ABI arguments (i16/i32) via trunc so
; no iN function argument ever crosses the C ABI whitelist
; (args must stay unsplit i8/i16/i32 scalars).

define i32 @mul12(i16 noundef zeroext %a, i16 noundef zeroext %b) {
; CHECK-LABEL: _mul12:
; CHECK: mul wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK: mov wr{{[0-9]+}}, #0x0fff
; CHECK-NEXT: anl wr{{[0-9]+}}, wr{{[0-9]+}}
  %ta = trunc i16 %a to i12
  %tb = trunc i16 %b to i12
  %m = mul i12 %ta, %tb
  %z = zext i12 %m to i32
  ret i32 %z
}

define i32 @div24(i32 noundef %a, i32 noundef %b) {
; CHECK-LABEL: _div24:
; CHECK: __divslong_PARM_2
; CHECK: ecall __divslong
; CHECK: eret
  %ta = trunc i32 %a to i24
  %tb = trunc i32 %b to i24
  %d = sdiv i24 %ta, %tb
  %z = sext i24 %d to i32
  ret i32 %z
}

define i32 @shift24(i32 noundef %a) {
; CHECK-LABEL: _shift24:
; CHECK: eret
  %t = trunc i32 %a to i24
  %l = shl i24 %t, 12
  %r = ashr i24 %l, 17
  %z = sext i24 %r to i32
  ret i32 %z
}

define i32 @cmp24(i32 noundef %a, i32 noundef %b) {
; CHECK-LABEL: _cmp24:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: jsle .LBB
  %ta = trunc i32 %a to i24
  %tb = trunc i32 %b to i24
  %c = icmp sgt i24 %ta, %tb
  %z = zext i1 %c to i32
  ret i32 %z
}

; A _BitInt(24) local is a 3-byte frame slot: SPX grows by 3 and the three
; byte stores span dr60-0x0002 .. dr60 (byte-granular stack storage).
define i24 @slot24(i32 noundef %a) {
; CHECK-LABEL: _slot24:
; CHECK: inc spx, #0x2
; CHECK-NEXT: inc spx, #0x1
; CHECK-DAG: mov @dr60-0x0002, r{{[0-9]+}}
; CHECK-DAG: mov @dr60-0x0001, r{{[0-9]+}}
; CHECK-DAG: mov @dr60, r{{[0-9]+}}
  %s = alloca i24, align 1
  %t = trunc i32 %a to i24
  store i24 %t, ptr %s, align 1
  %v = load i24, ptr %s, align 1
  ret i24 %v
}
