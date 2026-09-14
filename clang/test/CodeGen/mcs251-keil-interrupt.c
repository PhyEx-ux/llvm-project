// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

#define SLOT 51
void keil() interrupt SLOT {}
void gnu(void) __attribute__((interrupt(50)));
void gnu(void) {}
void plain() {}

// G1-3: the Keil suffix emits the same IR as the GNU attribute at the high
// end of the profile -- the maximum 126 (three digits) and the reclassified
// 45 via a parenthesized constant expression.
void keilhigh() interrupt 126 {}
void keilrel() interrupt (45) {}

// CHECK-DAG: define{{.*}} mcs251_intrcc void @keil()
// CHECK-DAG: define{{.*}} mcs251_intrcc void @gnu()
// CHECK-DAG: define{{.*}} mcs251_intrcc void @keilhigh()
// CHECK-DAG: define{{.*}} mcs251_intrcc void @keilrel()
// CHECK-DAG: define{{.*}} void @plain()
// CHECK-DAG: "mcs251-isr-vector"="51"
// CHECK-DAG: "mcs251-isr-vector"="50"
// CHECK-DAG: "mcs251-isr-vector"="126"
// CHECK-DAG: "mcs251-isr-vector"="45"
