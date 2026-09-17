// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O3 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O0 -emit-llvm -o - %s | grep -c '"mcs251-stable-symbol"="[^"]*1[CD]12_GLOBAL__N_16anon_xE"' | FileCheck %s --check-prefixes=ANON-COUNT
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O0 -emit-llvm -o - %s | grep -o '"mcs251-stable-symbol"="[^"]*"' | sort > %t.O0
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O1 -emit-llvm -o - %s | grep -o '"mcs251-stable-symbol"="[^"]*"' | sort > %t.O1
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O2 -emit-llvm -o - %s | grep -o '"mcs251-stable-symbol"="[^"]*"' | sort > %t.O2
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -O3 -emit-llvm -o - %s | grep -o '"mcs251-stable-symbol"="[^"]*"' | sort > %t.O3
// RUN: diff %t.O0 %t.O1
// RUN: diff %t.O0 %t.O2
// RUN: diff %t.O0 %t.O3
// RUN: grep -c '"mcs251-stable-symbol"' %t.O0 | FileCheck %s --check-prefixes=IDENT-COUNT
//
// G11-N4 (§8.1) top-level identity component E(D) for C++ declarations: the
// top-level component of a declaration the Itanium mangler does mangle is the
// pure Itanium encoding, so namespace/class context, overload signature and
// concrete template arguments all separate otherwise-colliding entities. The
// pre-revision scheme used the bare declaration name for every external
// entity, under which A::x/B::x, f(int)/f(long) and C::{anonymous}::anon_x /
// D::{anonymous}::anon_x all collapsed onto one identity (measured: two "x",
// two "f", two "<TU>.anon_x").
//
// Two independent kinds of proof, because neither alone is sufficient:
//   * per-entity CHECK-DAG lines pair the exact placement string with the exact
//     identity, so a stable symbol can only be satisfied by the attributes of
//     the entity that owns that address, and distinctness is asserted by the
//     literals themselves rather than by counting records;
//   * the four `grep -o | sort > %t.O<n>` RUNs capture the COMPLETE identity
//     strings of every entity, and the three `diff` RUNs require that whole set
//     to be byte-identical across -O0/-O1/-O2/-O3. Matching an 8-hex-digit
//     pattern per RUN would NOT prove that: it tolerates a drifting TU hash.
//     IDENT-COUNT additionally pins how many identities must be present, so the
//     comparison cannot pass by comparing empty files, and an entity silently
//     disappearing at one optimization level is caught.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))
#define RETAIN __attribute__((mcu_retain))

// Namespace-scope objects: the namespace is part of E(D). Both directions are
// covered -- the bind path installs the identity from the declaration global
// rather than from a definition, so it is a separate code path.
namespace A { int x PLACE(0x100) = 1; }
namespace B { int x PLACE(0x110) = 2; }
namespace A { extern int y BIND(0x120); }
namespace B { extern int y BIND(0x130); }

// Overloaded functions: the overload signature is part of E(D). Placement on
// definitions arrives through a declared redeclaration (the GNU spelling is
// not accepted on a function *definition* by -Wgcc-compat).
int f(int a) PLACE(0x200);
int f(long a) PLACE(0x210);
int f(int a) { return a; }
int f(long a) { return (int)a; }

// The same for the bind direction: two bind declarations of one overloaded
// name are two entities, so their identities must carry the signature too.
int h(int a) BIND(0x220);
int h(long a) BIND(0x230);

// Concrete explicit specializations are single strong entities with concrete
// template arguments; their identities carry them, so different template
// arguments give different identities. Both a function template and a variable
// template are covered.
template <class T> T tfn(T v);
template <> int tfn<int>(int v) PLACE(0x300);
template <> int tfn<int>(int v) { return v; }
template <> long tfn<long>(long v) PLACE(0x310);
template <> long tfn<long>(long v) { return v; }
template <class T> T tvar;
template <> int tvar<int> PLACE(0x320) = 7;
template <> long tvar<long> PLACE(0x330) = 8;

// Anonymous namespaces: the enclosing namespace is still encoded, so the same
// source spelling under different outer namespaces is two entities. Reopening
// the anonymous namespace in the same scope declares the *same* entity, so no
// second identity may appear for `anon_x`. Both are retained: an unreferenced
// internal global is dead-code-eliminated at -O1+, which would drop the object
// (and its attributes) from the module and make the optimization-level
// comparison vacuous instead of stable. (Same reason the local-static identity
// test retains its entities.)
namespace C { namespace { int anon_x PLACE(0x400) RETAIN = 4; } }
namespace D { namespace { int anon_x PLACE(0x410) RETAIN = 5; } }
namespace C { namespace { extern int anon_x; } }

