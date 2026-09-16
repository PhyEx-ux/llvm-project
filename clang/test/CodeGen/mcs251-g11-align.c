// RUN: %clang_cc1 -triple mcs251 -Werror -emit-llvm -o - %s | FileCheck %s
// G11 §2.2: the `aligned` attribute of a placement entity is recorded on the
// individual declaration that carries it, so the IR alignment carrier must be
// the entity-level constraint over the whole redeclaration chain -- and it
// must be present whether the aligned declaration comes before or after the
// placement declaration. The Sema side of the same matrix is
// Sema/mcs251-g11-late-align.c.
#define BIND(A) __attribute__((mcu_bind_at(A)))
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define ALIGNED8 __attribute__((aligned(8)))
#define ALIGNED16 __attribute__((aligned(16)))

// bind / object: 8-aligned address, aligned on the later declaration.
extern int bind_late BIND(0x200);
extern int bind_late ALIGNED8;
// bind / object: aligned on the earlier declaration.
extern int bind_early ALIGNED16;
extern int bind_early BIND(0x300);
// place / object: definition first, aligned later.
int place_late PLACE(0x400);
extern int place_late ALIGNED8;
// place / object: aligned first.
extern int place_early ALIGNED16;
int place_early PLACE(0x500);
// bind / function.
extern void fn_bind_late(void) BIND(0x600);
extern void fn_bind_late(void) ALIGNED8;
// bind / function: the aligned declaration comes first.
extern void fn_bind_early(void) ALIGNED8;
extern void fn_bind_early(void) BIND(0x608);
// place / function (aligned must precede the definition).
extern void fn_place_late(void) ALIGNED16;
void fn_place_late(void) PLACE(0x700);
void fn_place_late(void) {}
// place / function: placement first, then aligned, then the definition (the
// true reverse declaration order of fn_place_late above).
extern void fn_place_early(void) PLACE(0x708);
extern void fn_place_early(void) ALIGNED8;
void fn_place_early(void) {}

// The alignment carrier is the entity's GlobalObject, in both orders and for
// all four cells of the function matrix.
// CHECK: @bind_late = external global i32, align 8 #[[BINDLATE:[0-9]+]]
// CHECK: @bind_early = external global i32, align 16 #[[BINDEARLY:[0-9]+]]
// CHECK: @place_late = {{.*}}global i32 0, align 8 #[[PLACELATE:[0-9]+]]
// CHECK: @place_early = {{.*}}global i32 0, align 16 #[[PLACEEARLY:[0-9]+]]
// CHECK: declare void @fn_bind_late() addrspace(4) #[[FNBIND:[0-9]+]] align 8
// CHECK: declare void @fn_bind_early() addrspace(4) #[[FNBINDEARLY:[0-9]+]] align 8
// CHECK: define{{.*}}void @fn_place_late() addrspace(4) #[[FNPLACE:[0-9]+]]{{.*}} align 16
// CHECK: define{{.*}}void @fn_place_early() addrspace(4) #[[FNPLACEEARLY:[0-9]+]]{{.*}} align 8
// CHECK: attributes #[[BINDLATE]] = { "mcs251-place"="0x200,data,object,bind,0" "mcs251-stable-symbol"="bind_late" }
// CHECK: attributes #[[BINDEARLY]] = { "mcs251-place"="0x300,data,object,bind,0" "mcs251-stable-symbol"="bind_early" }
// CHECK: attributes #[[PLACELATE]] = { "mcs251-place"="0x400,data,object,owned,0" "mcs251-stable-symbol"="place_late" }
// CHECK: attributes #[[PLACEEARLY]] = { "mcs251-place"="0x500,data,object,owned,0" "mcs251-stable-symbol"="place_early" }
// CHECK: attributes #[[FNBIND]] = {{.*}}"mcs251-place"="0x600,code,function,bind,0" "mcs251-stable-symbol"="fn_bind_late"
// CHECK: attributes #[[FNBINDEARLY]] = {{.*}}"mcs251-place"="0x608,code,function,bind,0" "mcs251-stable-symbol"="fn_bind_early"
// CHECK: attributes #[[FNPLACE]] = {{.*}}"mcs251-place"="0x700,code,function,owned,0" "mcs251-stable-symbol"="fn_place_late"
// CHECK: attributes #[[FNPLACEEARLY]] = {{.*}}"mcs251-place"="0x708,code,function,owned,0" "mcs251-stable-symbol"="fn_place_early"
