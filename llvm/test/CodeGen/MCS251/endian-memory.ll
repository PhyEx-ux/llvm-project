; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o - | FileCheck %s
;
; No explicit DataLayout: exercise the target default, not a test override.
; The optional validation/mcs251-endian/run.py harness defines these scalar
; objects in SDCC edata/xdata and checks both directions in QEMU, O0/O2,
; asm/obj. In particular, load narrowing must not select the MSB as the LSB.
@_e16 = external global i16
@_e32 = external global i32
@_x16 = external global i16
@_x32 = external global i32

; CHECK-LABEL: _read_e16:
define i16 @_read_e16() {
  %v = load volatile i16, ptr @_e16, align 1
  ret i16 %v
}

; CHECK-LABEL: _read_e32:
define i32 @_read_e32() {
  %v = load volatile i32, ptr @_e32, align 1
  ret i32 %v
}

; CHECK-LABEL: _read_x16:
define i16 @_read_x16() {
  %v = load volatile i16, ptr @_x16, align 1
  ret i16 %v
}

; CHECK-LABEL: _read_x32:
define i32 @_read_x32() {
  %v = load volatile i32, ptr @_x32, align 1
  ret i32 %v
}

; CHECK-LABEL: _write_e16:
define void @_write_e16(i16 %v) {
  store volatile i16 %v, ptr @_e16, align 1
  ret void
}

; CHECK-LABEL: _write_e32:
define void @_write_e32(i32 %v) {
  store volatile i32 %v, ptr @_e32, align 1
  ret void
}

; CHECK-LABEL: _write_x16:
define void @_write_x16(i16 %v) {
  store volatile i16 %v, ptr @_x16, align 1
  ret void
}

; CHECK-LABEL: _write_x32:
define void @_write_x32(i32 %v) {
  store volatile i32 %v, ptr @_x32, align 1
  ret void
}

; 0x1357 -> 0x57, not 0x13. DAGCombiner narrows the load at O2.
; CHECK-LABEL: _low16:
; CHECK: .db 0x7e, {{0x[0-9a-f]+}}, (_x16+1) >> 8, (_x16+1)
; CHECK-NEXT: .db 0x7a, {{0x[0-9a-f]+}}, 0x00, (_x16+1) >> 16
define i8 @_low16() {
  %v = load i16, ptr @_x16, align 1
  %r = trunc i16 %v to i8
  ret i8 %r
}

; 0x89abcdef -> 0xef, not 0x89.
; CHECK-LABEL: _low32:
; CHECK: .db 0x7e, {{0x[0-9a-f]+}}, (_x32+3) >> 8, (_x32+3)
; CHECK-NEXT: .db 0x7a, {{0x[0-9a-f]+}}, 0x00, (_x32+3) >> 16
define i8 @_low32() {
  %v = load i32, ptr @_x32, align 1
  %r = trunc i32 %v to i8
  ret i8 %r
}

; 0x89abcdef -> 0xcdef, not 0x89ab.
; CHECK-LABEL: _lowword32:
; CHECK: .db 0x7e, {{0x[0-9a-f]+}}, (_x32+2) >> 8, (_x32+2)
; CHECK-NEXT: .db 0x7a, {{0x[0-9a-f]+}}, 0x00, (_x32+2) >> 16
define i16 @_lowword32() {
  %v = load i32, ptr @_x32, align 1
  %r = trunc i32 %v to i16
  ret i16 %r
}

; Store-to-load forwarding interprets the bytes of the constant store.
; CHECK-LABEL: _forward16:
; CHECK: mov [[HI16:r[0-9]+]], #0x13
; CHECK: mov dpl, [[HI16]]
define i8 @_forward16() {
  %p = alloca i16, align 1
  store i16 4951, ptr %p, align 1
  %r = load i8, ptr %p, align 1
  ret i8 %r
}

; CHECK-LABEL: _forward32:
; CHECK: mov [[HI32:r[0-9]+]], #0x89
; CHECK: mov dpl, [[HI32]]
define i8 @_forward32() {
  %p = alloca i32, align 1
  store i32 2309737967, ptr %p, align 1
  %r = load i8, ptr %p, align 1
  ret i8 %r
}

; CHECK-LABEL: _stack16:
define i16 @_stack16(i16 %v) {
  %p = alloca i16, align 1
  store volatile i16 %v, ptr %p, align 1
  %r = load volatile i16, ptr %p, align 1
  ret i16 %r
}

; CHECK-LABEL: _stack32:
define i32 @_stack32(i32 %v) {
  %p = alloca i32, align 1
  store volatile i32 %v, ptr %p, align 1
  %r = load volatile i32, ptr %p, align 1
  ret i32 %r
}

; Truncating stores use the low numeric bits, in big-endian memory order.
; CHECK-LABEL: _trunc_store16:
define void @_trunc_store16(i32 %v) {
  %r = trunc i32 %v to i16
  store volatile i16 %r, ptr @_x16, align 1
  ret void
}

; CHECK-LABEL: _zext_load16:
define i32 @_zext_load16() {
  %v = load volatile i16, ptr @_x16, align 1
  %r = zext i16 %v to i32
  ret i32 %r
}
