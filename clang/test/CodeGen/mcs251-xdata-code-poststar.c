// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
// RUN:   -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

// X1-3 IR contract: the post-'*' qualifier places the pointer *object* in
// the named space (the same IR the equivalent
// `char * __attribute__((address_space(3)))` declaration gets), while the
// specifier position keeps qualifying the pointed-to object. The two forms
// must stay visibly different in IR.

char * xdata p1;
// CHECK-DAG: @p1 = addrspace(3) global ptr null, align 1

char * code p2;
// CHECK-DAG: @p2 = addrspace(4) global ptr null, align 1

char * __xdata p3;
// CHECK-DAG: @p3 = addrspace(3) global ptr null, align 1

char xdata * into_xdata;
// CHECK-DAG: @into_xdata = global ptr addrspace(3) null, align 1

char code * into_code;
// CHECK-DAG: @into_code = global ptr addrspace(4) null, align 1

// Writing through the (writable) XDATA-located pointer object emits an AS3
// store of the pointer value.
void set_p1(char *v) { p1 = v; }
// CHECK-LABEL: define dso_local void @set_p1(
// CHECK: store ptr %{{.*}}, ptr addrspace(3) @p1

// Reading through a pointer into CODE loads from AS4.
char __code rom[4];
char read_rom(unsigned i) { return into_code[i]; }
// CHECK-LABEL: define dso_local signext i8 @read_rom(
// CHECK: load i8, ptr addrspace(4) %{{.*}}
