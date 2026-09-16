// RUN: %clang_cc1 -triple mcs251 -Werror -emit-llvm -o - %s | FileCheck %s
// G11 §2.2/§3.2: the placement and retention policy of an entity is decided
// over the *complete* redeclaration chain at translation-unit end. Attributes
// that arrive on a later declaration must reach the entity that was already
// processed, and an internal definition that nothing references must still be
// emitted (it is a fixed entity), with the exact llvm.used membership.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define RETAIN __attribute__((mcu_retain))
// External definition first, placement/retain on a later declaration.
extern int before RETAIN;
int before PLACE(0x100) = 1;
int after = 2;
extern int after PLACE(0x200) RETAIN;
void fn(void) {}
void fn(void) PLACE(0x300) RETAIN;
// The same shape for internal definitions that are referenced by nothing at
// all: the entity, its placement attributes and its llvm.used keepalive must
// all exist at -O0 (an internal unused definition is not "not emitted" when it
// carries a fixed placement or a retention request).
static int late_object = 1;
extern int late_object PLACE(0x400) RETAIN;
static void late_fn(void) {}
static void late_fn(void) PLACE(0x500) RETAIN;
// A placed internal definition without retain is a fixed entity too, but it
// carries no retention request and therefore stays out of llvm.used.
static int late_plain = 2;
extern int late_plain PLACE(0x600);

// The entities exist with their placement/stable attributes. Each global is
// bound to its own attribute group number, so a placement string can only be
// satisfied by the attributes of that very global.
// CHECK: @before = {{.*}}global i32 1, align 1 #[[BEFORE:[0-9]+]]
// CHECK: @after = {{.*}}global i32 2, align 1 #[[AFTER:[0-9]+]]
// CHECK: @late_object = internal global i32 1, align 1 #[[LATEOBJ:[0-9]+]]
// CHECK: @late_plain = internal global i32 2, align 1 #[[LATEPLAIN:[0-9]+]]
// The keepalive container holds exactly the retained definitions: the two
// external ones, the late-placed internal object and the late-placed internal
// function. `late_plain` is placed but not retained, so it is not a member.
// The exact member set is asserted, not just the length: the declared count
// [5 x ptr] plus one named match per member exhausts the container, so a
// missing, extra or substituted member fails. The member order inside the
// container is not part of the contract and is not asserted.
// CHECK: @llvm.used = appending global [5 x ptr] [
// CHECK-DAG: ptr @before
// CHECK-DAG: ptr @after
// CHECK-DAG: ptr @late_object
// CHECK-DAG: ptr addrspacecast (ptr addrspace(4) @late_fn to ptr)
// CHECK-DAG: ptr addrspacecast (ptr addrspace(4) @fn to ptr)
// CHECK: ], section "llvm.metadata"
// The unreferenced internal definitions carry bodies, not just declarations.
// CHECK: define dso_local void @fn() addrspace(4) #[[FN:[0-9]+]]
// CHECK: define internal void @late_fn() addrspace(4) #[[LATEFN:[0-9]+]]
// CHECK: attributes #[[BEFORE]] = { "mcs251-place"="0x100,data,object,owned,1" "mcs251-stable-symbol"="before" }
// CHECK: attributes #[[AFTER]] = { "mcs251-place"="0x200,data,object,owned,1" "mcs251-stable-symbol"="after" }
// CHECK: attributes #[[LATEOBJ]] = { "mcs251-place"="0x400,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_redecl_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.late_object" }
// CHECK: attributes #[[LATEPLAIN]] = { "mcs251-place"="0x600,data,object,owned,0" "mcs251-stable-symbol"="mcs251_g11_redecl_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.late_plain" }
// CHECK: attributes #[[FN]] = {{.*}}"mcs251-place"="0x300,code,function,owned,1" "mcs251-stable-symbol"="fn"
// CHECK: attributes #[[LATEFN]] = {{.*}}"mcs251-place"="0x500,code,function,owned,1" "mcs251-stable-symbol"="mcs251_g11_redecl_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.late_fn"
