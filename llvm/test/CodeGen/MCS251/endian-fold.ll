; RUN: opt -mtriple=mcs251 -passes=instcombine -S %s -o - | FileCheck %s
; RUN: opt -mtriple=mcs251 -passes='function(instcombine),globaldce' -S %s -o %t.ll
; RUN: llc -mtriple=mcs251 -verify-machineinstrs %t.ll -o %t.s
;
; These constants are removed before codegen: this tests LLVM core byte
; interpretation without depending on unsupported target global emission.
; opt must obtain the big-endian DataLayout from the target triple.
; CHECK: target datalayout = "E-m:e-p:32:8-i8:8-i16:8-i32:8-n8:16:32-S8"
@bytes = private constant [4 x i8] [i8 18, i8 52, i8 86, i8 120]
@scalar = private constant i32 2309737967
@record = private constant <{i8, i16, i32}> <{i8 165, i16 4951, i32 2309737967}>

; CHECK-LABEL: define i16 @_fold_bytes16()
; CHECK: ret i16 4660
define i16 @_fold_bytes16() {
  %v = load i16, ptr @bytes, align 1
  ret i16 %v
}

; CHECK-LABEL: define i32 @_fold_bytes32()
; CHECK: ret i32 305419896
define i32 @_fold_bytes32() {
  %v = load i32, ptr @bytes, align 1
  ret i32 %v
}

; CHECK-LABEL: define i8 @_fold_scalar_byte()
; CHECK: ret i8 -119
define i8 @_fold_scalar_byte() {
  %v = load i8, ptr @scalar, align 1
  ret i8 %v
}

; CHECK-LABEL: define i16 @_fold_scalar_word()
; CHECK: ret i16 -12817
define i16 @_fold_scalar_word() {
  %v = load i16, ptr getelementptr(i8, ptr @scalar, i32 2), align 1
  ret i16 %v
}

; Cross an aggregate field boundary: 0x1357's LSB followed by 0x89abcdef's MSB.
; CHECK-LABEL: define i16 @_fold_record()
; CHECK: ret i16 22409
define i16 @_fold_record() {
  %v = load i16, ptr getelementptr(i8, ptr @record, i32 2), align 1
  ret i16 %v
}
