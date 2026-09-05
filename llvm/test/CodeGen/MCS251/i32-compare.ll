; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -filetype=obj < %s -o %t
;
; All ten integer conditions, including materialisation and i32 select.
; Each jcc is the inverse condition, skipping the long true-edge EJMP.
define i8 @eq(i32 %x) {
; CHECK-LABEL: eq:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: jne
  %c = icmp eq i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @ne(i32 %x) {
; CHECK-LABEL: ne:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: je
  %c = icmp ne i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @ult(i32 %x) {
; CHECK-LABEL: ult:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: jnc
  %c = icmp ult i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @uge(i32 %x) {
; CHECK-LABEL: uge:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: j{{c|le}}
  %c = icmp uge i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @ugt(i32 %x) {
; CHECK-LABEL: ugt:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: j{{le|nc}}
  %c = icmp ugt i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @ule(i32 %x) {
; CHECK-LABEL: ule:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: j{{g|nc}}
  %c = icmp ule i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @slt(i32 %x) {
; CHECK-LABEL: slt:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: jsge
  %c = icmp slt i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @sge(i32 %x) {
; CHECK-LABEL: sge:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: jsl
  %c = icmp sge i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @sgt(i32 %x) {
; CHECK-LABEL: sgt:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: js{{le|ge}}
  %c = icmp sgt i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @sle(i32 %x) {
; CHECK-LABEL: sle:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK-NEXT: js{{g|l}}
  %c = icmp sle i32 %x, 305419896
  %r = zext i1 %c to i8
  ret i8 %r
}
define i32 @select32(i32 %x) {
; CHECK-LABEL: select32:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: ejmp
  %c = icmp ult i32 %x, 305419896
  %r = select i1 %c, i32 %x, i32 123456789
  ret i32 %r
}
define i8 @branch32(i32 %x) {
; CHECK-LABEL: branch32:
; CHECK: cmp dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: j{{nc|le}}
  %c = icmp ult i32 %x, 305419896
  br i1 %c, label %yes, label %no
yes:
  ret i8 42
no:
  ret i8 17
}
