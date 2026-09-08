; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/f64-arith.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/f64-arith.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/f64-neg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/f64-neg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/f64-cmp.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/f64-cmp.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/math.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MATH
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/math.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MATH
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/unsigned-to-float.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNSIGNED
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/unsigned-to-float.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNSIGNED
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/float-to-unsigned.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNSIGNED
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/float-to-unsigned.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNSIGNED
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/vector-add.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=VECTOR
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/vector-add.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=VECTOR
;
; F64 must never be aliased to the f32 ABI subset.  The unsupported math and
; unsigned conversion families also stay outside the connected set.
;
; F64: LLVM ERROR: MCS251 contract violation: f64 IR is not supported; MCS251 only connects an explicit f32 libcall subset
; MATH: LLVM ERROR: MCS251 contract violation: f32/f64 intrinsic operation is not yet implemented; soft-float runtime is not connected
; UNSIGNED: LLVM ERROR: MCS251 contract violation: f32 conversion is not in the connected libcall subset
; VECTOR: LLVM ERROR: MCS251 contract violation: f32/f64 arithmetic is not yet implemented; soft-float runtime is not connected

;--- vector-add.ll
; Scalar ABI arguments keep the rejection focused on vector arithmetic, not
; unsupported vector parameter passing. Dynamic lanes must not be scalarized
; into an allowed f32 helper before the contract rejects the vector operation.
define float @vector_add(float %a, float %b) {
  %v = insertelement <2 x float> zeroinitializer, float %a, i32 0
  %w = insertelement <2 x float> zeroinitializer, float %b, i32 0
  %r = fadd <2 x float> %v, %w
  %x = extractelement <2 x float> %r, i32 0
  ret float %x
}

;--- f64-arith.ll
define double @f64_arith(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

;--- f64-neg.ll
define double @f64_neg(double %a) {
  %r = fneg double %a
  ret double %r
}

;--- f64-cmp.ll
define i1 @f64_cmp(double %a, double %b) {
  %r = fcmp uno double %a, %b
  ret i1 %r
}

;--- math.ll
declare float @llvm.sqrt.f32(float)
define float @math(float %a) {
  %r = call float @llvm.sqrt.f32(float %a)
  ret float %r
}

;--- unsigned-to-float.ll
define float @unsigned_to_float(i32 %a) {
  %r = uitofp i32 %a to float
  ret float %r
}

;--- float-to-unsigned.ll
define i32 @float_to_unsigned(float %a) {
  %r = fptoui float %a to i32
  ret i32 %r
}
