// RUN: %clang_cc1 -triple mcs251-unknown-none -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -fsyntax-only -verify=err -DTEST_ERRORS %s

#ifndef TEST_ERRORS
void isr_a(void) __attribute__((interrupt(1), mcs251_isr_priority(3)));
void isr_a(void) {}

[[mcu::isr_priority(2)]] void __attribute__((interrupt(4))) isr_b(void) {}

// CHECK: define dso_local mcs251_intrcc void @isr_a() addrspace(4) #[[A:[0-9]+]]
// CHECK: define dso_local mcs251_intrcc void @isr_b() addrspace(4) #[[B:[0-9]+]]
// CHECK-DAG: attributes #[[A]] = { {{.*}}"mcs251-isr-priority"="3"{{.*}}"mcs251-isr-vector"="1"
// CHECK-DAG: attributes #[[B]] = { {{.*}}"mcs251-isr-priority"="2"{{.*}}"mcs251-isr-vector"="4"
#else
void bad_range(void) __attribute__((interrupt(1), mcs251_isr_priority(4))); // err-error {{MCS251 interrupt priority must be an integer constant in 0-3}}
void bad_neg(void) __attribute__((interrupt(2), mcs251_isr_priority(-1))); // err-error {{MCS251 interrupt priority must be an integer constant in 0-3}}
void not_isr(void) __attribute__((mcs251_isr_priority(1))); // err-error {{'mcs251_isr_priority' attribute is only valid on MCS251 interrupt functions}}
void conflict(void) __attribute__((interrupt(3), mcs251_isr_priority(1)));
void conflict(void) __attribute__((mcs251_isr_priority(2))); // err-error {{conflicting MCS251 interrupt priority}}
#endif
