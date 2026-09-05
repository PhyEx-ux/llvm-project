; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Phase 8: stores, mirror of load.ll (addressing forms verified against
; sdas251 V05.50.4). An i16 object is two byte stores with BIG-ENDIAN lane
; mapping: the value's hi lane (sub_hi8) goes to mem[base], the lo
; lane to mem[base+0x0001]. The store16 tests lock that mapping by pairing
; dph/dpl (the i16 argument's hi/lo bytes) with the displacements.

; The globals below are external on purpose: these tests lock the symbol
; address materialization encodings, which are identical for external and
; defined globals, and defined global data is (loudly) rejected until data
; areas exist -- see global-data-error.ll.

@gv8  = external global i8
@gv16 = external global i16

;-----------------------------------------------------------------------------
; Byte stores
;-----------------------------------------------------------------------------

; Constant value (the ABI has a single argument slot, so the value is an
; immediate and the pointer rides B:DPH:DPL).
define void @store8_ptr(ptr %p) {
; CHECK-LABEL: store8_ptr:
; CHECK:         mov r{{[0-9]+}}, #0xff
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  store i8 255, ptr %p
  ret void
}

define void @store8_ptr_off(ptr %p) {
; CHECK-LABEL: store8_ptr_off:
; CHECK:         mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
  %q = getelementptr i8, ptr %p, i16 1
  store i8 255, ptr %q
  ret void
}

; The store forms take the value in a register, so a constant global store
; is `mov wr,#gv8` then the indirect write.
define void @store8_g() {
; CHECK-LABEL: store8_g:
; CHECK:         .db 0x7e, {{.*}}(gv8) >> 8, (gv8)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv8) >> 16
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  store i8 255, ptr @gv8
  ret void
}

define void @store8_g_off() {
; CHECK-LABEL: store8_g_off:
; CHECK:         .db 0x7e, {{.*}}(gv8+1) >> 8, (gv8+1)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv8+1) >> 16
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  %q = getelementptr i8, ptr @gv8, i16 1
  store i8 255, ptr %q
  ret void
}

; Direct store to page-zero edata at 0x30.
define void @store8_direct() {
; CHECK-LABEL: store8_direct:
; CHECK:         mov r{{[0-9]+}}, #0xff
; CHECK-NEXT:    mov 0x30, r{{[0-9]+}}
  store i8 255, ptr inttoptr (i16 48 to ptr)
  ret void
}

; Direct store at 0x99 reaches the SFR space -- the ONLY encoding that does
; (same numeric address via @dr is region-00 edata; address-space trap, see
; MCS251ISelLowering.cpp).
define void @store8_sfr() {
; CHECK-LABEL: store8_sfr:
; CHECK:         mov r{{[0-9]+}}, #0x07
; CHECK-NEXT:    mov 0x99, r{{[0-9]+}}
  store i8 7, ptr inttoptr (i16 153 to ptr)
  ret void
}

define void @store8_gv(i8 %v) {
; CHECK-LABEL: store8_gv:
; CHECK:         .db 0x7e, {{.*}}(gv8) >> 8, (gv8)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv8) >> 16
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  store i8 %v, ptr @gv8
  ret void
}

; Volatile store: same encoding as the plain store.
define void @store8_volatile(ptr %p) {
; CHECK-LABEL: store8_volatile:
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  store volatile i8 1, ptr %p
  ret void
}

;-----------------------------------------------------------------------------
; Word stores (two byte stores, big-endian)
;-----------------------------------------------------------------------------

; The i16 argument arrives in dptr (dpl = lo, dph = hi). Big-endian store:
; dph (hi) -> displacement 0x0000, dpl (lo) -> displacement 0x0001.
define void @store16_g(i16 %v) {
; CHECK-LABEL: store16_g:
; CHECK:         mov [[LO:r[0-9]+]], dpl
; CHECK:         mov [[HI:r[0-9]+]], dph
; CHECK:         .db 0x7e, {{.*}}(gv16) >> 8, (gv16)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv16) >> 16
; CHECK:         mov @dr{{[0-9]+}}, [[HI]]
; CHECK-NEXT:    mov @dr{{[0-9]+}}+0x0001, [[LO]]
  store i16 %v, ptr @gv16
  ret void
}

