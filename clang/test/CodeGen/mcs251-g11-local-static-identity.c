// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O3 -emit-llvm -o - %s | FileCheck %s
// G11 §3.2 entity identity for function-local statics must be derived from the
// AST declaration structure only. Three scenarios where that is *not* the case
// if the identity splices in the emitted global name (the assembly name, or a
// LLVM symbol-table `.N` uniquing suffix) all live in this one TU, and every
// RUN level asserts the very same identity strings, so the identity is
// byte-identical across -O0/-O1/-O2/-O3:
//  1. asm-host: a top-level static and a local static whose assembly names
//     cross. The identity of the local is built from the declaration name
//     (`y`), never from the assembly label, so the two entities cannot collide
//     with each other (and stripping labels stays out of the scheme).
//  2. counter host: the same-named `x` statics of an available_externally
//     inline function are only emitted at -O1+, which shifts the module-wide
//     LLVM uniquing counter at -O0. The source-order ordinal of `x` inside
//     `cntr_host` (second occurrence -> ".2") is an AST property and does not
//     move.
//  3. deferred hosts: placement arrives on a late redeclaration, so the
//     function bodies and their statics are emitted from the deferred table in
//     table order (beta before alpha), not source order. The ordinal is
//     computed from the AST scan of each function body, not from the emission
//     sequence, so alpha/beta identities do not depend on that order either.
//  4. overloadable hosts: two C functions of one name marked
//     __attribute__((overloadable)) are distinct entities with distinct
//     mangled IR names (_Z8ovl_hosti / _Z8ovl_hostl, exactly what CGDecl
//     emits for their statics). The identity must follow the mangler's own
//     "needs mangling" verdict on the declaration (ItaniumMangle honors
//     OverloadableAttr in either language), not the language mode, or both
//     statics would collapse onto one `ovl_host.x` identity.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define RETAIN __attribute__((mcu_retain))

static int pinned __asm__("top_slot") PLACE(0x100) RETAIN;
void asm_host(void) {
  static int y __asm__("pinned") PLACE(0x200) RETAIN;
}

extern void consume(const int *);
extern inline __attribute__((gnu_inline)) void noise(void) {
  { static const int x = 1; consume(&x); }
  { static const int x = 2; consume(&x); }
}
static void cntr_host(void) {
  { static int x PLACE(0x400) RETAIN; }
  { static int x PLACE(0x410) RETAIN; }
}
void caller(void) { noise(); cntr_host(); }

static void alpha(void) {
  { static int x PLACE(0x500) RETAIN; }
  { static int x PLACE(0x510) RETAIN; }
}
static void beta(void) {
  { static int x PLACE(0x600) RETAIN; }
  { static int x PLACE(0x610) RETAIN; }
}
void alpha(void) PLACE(0x1000) RETAIN;
void beta(void) PLACE(0x2000) RETAIN;

void __attribute__((overloadable)) ovl_host(int a) {
  static int x PLACE(0x700) RETAIN;
}
void __attribute__((overloadable)) ovl_host(long a) {
  static int x PLACE(0x710) RETAIN;
}

