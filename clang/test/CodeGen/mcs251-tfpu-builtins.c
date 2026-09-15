// G7 S3 (G7-FLOAT-DESIGN-draft.md §2.3 plan A / §2.4): the TFPU DMA
// coprocessor math builtins.
//
// Nine commands in one batch (PM D2/D3): the five unary transcendentals
// (sin/cos/tan/atan/sqrt) and the four binary arithmetic ops (add/sub/mul/
// div). The builtin is f32-typed at the source level; CodeGen bitcasts each
// float argument to its IEEE-754 bit pattern and calls the corresponding
// llvm.mcs251.tfpu.* i32 intrinsic, then bitcasts the result back -- the
// intrinsic signature is i32 because this target softens every f32 and the
// generic float legalizer has no softening case for intrinsic nodes (see the
// SIGNATURE NOTE in IntrinsicsMCS251.td).
//
// Contract matrix pinned here:
//   1. all nine builtins produce exactly one intrinsic call of the matching
//      ID, with the bit pattern bitcast in and out around it;
//   2. the intrinsic is [IntrHasSideEffects] -- the hardware sequence writes
//      SFR 0xED and occupies the coprocessor, so nothing may reorder it out
//      of the demo 38 TPIN measurement window;
//   3. WRONG TYPE (double, int, pointer) is rejected by Sema's exact-f32
//      gate -- `double` is 32 bits wide on this target but is a distinct
//      type, and routing it through would silently claim an f64 path;
//   4. WRONG ARITY for the unary/binary split is rejected;
//   5. the intrinsic is a compiler builtin, not a source symbol: it must NOT
//      leak into !mcs251.signatures (the P-4 signature-metadata route). Only
//      the source functions the TU defines are registered.
//
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -O0 -emit-llvm -o - %t/pos.c | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o - %t/pos.c | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -O0 -emit-llvm -o - %t/pos.c | opt -passes=verify -S - | FileCheck %s --check-prefix=VERIFY
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o - %t/pos.c | opt -passes=verify -S - | FileCheck %s --check-prefix=VERIFY
//
// The five Sema gates, each on its own fixture so a diagnostic is
// attributable to exactly one shape (split-file: one mutated factor per
// file).
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o /dev/null %t/e-double.c 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o /dev/null %t/e-int.c 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o /dev/null %t/e-ptr.c 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o /dev/null %t/e-second.c 2>&1 | FileCheck %s --check-prefix=TYPE2
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o /dev/null %t/e-arity-unary.c 2>&1 | FileCheck %s --check-prefix=MANY
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -O2 -emit-llvm -o /dev/null %t/e-arity-binary.c 2>&1 | FileCheck %s --check-prefix=FEW

// TYPE: error: argument 1 of '__builtin_mcs251_tfpu_
// TYPE-SAME: ' must have type 'float'; the MCS251 TFPU builtins accept only f32 operands
// TYPE2: error: argument 2 of '__builtin_mcs251_tfpu_div' must have type 'float'; the MCS251 TFPU builtins accept only f32 operands
// MANY: error: too many arguments to function call, expected 1, have 2
// FEW: error: too few arguments to function call, expected 2, have 1

//--- pos.c
// The positives: one intrinsic per builtin, in the matching ID. The bitcasts
// are the f32<->i32 boundary; the intrinsic itself never sees a float.
// CHECK-LABEL: define {{.*}} float @u_sin(
// CHECK: %[[B:.*]] = bitcast float %{{.*}} to i32
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.sin(i32 %[[B]])
// CHECK: bitcast i32 %{{.*}} to float
float u_sin(float x) { return __builtin_mcs251_tfpu_sin(x); }

// CHECK-LABEL: define {{.*}} float @u_cos(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.cos(i32
float u_cos(float x) { return __builtin_mcs251_tfpu_cos(x); }

// CHECK-LABEL: define {{.*}} float @u_tan(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.tan(i32
float u_tan(float x) { return __builtin_mcs251_tfpu_tan(x); }

