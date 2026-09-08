; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; The generic 16-bit RAM pointer route is selected from the DataLayout width,
; not from a storage-model name. A displacement is folded only with a range
; proof for the runtime base; these unconstrained argument bases form the final
; i16 address explicitly before using the verified zero-displacement @WR form.

define i8 @near_load_byte(i16 %address) {
; CHECK-LABEL: _near_load_byte:
; CHECK: mov {{r[0-9]+}}, dpl
; CHECK: mov {{r[0-9]+}}, dph
; CHECK: mov {{r[0-9]+}}, @[[BASE:wr[0-9]+]]
  %p = inttoptr i16 %address to ptr
  %v = load volatile i8, ptr %p, align 1
  ret i8 %v
}

define void @near_store_word(i16 %address) {
; CHECK-LABEL: _near_store_word:
; CHECK: mov @[[BASE:wr[0-9]+]], {{r[0-9]+}}
; CHECK: add [[BASE]], #0x0001
; CHECK: mov @[[BASE]], {{r[0-9]+}}
  %p = inttoptr i16 %address to ptr
  store volatile i16 4660, ptr %p, align 1
  ret void
}

define i8 @near_plus_0x10(i16 %address) {
; CHECK-LABEL: _near_plus_0x10:
; CHECK: add [[ADDR:wr[0-9]+]], #0x0010
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 16
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i8 @near_minus_0x10(i16 %address) {
; CHECK-LABEL: _near_minus_0x10:
; CHECK: add [[ADDR:wr[0-9]+]], #0xfff0
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 -16
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i8 @near_plus_0x100(i16 %address) {
; CHECK-LABEL: _near_plus_0x100:
; CHECK: add [[ADDR:wr[0-9]+]], #0x0100
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 256
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i8 @near_minus_0x100(i16 %address) {
; CHECK-LABEL: _near_minus_0x100:
; CHECK: add [[ADDR:wr[0-9]+]], #0xff00
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 -256
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i8 @near_verified_max(i16 %address) {
; CHECK-LABEL: _near_verified_max:
; CHECK: add [[ADDR:wr[0-9]+]], #0x3eff
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 16127
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i8 @near_verified_negative_max(i16 %address) {
; CHECK-LABEL: _near_verified_negative_max:
; CHECK: add [[ADDR:wr[0-9]+]], #0xc101
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 -16127
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i8 @near_large_offset(i16 %address) {
; CHECK-LABEL: _near_large_offset:
; CHECK: add [[ADDR:wr[0-9]+]], #0x3f00
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i8, ptr %p, i16 16128
  %v = load volatile i8, ptr %q, align 1
  ret i8 %v
}

define i16 @near_cross_7fff() {
; CHECK-LABEL: _near_cross_7fff:
; CHECK: mov {{wr[0-9]+}}, #0x8010
  %q = getelementptr i8, ptr inttoptr (i16 32752 to ptr), i16 32
  %v = ptrtoint ptr %q to i16
  ret i16 %v
}

define i16 @near_back_to_7fff() {
; CHECK-LABEL: _near_back_to_7fff:
; CHECK: mov {{wr[0-9]+}}, #0x7fff
  %q = getelementptr i8, ptr inttoptr (i16 32768 to ptr), i16 -1
  %v = ptrtoint ptr %q to i16
  ret i16 %v
}

define i16 @near_last_value() {
; CHECK-LABEL: _near_last_value:
; CHECK: mov {{wr[0-9]+}}, #0xffff
  %q = getelementptr i8, ptr inttoptr (i16 65534 to ptr), i16 1
  %v = ptrtoint ptr %q to i16
  ret i16 %v
}

define i16 @near_scaled_index(i16 %address, i16 %index) {
; CHECK-LABEL: _near_scaled_index:
; CHECK: sll {{wr[0-9]+}}
; CHECK: add [[ADDR:wr[0-9]+]], {{wr[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
; CHECK: add [[ADDR]], #0x0001
; CHECK: mov {{r[0-9]+}}, @[[ADDR]]
  %p = inttoptr i16 %address to ptr
  %q = getelementptr i16, ptr %p, i16 %index
  %v = load volatile i16, ptr %q, align 1
  ret i16 %v
}

define i16 @near_pointer_roundtrip(ptr %address) {
; CHECK-LABEL: _near_pointer_roundtrip:
; CHECK: mov @[[SLOT:wr[0-9]+]], {{r[0-9]+}}
; CHECK: add [[NEXT:wr[0-9]+]], #0x0001
; CHECK: mov @[[NEXT]], {{r[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @[[SLOT]]
; CHECK: mov {{r[0-9]+}}, @[[NEXT]]
  store volatile ptr inttoptr (i16 4660 to ptr), ptr %address, align 1
  %result = load volatile ptr, ptr %address, align 1
  %integer = ptrtoint ptr %result to i16
  ret i16 %integer
}

; The same numeric 16-bit legalization is reusable by each approved near RAM
; address space; it is not welded to AS0 or to either storage-model spelling.
define i8 @near_as1(ptr addrspace(1) %p) {
; CHECK-LABEL: _near_as1:
; CHECK: mov {{r[0-9]+}}, @{{wr[0-9]+}}
  %v = load volatile i8, ptr addrspace(1) %p, align 1
  ret i8 %v
}

define i8 @near_as2(ptr addrspace(2) %p) {
; CHECK-LABEL: _near_as2:
; CHECK: mov {{r[0-9]+}}, @{{wr[0-9]+}}
  %v = load volatile i8, ptr addrspace(2) %p, align 1
  ret i8 %v
}

define i8 @near_as8(ptr addrspace(8) %p) {
; CHECK-LABEL: _near_as8:
; CHECK: mov {{r[0-9]+}}, @{{wr[0-9]+}}
  %v = load volatile i8, ptr addrspace(8) %p, align 1
  ret i8 %v
}

define i32 @near_ptrdiff(ptr %lhs, ptr %rhs) {
; CHECK-LABEL: _near_ptrdiff:
; CHECK: sub {{dr[0-9]+}}, {{dr[0-9]+}}
  %li = ptrtoint ptr %lhs to i32
  %ri = ptrtoint ptr %rhs to i32
  %diff = sub i32 %li, %ri
  ret i32 %diff
}
