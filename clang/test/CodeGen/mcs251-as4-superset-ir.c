// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s

// A1 IR contract (RUNTIME-AS-PTR-DESIGN-A.md §3-A1, §4.1): an AS4 -> AS0
// implicit conversion must materialize as an explicit `addrspacecast` in the
// IR, never as a bitcast, an integer round-trip, or a silent type change.
// `-disable-llvm-passes` keeps the emitted IR at the -O0 shape, so a fold of
// a global address cannot hide the cast (constant sources print the cast
// inline as a ConstantExpr, which is still an addrspacecast).

// The conversion never changes the *source* object: the globals stay in AS4,
// the array stays read-only, and no bitcast/ptrtoint round-trip appears
// anywhere in the module. These DAG checks come before the first match-label
// because they scan the module prologue.
// CHECK-DAG: @table = addrspace(4) constant [4 x i8] c"\01\02\03\04", align 1
// CHECK-DAG: @romptr = global ptr addrspace(4) null, align 1
// CHECK-DAG: @plain = global ptr null, align 1
// CHECK-NOT: bitcast ptr addrspace(4) @table
// CHECK-NOT: ptrtoint ptr addrspace(4) %{{.*}} to i32

typedef unsigned char uint8;

uint8 __code table[4] = {1, 2, 3, 4};
uint8 __code *romptr;
const uint8 *plain;

// Assignment of a constant CODE address to an AS0 pointer: the initializer is
// an addrspacecast ConstantExpr.
void assign(void) {
  plain = table;
}
// CHECK-LABEL: define dso_local void @assign(
// CHECK: store ptr addrspacecast (ptr addrspace(4) @table to ptr), ptr @plain

// Assignment of a runtime CODE pointer: a real addrspacecast instruction.
void assign_ptr(uint8 __code *p) {
  plain = p;
}
// CHECK-LABEL: define dso_local void @assign_ptr(
// CHECK: [[CAST:%.*]] = addrspacecast ptr addrspace(4) %{{.*}} to ptr
// CHECK: store ptr [[CAST]], ptr @plain

// Parameter passing: the argument arrives already converted.
void takes(const uint8 *s);
void call(uint8 __code *p) {
  takes(p);
}
// CHECK-LABEL: define dso_local void @call(
// CHECK: [[CAST:%.*]] = addrspacecast ptr addrspace(4) %{{.*}} to ptr
// CHECK: call addrspace(4) void @takes(ptr noundef [[CAST]])

// Explicit reverse conversion keeps the same addrspacecast instruction.
uint8 __code *to_code(uint8 *p) {
  return (uint8 __code *)p;
}
// CHECK-LABEL: define dso_local ptr addrspace(4) @to_code(
// CHECK: [[CAST:%.*]] = addrspacecast ptr %{{.*}} to ptr addrspace(4)
// CHECK: ret ptr addrspace(4) [[CAST]]

// Comparison between an AS4 pointer and an AS0 pointer: the composite type is
// resolved by the standard pointer rules, so one side is converted and the
// other keeps its own address space.
int cmp(void) {
  return table == plain;
}
// CHECK-LABEL: define dso_local i32 @cmp(
// CHECK: [[CAST:%.*]] = addrspacecast ptr %{{.*}} to ptr addrspace(4)
// CHECK: icmp eq ptr addrspace(4) @table, [[CAST]]

// Same-object pointer difference through the conversion is accepted and is
// computed on the converted representations (the AS4 operand is converted,
// not truncated). Note the order: the `sub` must be checked before the
// `ptrtoint` text it precedes on the same IR line.
long diff(void) {
  return (const uint8 *)table - plain;
}
// CHECK-LABEL: define dso_local i32 @diff(
// CHECK: sub i32 ptrtoint (ptr addrspacecast (ptr addrspace(4) @table to ptr) to i32)
