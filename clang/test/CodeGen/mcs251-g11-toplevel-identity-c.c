// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O3 -emit-llvm -o - %s | FileCheck %s
//
// G11-N4 (§8.1) top-level identity component E(D) in C mode.
//
// Plain C top-level entities are not mangled, so their identity stays the bare
// declaration name -- byte-identical to the pre-revision scheme (the placement
// compatibility test pins the whole set of those strings; this file pins the
// two C-only cases that the scheme *does* change or must newly reject).
//
//   * __attribute__((overloadable)) is the one C construct for which the
//     Itanium mangler's shouldMangleCXXName fires, so two same-named
//     overloadable functions are two entities and their identities must be the
//     mangled encodings. Pre-revision both were the bare name (`cg` twice for
//     the object case, `g` twice in the measured probe); the scheme now yields
//     `_Z2cgi` / `_Z2cgl`, which is also what CodeGen emits as their IR names.
//   * an unmangled C declaration whose identifier starts with "_Z" is in the
//     same reserved domain as an Itanium encoding and is rejected by Sema for
//     participating entities (see mcs251-g11-toplevel-c-reserved-name.c and
//     the c-domain-cross migration).
//
// Every CHECK pairs an exact address with an exact identity, so an identity
// can only be satisfied by the entity that owns that address; all four RUN
// levels assert the same strings (byte-stable across -O0/-O1/-O2/-O3).
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))

// Two overloadable functions of one name: distinct entities, distinct
// identities, in both the owned and the bind direction.
void __attribute__((overloadable)) cg(int a) PLACE(0x100);
void __attribute__((overloadable)) cg(long a) PLACE(0x110);
void __attribute__((overloadable)) cg(int a) {}
void __attribute__((overloadable)) cg(long a) {}
extern void __attribute__((overloadable)) cb(int a) BIND(0x120);
extern void __attribute__((overloadable)) cb(long a) BIND(0x130);

// Plain C: unchanged bare declaration name, owned and bind.
int plain_owned PLACE(0x200) = 7;
extern int plain_bound BIND(0x210);

// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x100,code,function,owned,0" "mcs251-stable-symbol"="_Z2cgi"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x110,code,function,owned,0" "mcs251-stable-symbol"="_Z2cgl"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x120,code,function,bind,0" "mcs251-stable-symbol"="_Z2cbi"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x130,code,function,bind,0" "mcs251-stable-symbol"="_Z2cbl"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x200,data,object,owned,0" "mcs251-stable-symbol"="plain_owned" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x210,data,object,bind,0" "mcs251-stable-symbol"="plain_bound" }