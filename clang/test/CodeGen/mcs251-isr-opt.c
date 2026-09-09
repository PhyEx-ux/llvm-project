// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O3 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -Os -emit-llvm -o - %s | FileCheck %s

// ISR campaign T09: the card-frozen optimization test. The ISR body carries
// two early returns around a helper call so no optimization level may fold
// the entry away; the internal definition must keep its CC, slot attribute
// and llvm.used keepalive root verbatim from -O0 through -Os.
//
// The text form is the card-frozen one and matches the landed T02
// mcs251-isr.c exactly: explicit -mcs251-memory-contract=1,1,32,8,1 (the
// contract the clang driver emits AS0 ISR definitions under, so the frozen
// plain-ptr used root and `define internal mcs251_intrcc` text apply here).

extern void helper(unsigned char, unsigned char);
volatile unsigned char value;

static void irq(void) __attribute__((interrupt(1)));
static void irq(void) {
  if (value == 0)
    return;
  helper(value, 3);
  if (value == 2)
    return;
  value = 4;
}

void ordinary(void) { helper(1, 2); }

// CHECK: @llvm.used = appending global
// CHECK-SAME: @irq
// CHECK: define internal mcs251_intrcc void @irq()
// CHECK: call void @helper
// CHECK: define{{.*}} void @ordinary()
// CHECK: "mcs251-isr-vector"="1"