; Constant i16 value materialised whole, then stored by its lanes: wr =
; 0x1234 stores 0x12 at  and 0x34 at +0x0001 (r2 = wr2 hi, r3 = lo).
define void @store16_ptr(ptr %p) {
; CHECK-LABEL: store16_ptr:
; CHECK:         mov [[HI:r[0-9]+]], #0x12
; CHECK:         mov @dr{{[0-9]+}}, [[HI]]
; CHECK:         mov [[LO:r[0-9]+]], #0x34
; CHECK:         mov @dr{{[0-9]+}}+0x0001, [[LO]]
  store i16 4660, ptr %p
  ret void
}

define void @store16_g_off(i16 %v) {
; CHECK-LABEL: store16_g_off:
; CHECK:         mov [[LO:r[0-9]+]], dpl
; CHECK:         mov [[HI:r[0-9]+]], dph
; CHECK:         .db 0x7e, {{.*}}(gv16+2) >> 8, (gv16+2)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv16+2) >> 16
; CHECK:         mov @dr{{[0-9]+}}, [[HI]]
; CHECK-NEXT:    mov @dr{{[0-9]+}}+0x0001, [[LO]]
  %q = getelementptr i8, ptr @gv16, i16 2
  store i16 %v, ptr %q
  ret void
}

; Volatile i16 store: same two-instruction shape.
define void @store16_volatile(i16 %v) {
; CHECK-LABEL: store16_volatile:
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NEXT:    mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
  store volatile i16 %v, ptr @gv16
  ret void
}

;-----------------------------------------------------------------------------
; Truncating store (i16 value -> i8 memory)
;-----------------------------------------------------------------------------

; Runtime i16 value: the low lane (dpl) is stored. (A load16+trunc+store8
; chain is folded by the DAG combiner into a single byte load+store before
; lowering, so the trunc path needs the value to come from the ABI slot.)
define void @store16_trunc(i16 %v) {
; CHECK-LABEL: store16_trunc:
; CHECK:         mov [[LO:r[0-9]+]], dpl
; CHECK:         .db 0x7e, {{.*}}(gv8) >> 8, (gv8)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv8) >> 16
; CHECK:         mov @dr{{[0-9]+}}, [[LO]]
  %t = trunc i16 %v to i8
  store i8 %t, ptr @gv8
  ret void
}

; Truncating store to a constant direct address keeps the dir8 form: falling back to @dr would retarget 0x80-0xff from the SFR space
; to region-00 edata.
define void @store16_trunc_direct(i16 %v) {
; CHECK-LABEL: store16_trunc_direct:
; CHECK:         mov [[LO:r[0-9]+]], dpl
; CHECK-NOT:     mov wr{{[0-9]+}}, #0x0030
; CHECK:         mov 0x30, [[LO]]
  %t = trunc i16 %v to i8
  store i8 %t, ptr inttoptr (i16 48 to ptr)
  ret void
}

define void @store16_trunc_sfr(i16 %v) {
; CHECK-LABEL: store16_trunc_sfr:
; CHECK:         mov [[LO:r[0-9]+]], dpl
; CHECK:         mov 0x99, [[LO]]
  %t = trunc i16 %v to i8
  store i8 %t, ptr inttoptr (i16 153 to ptr)
  ret void
}

; Constant truncation shortens to a plain byte store.
define void @store16_trunc_const() {
; CHECK-LABEL: store16_trunc_const:
; CHECK:         mov r{{[0-9]+}}, #0x34
; CHECK:         .db 0x7e, {{.*}}(gv8) >> 8, (gv8)
; CHECK-NEXT:    .db 0x7a, {{.*}}(gv8) >> 16
; CHECK:         mov @dr{{[0-9]+}}, r{{[0-9]+}}
  %t = trunc i16 4660 to i8
  store i8 %t, ptr @gv8
  ret void
}
