; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj < %s -o %t
;
; Pinned to the v1 compatibility contract: this is a legacy-layout suite. The
; llc no-flag default is the xsmall/v2-Small model (clang cc1 default).
;
; Stage C and stack/ABI/relocation boundary cases.
@slot = external global ptr
@external_scalar = external global i16

define i8 @indirect(ptr %f) {
; CHECK-LABEL: _indirect:
; CHECK: ecall @dr{{[0-9]+}}
  %v = call i8 %f()
  ret i8 %v
}
define i32 @indirect_i32(ptr %f) {
; CHECK-LABEL: _indirect_i32:
; CHECK: mov b,
; CHECK: mov a,
; CHECK: ecall @dr{{[0-9]+}}
  %v = call i32 %f(i32 305419896)
  ret i32 %v
}
define i8 @local_fn() {
  ret i8 77
}
define ptr @local_address() {
; CHECK-LABEL: _local_address:
; CHECK: (_local_fn) >> 8, (_local_fn)
; CHECK: (_local_fn) >> 16
  ret ptr @local_fn
}
define ptr @local_addend() {
; CHECK-LABEL: _local_addend:
; CHECK: (_local_fn-65536) >> 8, (_local_fn-65536)
; CHECK: (_local_fn-65536) >> 16
  %p = getelementptr i8, ptr @local_fn, i32 -65536
  ret ptr %p
}
define i8 @local_indirect() {
; CHECK-LABEL: _local_indirect:
; CHECK: ecall _local_address
; CHECK: ecall @dr{{[0-9]+}}
  %p = call ptr @local_address()
  %v = call i8 %p()
  ret i8 %v
}
declare ptr @sdcc_pointer()
define i32 @sdcc_pointer_result() {
; CHECK-LABEL: _sdcc_pointer_result:
; CHECK: ecall _sdcc_pointer
; CHECK-NOT: mov {{r[0-9]+}}, a
; CHECK: eret
  %p = call ptr @sdcc_pointer()
  %r = ptrtoint ptr %p to i32
  ret i32 %r
}
declare i8 @sdcc_reader(ptr)
define i8 @sdcc_pointer_arg() {
; CHECK-LABEL: _sdcc_pointer_arg:
; CHECK: ecall _sdcc_reader
  %r = call i8 @sdcc_reader(ptr inttoptr (i32 131072 to ptr))
  ret i8 %r
}
define i16 @external_word(i16 %x) {
; CHECK-LABEL: _external_word:
; CHECK: (_external_scalar) >> 16
  store volatile i16 %x, ptr @external_scalar, align 1
  %v = load volatile i16, ptr @external_scalar, align 1
  ret i16 %v
}
define i32 @edge_load(ptr %p) {
; CHECK-LABEL: _edge_load:
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
  %q = getelementptr i8, ptr %p, i32 32767
  %v = load volatile i32, ptr %q, align 1
  ret i32 %v
}
define i8 @edge_neg(ptr %p) {
; CHECK-LABEL: _edge_neg:
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
  %q = getelementptr i8, ptr %p, i32 -32769
  %v = load volatile i8, ptr %q
  ret i8 %v
}
define i8 @dyn_stack(i16 %n) {
; CHECK-LABEL: _dyn_stack:
; CHECK: push dr16
; CHECK: mov 0x85,
; CHECK: mov 0x81,
; CHECK: ecall _stack_read
; CHECK: pop dr16
  %p = alloca i8, i16 %n, align 1
  store volatile i8 90, ptr %p
  %v = call i8 @stack_read(ptr %p)
  ret i8 %v
}
define i8 @nested_dyn(i16 %n) {
; CHECK-LABEL: _nested_dyn:
; CHECK: push dr16
; CHECK: ecall _dyn_stack
; CHECK: ecall _sdcc_reader
; CHECK: pop dr16
  %p = alloca i8, i16 %n, align 1
  store volatile i8 11, ptr %p
  %a = call i8 @dyn_stack(i16 %n)
  %b = call i8 @sdcc_reader(ptr %p)
  %c = load volatile i8, ptr %p
  %s = add i8 %a, %b
  %r = add i8 %s, %c
  ret i8 %r
}
define i8 @stack_read(ptr %p) {
  %v = load volatile i8, ptr %p
  ret i8 %v
}
declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)
define i16 @vla_loop(i16 %n) {
; CHECK-LABEL: _vla_loop:
; CHECK: push dr16
; CHECK: mov 0x85,
; CHECK: mov 0x81,
; CHECK: pop dr16
  br label %loop
loop:
  %i = phi i16 [ 0, %0 ], [ %next, %loop ]
  %sum = phi i16 [ 0, %0 ], [ %newsum, %loop ]
  %s = call ptr @llvm.stacksave()
  %p = alloca i8, i16 %n, align 1
  store volatile i8 3, ptr %p
  %v = load volatile i8, ptr %p
  %z = zext i8 %v to i16
  %newsum = add i16 %sum, %z
  call void @llvm.stackrestore(ptr %s)
  %next = add i16 %i, 1
  %more = icmp ult i16 %next, 4
  br i1 %more, label %loop, label %exit
exit:
  ret i16 %newsum
}
define i16 @recurse(i16 %n) {
; CHECK-LABEL: _recurse:
; CHECK: ecall _recurse
  %p = alloca i16, align 1
  store volatile i16 %n, ptr %p
  %done = icmp eq i16 %n, 0
  br i1 %done, label %base, label %rec
base:
  ret i16 0
rec:
  %dec = sub i16 %n, 1
  %r = call i16 @recurse(i16 %dec)
  %saved = load volatile i16, ptr %p
  %sum = add i16 %saved, %r
  ret i16 %sum
}
