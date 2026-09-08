// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

_Static_assert(sizeof(double) == 4, "MCS251 double storage width");
_Static_assert(_Alignof(double) == _Alignof(float),
               "MCS251 double and float alignment");

double double_global;

// CHECK: @double_global = {{.*}}global float
// CHECK-LABEL: define {{.*}}float @add(float noundef %a, float noundef %b)
// CHECK-NOT: double
// CHECK-NOT: f64
// CHECK: fadd float
// CHECK: ret float
// CHECK-NOT: double
// CHECK-NOT: f64
// CHECK: }
double add(double a, double b) { return a + b; }
