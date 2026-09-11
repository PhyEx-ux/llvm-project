// RUN: %clang_cc1 -triple mcs251-unknown-none -cl-std=CL1.2 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -cl-std=CL2.0 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -cl-std=CL3.0 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -cl-std=CL2.0 -DHOST -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=HOST

#ifdef HOST
// HOST-LABEL: define{{.*}} @host_store(
// HOST: store half {{.*}}, ptr addrspace(4)
void host_store(half __attribute__((address_space(4))) *p) {
  __builtin_store_halff(1.0f, p);
}
#else
// CHECK-LABEL: define{{.*}} @write_xdata(
// CHECK: store half {{.*}}, ptr addrspace(3)
void write_xdata(half __xdata *p) { __builtin_store_half(1.0f, p); }
// CHECK-LABEL: define{{.*}} @write_default(
// CHECK: store half {{.*}}, ptr {{%.*}}
void write_default(half *p) { __builtin_store_halff(1.0f, p); }
// CHECK-LABEL: define{{.*}} @read_code(
// CHECK: load half, ptr addrspace(4)
float read_code(half __code *p) { return __builtin_load_halff(p); }
#endif
