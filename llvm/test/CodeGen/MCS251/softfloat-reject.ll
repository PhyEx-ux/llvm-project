; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -O0 %t/f64-arith.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O2 %t/f64-arith.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O0 %t/f64-neg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O2 %t/f64-neg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O0 %t/f64-cmp.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O2 %t/f64-cmp.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O0 %t/math.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MATH
; RUN: not llc -mtriple=mcs251 -O2 %t/math.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MATH
; RUN: not llc -mtriple=mcs251 -O0 %t/wide-int-to-float.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=WIDE
; RUN: not llc -mtriple=mcs251 -O2 %t/wide-int-to-float.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=WIDE
; RUN: not llc -mtriple=mcs251 -O0 %t/float-to-wide-int.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=WIDE
; RUN: not llc -mtriple=mcs251 -O2 %t/float-to-wide-int.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=WIDE
; RUN: not llc -mtriple=mcs251 -O0 %t/f64-to-f32.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O2 %t/f64-to-f32.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -O0 %t/vector-add.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=VECTOR
; RUN: not llc -mtriple=mcs251 -O2 %t/vector-add.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=VECTOR
;
; F64 must never be aliased to the f32 ABI subset.  The unsupported math
; family also stays outside the connected set.  G7 S1' (PM ruling 2026-09-15,
; D1) connected the unsigned i32 <-> f32 pair AND the narrow i8/i16 forms
; (promoted to i32 by the generic soft-float legalizer), so those are no
; longer rejections -- what stays rejected is any integer WIDER than 32 bits
; (no DI helper exists) and any f64 conversion.
;
; F64: LLVM ERROR: MCS251 contract violation: f64 IR is not supported; MCS251 only connects an explicit f32 libcall subset
; MATH: LLVM ERROR: MCS251 contract violation: the f32 intrinsic '{{.*}}' is not in the connected f32 subset (basic arithmetic, conversions and compares are connected; math functions such as sqrt are not)
; WIDE: LLVM ERROR: MCS251 contract violation: f32 conversion is not in the connected libcall subset
; VECTOR: LLVM ERROR: MCS251 contract violation: this f32 operation is not in the connected f32 subset (the connected subset is the basic arithmetic/division helpers); vector and remaining float forms are not wired

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

;--- wide-int-to-float.ll
; i64 sources need a DI helper, which the connected set deliberately omits.
define float @wide_int_to_float(i64 %a) {
  %r = uitofp i64 %a to float
  ret float %r
}

;--- float-to-wide-int.ll
define i64 @float_to_wide_int(float %a) {
  %r = fptoui float %a to i64
  ret i64 %r
}

;--- f64-to-f32.ll
; f64 is never an alias for the f32 subset, not even narrowing into it.
define float @f64_to_f32(double %a) {
  %r = fptrunc double %a to float
  ret float %r
}
