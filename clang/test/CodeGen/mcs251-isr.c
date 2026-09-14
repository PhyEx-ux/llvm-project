// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

extern void helper(void);
static void irq(void) __attribute__((interrupt(1)));
static void irq(void) { helper(); }
void ordinary(void) { helper(); }

// G1-3: the profile maximum 126 and a reclassified high slot keep the
// MCS251_INTR calling convention, the llvm.used keepalive root and the
// canonical decimal attribute text; a 2-digit and a 3-digit slot sit in one
// module so nothing in CodeGen assumes the legacy 2-digit legacy domain.
static void top(void) __attribute__((interrupt(126)));
static void top(void) { helper(); }
static void rel(void) __attribute__((interrupt(45)));
static void rel(void) { helper(); }

// CHECK: @llvm.used = appending global
// CHECK-SAME: @irq
// CHECK-SAME: @top
// CHECK-SAME: @rel
// CHECK-DAG: define internal mcs251_intrcc void @irq()
// CHECK-DAG: define internal mcs251_intrcc void @top()
// CHECK-DAG: define internal mcs251_intrcc void @rel()
// CHECK-DAG: define{{.*}} void @ordinary()
// CHECK: "mcs251-isr-vector"="1"
// CHECK: "mcs251-isr-vector"="126"
// CHECK: "mcs251-isr-vector"="45"
// CHECK-NOT: "interrupt"=
