; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 2/3: constant materialisation and the i8/i16 return-value ABI.
; i8 results are returned in dpl, i16 results in the dpl:dph pair
; (dpl = low byte, dph = high byte). All facts measured on hardware
; (SDCC 4.6.0 #16555 + sdas251 V05.50.4 + QEMU stc32g144k246).

define void @ret_void() {
; CHECK-LABEL: ret_void:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    eret
; CHECK-NEXT:  ; -- End function
  ret void
}

define i8 @ret_i8_42() {
; CHECK-LABEL: ret_i8_42:
; CHECK:         mov [[R:r[0-9]+]], #0x2a
; CHECK-NEXT:    mov dpl, [[R]]
; CHECK-NEXT:    eret
  ret i8 42
}

define i8 @ret_i8_zero() {
; CHECK-LABEL: ret_i8_zero:
; CHECK:         mov [[R:r[0-9]+]], #0x00
; CHECK-NEXT:    mov dpl, [[R]]
; CHECK-NEXT:    eret
  ret i8 0
}

define i8 @ret_i8_max() {
; CHECK-LABEL: ret_i8_max:
; CHECK:         mov [[R:r[0-9]+]], #0xff
; CHECK-NEXT:    mov dpl, [[R]]
; CHECK-NEXT:    eret
  ret i8 255
}

define i8 @ret_i8_neg() {
; Negative constants must print as their two's-complement byte pattern.
; CHECK-LABEL: ret_i8_neg:
; CHECK:         mov [[R:r[0-9]+]], #0xff
; CHECK-NEXT:    mov dpl, [[R]]
; CHECK-NEXT:    eret
  ret i8 -1
}

define i16 @ret_i16_1234() {
; The 16-bit constant goes to a wr pair; dpl receives the low byte (the
; sub_lo8 lane) and dph the high byte (sub_hi8 lane).
; CHECK-LABEL: ret_i16_1234:
; CHECK:         mov [[W:wr[0-9]+]], #0x1234
; CHECK-NEXT:    mov dpl, r{{[0-9]+}}
; CHECK-NEXT:    mov dph, r{{[0-9]+}}
; CHECK-NEXT:    eret
  ret i16 4660
}

define i16 @ret_i16_zero() {
; CHECK-LABEL: ret_i16_zero:
; CHECK:         mov [[W:wr[0-9]+]], #0x0000
; CHECK-NEXT:    mov dpl, r{{[0-9]+}}
; CHECK-NEXT:    mov dph, r{{[0-9]+}}
; CHECK-NEXT:    eret
  ret i16 0
}

define i16 @ret_i16_max() {
; CHECK-LABEL: ret_i16_max:
; CHECK:         mov [[W:wr[0-9]+]], #0xffff
; CHECK-NEXT:    mov dpl, r{{[0-9]+}}
; CHECK-NEXT:    mov dph, r{{[0-9]+}}
; CHECK-NEXT:    eret
  ret i16 -1
}

; With allocation order ascending, wr0 = [r1 (lo), r0 (hi)]: the low byte
; of 0x1234 (0x34) lives in r1 and must land in dpl, the high byte (0x12)
; lives in r0 and must land in dph.
define i16 @ret_i16_lane_order() {
; CHECK-LABEL: ret_i16_lane_order:
; CHECK:         mov wr0, #0x1234
; CHECK-NEXT:    mov dpl, r1
; CHECK-NEXT:    mov dph, r0
; CHECK-NEXT:    eret
  ret i16 4660
}