// CHECK-LABEL: define {{.*}} float @u_atan(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.atan(i32
float u_atan(float x) { return __builtin_mcs251_tfpu_atan(x); }

// CHECK-LABEL: define {{.*}} float @u_sqrt(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.sqrt(i32
float u_sqrt(float x) { return __builtin_mcs251_tfpu_sqrt(x); }

// CHECK-LABEL: define {{.*}} float @b_add(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.add(i32
float b_add(float a, float b) { return __builtin_mcs251_tfpu_add(a, b); }

// CHECK-LABEL: define {{.*}} float @b_sub(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.sub(i32
float b_sub(float a, float b) { return __builtin_mcs251_tfpu_sub(a, b); }

// CHECK-LABEL: define {{.*}} float @b_mul(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.mul(i32
float b_mul(float a, float b) { return __builtin_mcs251_tfpu_mul(a, b); }

// CHECK-LABEL: define {{.*}} float @b_div(
// CHECK: %{{.*}} = {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.div(i32
float b_div(float a, float b) { return __builtin_mcs251_tfpu_div(a, b); }

// The nested chain: two commands, in source order, each with its own
// intrinsic call. This is the demo 38 shape (a transcendental feeding an
// arithmetic op).
// CHECK-LABEL: define {{.*}} float @chain(
// CHECK: {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.sin(i32
// CHECK: {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.mul(i32
float chain(float x, float y) {
  return __builtin_mcs251_tfpu_mul(__builtin_mcs251_tfpu_sin(x), y);
}

// CHECK-LABEL: define {{.*}} void @through_ptr(
// CHECK: {{(tail )?}}call{{.*}} i32 @llvm.mcs251.tfpu.sqrt(i32
// CHECK: store {{.*}} ptr
void through_ptr(float x, float *out) {
  *out = __builtin_mcs251_tfpu_sqrt(x);
}

// The intrinsic declaration is [IntrHasSideEffects]: it must not be marked
// readnone/readonly/argmemonly/speculatable (that is what would let a pass
// hoist or drop the hardware sequence). Pinned on the post-`opt verify`
// module so no IR pass folded it away.
// VERIFY-DAG: declare{{.*}} i32 @llvm.mcs251.tfpu.sin(i32)
// VERIFY-NOT: readnone
// VERIFY-NOT: readonly
// VERIFY-NOT: argmemonly
// VERIFY-NOT: speculatable

// Signature metadata (P-4): the intrinsic is a compiler builtin with no
// linkage symbol, so it must NOT appear in !mcs251.signatures. Only the
// source functions defined above are registered -- the names carry the C
// spelling, never llvm.mcs251.*.
// VERIFY: !mcs251.signatures = !{
// VERIFY-DAG: !{!"_u_sin", i32 1, i32 0, i32 0
// VERIFY-DAG: !{!"_b_add", i32 1, i32 0, i32 0, i32 0}
// VERIFY-DAG: !{!"_through_ptr", i32 1, i32 0, i32 0, i32 0}
// VERIFY-NOT: !{!"llvm.mcs251

//--- e-double.c
// double is 32 bits wide on this target but is a distinct type from float:
// accepting it would silently claim an f64 path that does not exist.
double e_double(double d) { return __builtin_mcs251_tfpu_sin(d); }

//--- e-int.c
int e_int(int i) { return __builtin_mcs251_tfpu_sqrt(i); }

//--- e-ptr.c
float e_ptr(float *p) { return __builtin_mcs251_tfpu_cos(p); }

//--- e-second.c
// The second operand is checked too, and the diagnostic names the position.
float e_second(float a, int b) { return __builtin_mcs251_tfpu_div(a, b); }

//--- e-arity-unary.c
// Arity is split unary/binary: the unary five take exactly one argument.
float e_arity_unary(float a, float b) { return __builtin_mcs251_tfpu_sin(a, b); }

//--- e-arity-binary.c
// And the binary four take exactly two.
float e_arity_binary(float a) { return __builtin_mcs251_tfpu_add(a); }
