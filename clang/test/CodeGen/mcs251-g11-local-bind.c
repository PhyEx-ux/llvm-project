// RUN: %clang_cc1 -triple mcs251 -Werror -emit-llvm -o - %s | FileCheck %s
// G11 §2.2: a `mcu_bind_at` declaration is itself a placement record; it
// carries external linkage and produces no storage, so an unreferenced
// block-scope declaration must still have an IR carrier at -O0. Block-scope
// declarations do not go through CodeGenModule::EmitGlobal, so without an
// explicit path the declaration and its placement attribute disappear at the
// frontend stage (this is not an optimizer lifetime question).
#define BIND(A) __attribute__((mcu_bind_at(A)))

void unreferenced(void) {
  extern int object BIND(0x100);
  extern __xdata int far_object BIND(0x2000);
  extern void function(void) BIND(0x200);
}

void referenced(void) {
  extern int object BIND(0x100);
  object = 1;
}

// The three declarations exist with their placement attributes and without
// storage: `external`/`declare`, never a definition.
// CHECK: @object = external global i32, align 1 #[[OBJ:[0-9]+]]
// CHECK: @far_object = external addrspace(3) global i32, align 1 #[[FAR:[0-9]+]]
// A bind declaration is not a keepalive root (retain is refused on bind), so
// no llvm.used container is created; it would be printed right here, between
// the globals and the function bodies.
// CHECK-NOT: @llvm.used
// CHECK: define{{.*}}void @unreferenced()
// CHECK: declare void @function() addrspace(4) #[[FN:[0-9]+]] align 4
// CHECK: attributes #[[OBJ]] = { "mcs251-place"="0x100,data,object,bind,0" "mcs251-stable-symbol"="object" }
// CHECK: attributes #[[FAR]] = { "mcs251-place"="0x2000,xdata,object,bind,0" "mcs251-stable-symbol"="far_object" }
// CHECK: attributes #[[FN]] = {{.*}}"mcs251-place"="0x200,code,function,bind,0" "mcs251-stable-symbol"="function"
