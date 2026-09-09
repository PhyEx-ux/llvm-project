// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

extern void helper(void);
static void irq(void) __attribute__((interrupt(1)));
static void irq(void) { helper(); }
void ordinary(void) { helper(); }

// CHECK: @llvm.used = appending global
// CHECK-SAME: @irq
// CHECK-DAG: define internal mcs251_intrcc void @irq()
// CHECK-DAG: define{{.*}} void @ordinary()
// CHECK: "mcs251-isr-vector"="1"
// CHECK-NOT: "interrupt"=
