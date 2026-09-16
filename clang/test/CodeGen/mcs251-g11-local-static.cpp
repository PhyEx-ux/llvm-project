// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -emit-llvm -o - %s | FileCheck %s
// G11 §3.2 entity identity: a function-local static is identified by its
// *entity* context, not by the simple function name plus the variable name.
// Two overloads of one function name are different functions, so their
// same-named local statics are different entities with different stable
// symbols (and therefore different .mcu.fixed.* section names in G11-B). A
// solver that splices FD->getName() + "." + D->getName() collapses them. The
// function component of the identity is the C++ mangled name of the enclosing
// function (only a declaration-structure property: overload signature, class
// and closure type included), the variable component is the plain declaration
// name, and same-named statics of one function are separated by an AST
// source-order ordinal.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define RETAIN __attribute__((mcu_retain))

int overloaded(int) { static int x PLACE(0x100) RETAIN; return x; }
int overloaded(long) { static int x PLACE(0x200) RETAIN; return x; }

struct Device {
  void configure(int);
  void configure(long);
};
void Device::configure(int) { static int state PLACE(0x300) RETAIN; (void)state; }
void Device::configure(long) { static int state PLACE(0x400) RETAIN; (void)state; }

// Each global is bound to its own attribute group: a placement or identity
// string can only be satisfied by the attributes of that very global.
// CHECK: @_ZZ10overloadediE1x = internal global i32 0, align 1 #[[OI:[0-9]+]]
// CHECK: @_ZZ10overloadedlE1x = internal global i32 0, align 1 #[[OL:[0-9]+]]
// CHECK: @_ZZN6Device9configureEiE5state = internal global i32 0, align 1 #[[DI:[0-9]+]]
// CHECK: @_ZZN6Device9configureElE5state = internal global i32 0, align 1 #[[DL:[0-9]+]]
// The keepalive container holds exactly the four retained entities; the count
// plus one named match per member exhausts it.
// CHECK: @llvm.used = appending global [4 x ptr] [
// CHECK-DAG: ptr @_ZZ10overloadediE1x
// CHECK-DAG: ptr @_ZZ10overloadedlE1x
// CHECK-DAG: ptr @_ZZN6Device9configureEiE5state
// CHECK-DAG: ptr @_ZZN6Device9configureElE5state
// CHECK: ], section "llvm.metadata"
// The stable symbols are pairwise distinct: the overload signature (and the
// class for the members) is part of the entity identity through the enclosing
// function's mangled name. The TU-qualified hash digits are matched by the
// fixed 8-hex-digit shape; each identity is <TU>.<mangled function>.<variable
// declaration name>.
// CHECK: attributes #[[OI]] = { "mcs251-place"="0x100,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._Z10overloadedi.x" }
// CHECK: attributes #[[OL]] = { "mcs251-place"="0x200,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._Z10overloadedl.x" }
// CHECK: attributes #[[DI]] = { "mcs251-place"="0x300,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._ZN6Device9configureEi.state" }
// CHECK: attributes #[[DL]] = { "mcs251-place"="0x400,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._ZN6Device9configureEl.state" }
