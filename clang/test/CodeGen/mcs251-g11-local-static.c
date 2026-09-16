// RUN: %clang_cc1 -triple mcs251 -Werror -emit-llvm -o - %s | FileCheck %s
// G11 §2.2: `mcu_place_at` is accepted on a static-storage object, and a
// function-local static is one (automatic storage is what is rejected). Its
// placement, noinit and retain policy must reach the emitted global and the
// keepalive container instead of being silently dropped: the entity is
// emitted by CGDecl::EmitStaticVarDecl, which does not go through
// CodeGenModule::EmitGlobal.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define RETAIN __attribute__((mcu_retain))
#define NOINIT __attribute__((noinit))

void plain(void) { static int x PLACE(0x100); }
void kept(void) { static int x PLACE(0x110) RETAIN; }
void warm(void) { static int y PLACE(0x120) NOINIT; }
void combined(void) { static int x PLACE(0x130) RETAIN NOINIT; }
// Two functions of the same translation unit each declare `x`: the identities
// must stay distinct, both in the stable symbol and in the section name that
// G11-B derives from it (one .mcu.fixed.* section per entity).
void same_name_a(void) { static int x PLACE(0x140); }
void same_name_b(void) { static int x PLACE(0x150); }
// One function, two blocks, the same variable name: two independent entities
// inside the *same* function. The simple function name plus the variable name
// cannot tell them apart, so the identity must (and the section name G11-B
// derives from it must) distinguish them.
void same_host(void) {
  { static int x PLACE(0x160) RETAIN; }
  { static int x PLACE(0x170) RETAIN; }
}
// XDATA-qualified local static: storage class axis is xdata, not data.
void far(void) { static __xdata int z PLACE(0x2000); }

// CHECK: @plain.x = internal global i32 0, align 1 #[[PLAIN:[0-9]+]]
// CHECK: @kept.x = internal global i32 0, align 1 #[[KEPT:[0-9]+]]
// CHECK: @warm.y = internal global i32 0, align 1 #[[WARM:[0-9]+]]
// CHECK: @combined.x = internal global i32 0, align 1 #[[COMBINED:[0-9]+]]
// CHECK: @same_name_a.x = internal global i32 0, align 1 #[[SNA:[0-9]+]]
// CHECK: @same_name_b.x = internal global i32 0, align 1 #[[SNB:[0-9]+]]
// CHECK: @same_host.x = internal global i32 0, align 1 #[[SH:[0-9]+]]
// The IR uniquing suffix (".1") is the module symbol table's own; the identity
// below uses the AST source-order ordinal (second occurrence -> ".2"), so the
// two suffixes intentionally differ.
// CHECK: @same_host.x.1 = internal global i32 0, align 1 #[[SH2:[0-9]+]]
// CHECK: @far.z = {{.*}}addrspace(3) global i32 0, align 1 #[[FAR:[0-9]+]]
// Only the retained local statics are keepalive roots (`plain`, `warm`,
// `same_name_*` and `far` carry no mcu_retain): the count plus one named match
// per member exhaust the container. Member order is not asserted.
// CHECK: @llvm.used = appending global [4 x ptr] [
// CHECK-DAG: ptr @kept.x
// CHECK-DAG: ptr @combined.x
// CHECK-DAG: ptr @same_host.x
// CHECK-DAG: ptr @same_host.x.1
// CHECK: ], section "llvm.metadata"
// CHECK: attributes #[[PLAIN]] = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.plain.x" }
// CHECK: attributes #[[KEPT]] = { "mcs251-place"="0x110,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.kept.x" }
// noinit is NOTE flags bit1.
// CHECK: attributes #[[WARM]] = { "mcs251-place"="0x120,data,object,owned,2" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.warm.y" }
// retain|noinit.
// CHECK: attributes #[[COMBINED]] = { "mcs251-place"="0x130,data,object,owned,3" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.combined.x" }
// CHECK: attributes #[[SNA]] = { "mcs251-place"="0x140,data,object,owned,0" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.same_name_a.x" }
// CHECK: attributes #[[SNB]] = { "mcs251-place"="0x150,data,object,owned,0" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.same_name_b.x" }
// The two same-function entities carry distinct identities: the identity is
// <TU>.<function name>.<variable name> plus, from the second same-named static
// of the same function onwards, its AST source-order ordinal (".2"), so the
// second entity cannot collapse onto the first no matter what the emitted
// global names look like.
// CHECK: attributes #[[SH]] = { "mcs251-place"="0x160,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.same_host.x" }
// CHECK: attributes #[[SH2]] = { "mcs251-place"="0x170,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.same_host.x.2" }
// CHECK: attributes #[[FAR]] = { "mcs251-place"="0x2000,xdata,object,owned,0" "mcs251-stable-symbol"="mcs251_g11_local_static_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.far.z" }
