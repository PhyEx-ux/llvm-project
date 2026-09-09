// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

#define SLOT 51
void keil() interrupt SLOT {}
void gnu(void) __attribute__((interrupt(50)));
void gnu(void) {}
void plain() {}

// CHECK-DAG: define{{.*}} mcs251_intrcc void @keil()
// CHECK-DAG: define{{.*}} mcs251_intrcc void @gnu()
// CHECK-DAG: define{{.*}} void @plain()
// CHECK-DAG: "mcs251-isr-vector"="51"
// CHECK-DAG: "mcs251-isr-vector"="50"
