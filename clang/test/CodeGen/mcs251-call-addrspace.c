// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefix=V2
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefix=COMPAT
//
// The v2 program address space is AS4.  Keep every ordinary C function
// declaration/definition and both direct and indirect C call forms in that
// address space.  Compatibility deliberately remains AS0.

extern void external(void);

static void local(void) {}

void call_external(void) { external(); }
void call_local(void) { local(); }
void call_indirect(void (*fn)(void)) { fn(); }

// V2-LABEL: define{{.*}} void @call_external(){{.*}} addrspace(4)
// V2: call addrspace(4) void @external()
// V2: declare void @external() addrspace(4)
// V2-LABEL: define{{.*}} void @call_local(){{.*}} addrspace(4)
// V2: call addrspace(4) void @local()
// V2: define{{.*}} void @local(){{.*}} addrspace(4)
// V2-LABEL: define{{.*}} void @call_indirect(ptr addrspace(4){{.*}}){{.*}} addrspace(4)
// V2: call addrspace(4) void %{{.*}}()
//
// COMPAT-LABEL: define{{.*}} void @call_external()
// COMPAT: call void @external()
// COMPAT: declare void @external(){{.*}}
// COMPAT-NOT: declare void @external() addrspace(4)
// COMPAT-LABEL: define{{.*}} void @call_indirect(ptr{{.*}})
// COMPAT: call void %{{.*}}()
