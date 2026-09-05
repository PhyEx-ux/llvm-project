; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj < %s -o %t
;
; Four-byte canonical pointers, signed DR displacement and symbolic regions.
@far = external global i8
@slot = external global ptr

define i32 @pointer_size() {
; CHECK-LABEL: _pointer_size:
; CHECK: #0x0004
  %p = getelementptr ptr, ptr null, i32 1
  %n = ptrtoint ptr %p to i32
  ret i32 %n
}
define ptr @global_pointer() {
; CHECK-LABEL: _global_pointer:
; CHECK: .db 0x7e,
; CHECK: (_far) >> 8, (_far)
; CHECK-NEXT: {{.*}}.db 0x7a, {{.*}}0x00, (_far) >> 16
  ret ptr @far
}
define ptr @global_carry() {
; CHECK-LABEL: _global_carry:
; CHECK: (_far+32) >> 8, (_far+32)
; CHECK: (_far+32) >> 16
  %p = getelementptr i8, ptr @far, i32 32
  ret ptr %p
}
define i8 @ptr_inc(ptr %p) {
; CHECK-LABEL: _ptr_inc:
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0001
  %q = getelementptr i8, ptr %p, i32 1
  %v = load volatile i8, ptr %q
  ret i8 %v
}
define i8 @ptr_neg(ptr %p) {
; CHECK-LABEL: _ptr_neg:
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}-0x0001
  %q = getelementptr i8, ptr %p, i32 -1
  %v = load volatile i8, ptr %q
  ret i8 %v
}
define i8 @ptr_big(ptr %p) {
; CHECK-LABEL: _ptr_big:
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}
  %q = getelementptr i8, ptr %p, i32 65536
  %v = load volatile i8, ptr %q
  ret i8 %v
}
define i32 @ptr_index(i16 %i) {
; CHECK-LABEL: _ptr_index:
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
  %p = getelementptr i32, ptr inttoptr (i32 131072 to ptr), i16 %i
  %r = ptrtoint ptr %p to i32
  ret i32 %r
}
define i8 @ptr_branch(ptr %p) {
; CHECK-LABEL: _ptr_branch:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
  %c = icmp ult ptr %p, inttoptr (i32 131072 to ptr)
  br i1 %c, label %a, label %b
a:
  ret i8 42
b:
  ret i8 17
}
define i8 @ptr_roundtrip(ptr %p) {
; CHECK-LABEL: _ptr_roundtrip:
; CHECK: .db 0x7e,
; CHECK: mov @dr{{[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}
  store volatile ptr %p, ptr @slot, align 1
  %q = load volatile ptr, ptr @slot, align 1
  %same = icmp eq ptr %p, %q
  %r = zext i1 %same to i8
  ret i8 %r
}
define i32 @load32(ptr %p) {
; CHECK-LABEL: _load32:
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0003
  %v = load volatile i32, ptr %p, align 1
  ret i32 %v
}
define void @store32(i32 %v) {
; CHECK-LABEL: _store32:
; CHECK: mov @dr{{[0-9]+}},
; CHECK: mov @dr{{[0-9]+}}+0x0003,
  store volatile i32 %v, ptr inttoptr (i32 66048 to ptr), align 1
  ret void
}
define i8 @stack_address() {
; CHECK-LABEL: _stack_address:
; CHECK: mov {{r[0-9]+}}, 0x81
; CHECK: mov {{r[0-9]+}}, 0x85
; CHECK: ecall _stack_reader
  %p = alloca i8, align 1
  store volatile i8 90, ptr %p
  %v = call i8 @stack_reader(ptr %p)
  ret i8 %v
}
define i8 @stack_reader(ptr %p) {
  %v = load volatile i8, ptr %p
  ret i8 %v
}
