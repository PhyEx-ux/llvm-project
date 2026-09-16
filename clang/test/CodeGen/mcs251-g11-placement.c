// RUN: %clang_cc1 -triple mcs251 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -fvisibility=hidden -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -emit-llvm -o %t.ll %s
// RUN: %python %S/Inputs/mcs251-g11-stable.py %clang_cc1 %t.dir

#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))
#define RETAIN __attribute__((mcu_retain))

int global PLACE(0x1234) = 7;
__xdata int far_global PLACE(0x20000) = 8;
__code int code_global PLACE(0xFC3000) = 9;
int warm PLACE(0x1300) __attribute__((noinit));
int kept PLACE(0x1400) RETAIN;
int kept_reverse RETAIN PLACE(0x1500);
// Unreferenced bind declarations must survive frontend lazy emission.
extern int unreferenced BIND(0x1600);
extern __xdata int unreferenced_far BIND(0x21000);
extern void unreferenced_fn(void) BIND(0xFC4000);
PLACE(0xFC5000) void placed_fn(void) {}

// Each entity is captured together with the number of its own attribute group,
// so the placement string below can only be satisfied by the attributes of
// that very entity. The stable symbol is the scalar declaration name for
// external entities (§3.2); the file-scope object/function split, the AS3/AS4
// storage classes and the bind/owned ownership are asserted verbatim.
// CHECK: @global = {{.*}}global i32 7, align 1 #[[GLOBAL:[0-9]+]]
// CHECK: @far_global = {{.*}}addrspace(3) global i32 8, align 1 #[[FARGLOBAL:[0-9]+]]
// CHECK: @code_global = {{.*}}addrspace(4) constant i32 9, align 1 #[[CODEGLOBAL:[0-9]+]]
// CHECK: @unreferenced = external{{.*}}global i32, align 1 #[[UNREF:[0-9]+]]
// CHECK: @unreferenced_far = external{{.*}}addrspace(3) global i32, align 1 #[[UNREFFAR:[0-9]+]]
// CHECK: @warm = {{.*}}global i32 0, align 1 #[[WARM:[0-9]+]]
// CHECK: @kept = {{.*}}global i32 0, align 1 #[[KEPT:[0-9]+]]
// CHECK: @kept_reverse = {{.*}}global i32 0, align 1 #[[KEPTREV:[0-9]+]]
// The keepalive container is exactly the two retained entities.
// CHECK: @llvm.used = appending global [2 x ptr] [
// CHECK-DAG: ptr @kept
// CHECK-DAG: ptr @kept_reverse
// CHECK: ], section "llvm.metadata"
// CHECK: declare{{.*}}void @unreferenced_fn() addrspace(4) #[[UNREFFN:[0-9]+]]
// CHECK: define{{.*}}void @placed_fn() addrspace(4) #[[PLACEDFN:[0-9]+]]
// CHECK: attributes #[[GLOBAL]] = { "mcs251-place"="0x1234,data,object,owned,0" "mcs251-stable-symbol"="global" }
// CHECK: attributes #[[FARGLOBAL]] = { "mcs251-place"="0x20000,xdata,object,owned,0" "mcs251-stable-symbol"="far_global" }
// CHECK: attributes #[[CODEGLOBAL]] = { "mcs251-place"="0xFC3000,code,object,owned,0" "mcs251-stable-symbol"="code_global" }
// CHECK: attributes #[[UNREF]] = { "mcs251-place"="0x1600,data,object,bind,0" "mcs251-stable-symbol"="unreferenced" }
// CHECK: attributes #[[UNREFFAR]] = { "mcs251-place"="0x21000,xdata,object,bind,0" "mcs251-stable-symbol"="unreferenced_far" }
// CHECK: attributes #[[WARM]] = { "mcs251-place"="0x1300,data,object,owned,2" "mcs251-stable-symbol"="warm" }
// CHECK: attributes #[[KEPT]] = { "mcs251-place"="0x1400,data,object,owned,1" "mcs251-stable-symbol"="kept" }
// CHECK: attributes #[[KEPTREV]] = { "mcs251-place"="0x1500,data,object,owned,1" "mcs251-stable-symbol"="kept_reverse" }
// CHECK: attributes #[[UNREFFN]] = {{.*}}"mcs251-place"="0xFC4000,code,function,bind,0" "mcs251-stable-symbol"="unreferenced_fn"
// CHECK: attributes #[[PLACEDFN]] = {{.*}}"mcs251-place"="0xFC5000,code,function,owned,0" "mcs251-stable-symbol"="placed_fn"
