// RUN: %clang_cc1 -triple mcs251-unknown-none -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -emit-pch -o %t.pch %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -include-pch %t.pch -x c -emit-llvm -disable-llvm-passes -o - /dev/null | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -x objective-c -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -x c++ -std=c++17 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -DHOST -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=HOST
// RUN: not %clang_cc1 -triple mcs251-unknown-none -x objective-c -fobjc-gc -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=GC

// No MCS251 GC runtime is introduced: keep the overlapping-copy semantics
// and both address spaces in IR, rather than passing AS4 to an AS0 runtime.
// GC: error: cannot compile this Objective-C garbage-collected memmove on MCS251 yet
#ifdef __cplusplus
extern "C" {
#endif
#ifdef HOST
// HOST-LABEL: define{{.*}} @plain(
// HOST: call{{.*}} @objc_memmove_collectable(
// HOST-NOT: llvm.memmove
void plain(char *d, char *s) { __builtin_objc_memmove_collectable(d, s, 8); }
#else
typedef char __attribute__((address_space(4))) Code;
typedef char __attribute__((address_space(3))) XData;
// CHECK-LABEL: define{{.*}} @from_code(
// CHECK: call{{.*}} @llvm.memmove.p0.p4.i32(ptr {{.*}}, ptr addrspace(4) {{.*}}, i32 8, i1 false)
void from_code(char *d, const Code *s) { __builtin_objc_memmove_collectable(d, s, 8); }
// CHECK-LABEL: define{{.*}} @to_xdata(
// CHECK: call{{.*}} @llvm.memmove.p3.p4.i32(ptr addrspace(3) {{.*}}, ptr addrspace(4) {{.*}}, i32 8, i1 false)
void to_xdata(XData *d, const Code *s) { __builtin_objc_memmove_collectable(d, s, 8); }
// CHECK-LABEL: define{{.*}} @from_xdata(
// CHECK: call{{.*}} @llvm.memmove.p0.p3.i32(
void from_xdata(char *d, const XData *s) { __builtin_objc_memmove_collectable(d, s, 8); }
// CHECK-LABEL: define{{.*}} @plain(
// CHECK: call{{.*}} @llvm.memmove.p0.p0.i32(
void plain(char *d, char *s) { __builtin_objc_memmove_collectable(d, s, 8); }
// The C builtin returns default-space void *, independently of the operand.
// CHECK-LABEL: define{{.*}} @return_destination(
// CHECK: call{{.*}} @llvm.memmove.p3.p4.i32(
// CHECK: addrspacecast ptr addrspace(3) {{.*}} to ptr
void *return_destination(XData *d, const Code *s) {
  return __builtin_objc_memmove_collectable(d, s, 8);
}
#endif
#ifdef __cplusplus
}
#endif
