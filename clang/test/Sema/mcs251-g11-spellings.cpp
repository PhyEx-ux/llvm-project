// RUN: %clang_cc1 -triple mcs251 -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -ast-print %s > %t
// RUN: FileCheck %s --check-prefix=PRINT < %t
// expected-no-diagnostics

[[mcu::place_at(0x100), mcu::retain]] int cxx;
[[mcu::retain, mcu::place_at(0x110)]] int reversed;
[[mcu::bind_at(0x200)]] extern int bound;
[[gnu::mcu_place_at(0x300), gnu::mcu_retain]] int gnu_scoped;
[[gnu::mcu_bind_at(0x400)]] extern void bound_fn();
__attribute__((mcu_place_at(0x500), mcu_retain)) int gnu;
// PRINT: {{\[\[}}mcu::place_at(256)]]
// PRINT-SAME: {{\[\[}}mcu::retain]]
// PRINT: {{\[\[}}mcu::retain]]
// PRINT-SAME: {{\[\[}}mcu::place_at(272)]]
// PRINT: {{\[\[}}mcu::bind_at(512)]]