// The LLVM uniquing suffix of the *IR global names* (".1"/".2"/".3"/".4" for
// the second same-named static of each function) legitimately differs with the
// optimization level and with the deferred emission order; it is matched as
// a plain numeric suffix. Only the stable-symbol strings are pinned exactly,
// and those are what G11-B turns into section names.
// CHECK: @"\01pinned" = internal global i32 0, align 1 #[[LOCY:[0-9]+]]
// CHECK: @_Z8ovl_hosti.x = internal global i32 0, align 1 #[[OX1:[0-9]+]]
// CHECK: @_Z8ovl_hostl.x = internal global i32 0, align 1 #[[OX2:[0-9]+]]
// CHECK: @"\01top_slot" = internal global i32 0, align 1 #[[TOPP:[0-9]+]]
// CHECK: @cntr_host.x = internal global i32 0, align 1 #[[HX1:[0-9]+]]
// CHECK: @cntr_host.x.{{[0-9]+}} = internal global i32 0, align 1 #[[HX2:[0-9]+]]
// CHECK: @beta.x = internal global i32 0, align 1 #[[BX1:[0-9]+]]
// CHECK: @beta.x.{{[0-9]+}} = internal global i32 0, align 1 #[[BX2:[0-9]+]]
// CHECK: @alpha.x = internal global i32 0, align 1 #[[AX1:[0-9]+]]
// CHECK: @alpha.x.{{[0-9]+}} = internal global i32 0, align 1 #[[AX2:[0-9]+]]
// The keepalive set is exactly the twelve retained entities (2 asm entities,
// 2 counter-host objects, 4 deferred-host objects, 2 deferred functions, and
// the 2 overloadable-host objects -- the *hosts themselves* are unplaced, so
// they are not kept alive, only their statics are).
// CHECK: @llvm.used = appending global [12 x ptr] [
// CHECK-DAG: ptr @"\01top_slot"
// CHECK-DAG: ptr @"\01pinned"
// CHECK-DAG: ptr @cntr_host.x,
// CHECK-DAG: ptr @cntr_host.x.{{[0-9]+}}
// CHECK-DAG: ptr @alpha.x,
// CHECK-DAG: ptr @alpha.x.{{[0-9]+}}
// CHECK-DAG: ptr @beta.x,
// CHECK-DAG: ptr @beta.x.{{[0-9]+}}
// CHECK-DAG: ptr @_Z8ovl_hosti.x,
// CHECK-DAG: ptr @_Z8ovl_hostl.x
// CHECK-DAG: ptr addrspacecast (ptr addrspace(4) @alpha to ptr)
// CHECK-DAG: ptr addrspacecast (ptr addrspace(4) @beta to ptr)
// CHECK: ], section "llvm.metadata"
// CHECK-DAG: define internal void @alpha() addrspace(4) #[[FA:[0-9]+]] align 4 {
// CHECK-DAG: define internal void @beta() addrspace(4) #[[FB:[0-9]+]] align 4 {
// Identities: the local static is `<TU>.<function>.<varname>` with the
// function and variable *declaration* names and, from the second same-named
// static of one function onwards, the source-order ordinal appended
// (".2"). The top-level `pinned` keeps the plain top-level rule (declaration
// name, no function component), so the asm crossing cannot collide.
// CHECK: attributes #[[LOCY]] = { "mcs251-place"="0x200,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.asm_host.y" }
// CHECK: attributes #[[OX1]] = { "mcs251-place"="0x700,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._Z8ovl_hosti.x" }
// CHECK: attributes #[[OX2]] = { "mcs251-place"="0x710,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._Z8ovl_hostl.x" }
// CHECK: attributes #[[TOPP]] = { "mcs251-place"="0x100,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.pinned" }
// CHECK: attributes #[[HX1]] = { "mcs251-place"="0x400,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.cntr_host.x" }
// CHECK: attributes #[[HX2]] = { "mcs251-place"="0x410,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.cntr_host.x.2" }
// CHECK: attributes #[[BX1]] = { "mcs251-place"="0x600,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.beta.x" }
// CHECK: attributes #[[BX2]] = { "mcs251-place"="0x610,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.beta.x.2" }
// CHECK: attributes #[[AX1]] = { "mcs251-place"="0x500,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.alpha.x" }
// CHECK: attributes #[[AX2]] = { "mcs251-place"="0x510,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.alpha.x.2" }
// CHECK-DAG: attributes #[[FA]] = {{.*}}"mcs251-place"="0x1000,code,function,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.alpha"{{.*}}
// CHECK-DAG: attributes #[[FB]] = {{.*}}"mcs251-place"="0x2000,code,function,owned,1" "mcs251-stable-symbol"="mcs251_g11_local_static_identity_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.beta"{{.*}}
