// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
// RUN:   -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

// X1-4: a string literal initializing a pointer into the CODE space denotes
// a constant object *in that space* (official shape:
// `uint8 code *PlotModeTxt[] = {"Vect", "Dots"};`). IR level: the anonymous
// string globals land in AS4 and the pointer table initializes from
// ptr addrspace(4) values. Object-level CODE placement of the table itself
// is the X3/backend slice.

typedef unsigned char BYTE, uint8;

uint8 code *PlotModeTxt[] = {"Vect", "Dots"};
// CHECK-DAG: @.str = private unnamed_addr addrspace(4) constant [5 x i8] c"Vect\00"
// CHECK-DAG: @.str.1 = private unnamed_addr addrspace(4) constant [5 x i8] c"Dots\00"
// CHECK-DAG: @PlotModeTxt = global [2 x ptr addrspace(4)] [ptr addrspace(4) @.str, ptr addrspace(4) @.str.1], align 1

__code char *RomMsg = "boot";
// CHECK-DAG: @.str.2 = private unnamed_addr addrspace(4) constant [5 x i8] c"boot\00"
// CHECK-DAG: @RomMsg = global ptr addrspace(4) @.str.2, align 1

// Plain default-space strings are untouched: constant global in AS0.
char *Plain[] = {"AS0"};
// CHECK-DAG: @.str.3 = private unnamed_addr constant [4 x i8] c"AS0\00"
// CHECK-DAG: @Plain = global [1 x ptr] [ptr @.str.3], align 1