// extern "C" declarations are not mangled and carry no language-mode tag, so
// they keep the bare identifier (the cross-language equality with the C
// counterpart is asserted by mcs251-g11-toplevel-cross-language.c).
extern "C" { int c_link PLACE(0x500) = 6; }
extern "C" { extern int c_bind BIND(0x510); }

// A namespace-scope bind declaration is a top-level entity all the same.
namespace E { extern int b BIND(0x520); }

// Second overload pair kept separate from `f` to prove the identity does not
// depend on which overloads happen to be declared earlier in the TU.
int g(int a) PLACE(0x600);
int g(long a) PLACE(0x610);
int g(int a) { return a; }
int g(long a) { return (int)a; }

// Accepted controls for the §8.1 input boundary: the identifier of these two
// entities starts with "_Z" but the mangler still encodes them (a C++
// file-scope static is mangled as `_ZL<len><identifier>`; a namespace-scope
// variable as `_ZN1M6_Z10nsE`), so the unmangled reserved-name domain is never
// entered. The identity carries the mangled component, not the bare identifier.
static int _Z9static PLACE(0x700) RETAIN;
namespace M { int _Z10ns PLACE(0x710) = 11; }

// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="_ZN1A1xE" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x110,data,object,owned,0" "mcs251-stable-symbol"="_ZN1B1xE" }
// Namespace-scope *bind* pair: same variable name, same ownership direction,
// different namespace -- the two identities must differ (and they are the only
// entities at these two addresses, so the pairing is exclusive).
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x120,data,object,bind,0" "mcs251-stable-symbol"="_ZN1A1yE" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x130,data,object,bind,0" "mcs251-stable-symbol"="_ZN1B1yE" }
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x200,code,function,owned,0" "mcs251-stable-symbol"="_Z1fi"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x210,code,function,owned,0" "mcs251-stable-symbol"="_Z1fl"{{.*}}
// C++ bind overload pair: the signature separates the two bind declarations.
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x220,code,function,bind,0" "mcs251-stable-symbol"="_Z1hi"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x230,code,function,bind,0" "mcs251-stable-symbol"="_Z1hl"{{.*}}
// Concrete template arguments are part of the identity: the two explicit
// specializations of one function template, and of one variable template, are
// pairwise different identities (a scheme that keyed on the template *name*
// would collapse each pair).
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x300,code,function,owned,0" "mcs251-stable-symbol"="_Z3tfnIiET_S0_"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x310,code,function,owned,0" "mcs251-stable-symbol"="_Z3tfnIlET_S0_"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x320,data,object,owned,0" "mcs251-stable-symbol"="_Z4tvarIiE" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x330,data,object,owned,0" "mcs251-stable-symbol"="_Z4tvarIlE" }
// The two anonymous-namespace entities share one TU (same 8 hex digits) and one
// entity component shape; only the outer namespace differs. The mangled
// component is one field of a three-field identity, so the two strings are
// distinct even though the variable name is the same.
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x400,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_toplevel_identity_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._ZN1C12_GLOBAL__N_16anon_xE" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x410,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_toplevel_identity_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._ZN1D12_GLOBAL__N_16anon_xE" }
// The reopened anonymous namespace must not produce a third identity: exactly
// two anonymous-namespace identities exist in the whole module (C's and D's),
// asserted by the ANON-COUNT RUN.
// ANON-COUNT: 2
// 21 entities are asserted above; the byte-comparison RUNs must see all of
// them at every optimization level.
// IDENT-COUNT: 21
// Unmangled (extern "C") entities: the bare identifier, no language tag.
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x500,data,object,owned,0" "mcs251-stable-symbol"="c_link" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x510,data,object,bind,0" "mcs251-stable-symbol"="c_bind" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x520,data,object,bind,0" "mcs251-stable-symbol"="_ZN1E1bE" }
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x600,code,function,owned,0" "mcs251-stable-symbol"="_Z1gi"{{.*}}
// CHECK-DAG: attributes #{{[0-9]+}} = {{.*}}"mcs251-place"="0x610,code,function,owned,0" "mcs251-stable-symbol"="_Z1gl"{{.*}}
// The "_Z"-spelled but mangled controls: the mangled component is used, so the
// bare reserved identifier never appears in any identity.
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x700,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_toplevel_identity_cpp.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._ZL9_Z9static" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x710,data,object,owned,0" "mcs251-stable-symbol"="_ZN1M6_Z10nsE" }
