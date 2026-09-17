// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o %t.c.ll %S/Inputs/mcs251-g11-toplevel-cross-language.c
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -emit-llvm -o %t.cpp.ll %s
// RUN: grep -o '"mcs251-stable-symbol"="[^"]*"' %t.c.ll | sort > %t.c.stables
// RUN: grep -o '"mcs251-stable-symbol"="[^"]*"' %t.cpp.ll | sort > %t.cpp.stables
// RUN: diff %t.c.stables %t.cpp.stables
// RUN: grep -c '"mcs251-stable-symbol"' %t.c.stables | FileCheck %s --check-prefixes=COUNT
// RUN: FileCheck %s --check-prefixes=C-SIDE < %t.c.ll
// RUN: FileCheck %s --check-prefixes=CPP-SIDE < %t.cpp.ll
// RUN: FileCheck %s < %t.cpp.stables
//
// G11-N4 (§8.1 rule 3): the top-level identity component of a declaration the
// mangler does not mangle is the identifier verbatim with NO language-mode
// tag, so a plain C declaration and the corresponding C++ extern "C"
// declaration share one component.
//
// The roles are genuinely SWAPPED between the two TUs -- the C side owns
// `c_owned` and binds `cpp_owned`, while this C++ side binds `c_owned` and
// owns `cpp_owned`. That covers both required directions (C owned vs C++ bind
// and C bind vs C++ owned). If the component carried a language tag (or were
// derived from the language mode in any other way), the two TUs' identity sets
// would differ and the `diff` RUN would fail.
//
// The `diff` RUN compares the COMPLETE identity strings of both sides
// byte-for-byte; the two FileCheck RUNs additionally assert, per side, the
// exact (address, storage class, entity kind, ownership) tuple paired with the
// identity of that very entity. Without that ownership assertion a test whose
// sides had accidentally been written with the *same* ownership would still
// pass the identity comparison; with it, such a mistake fails.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))

// Opposite of the C side: bind here, owned there.
extern "C" { extern int c_owned BIND(0x100); }
extern "C" { int cpp_owned PLACE(0x200) = 2; }

// Exactly two identities, one per entity (also rules out a swap that accidentally
// emitted an extra entity on one side).
// COUNT: 2
// C side: c_owned owned, cpp_owned bind.
// C-SIDE-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="c_owned" }
// C-SIDE-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x200,data,object,bind,0" "mcs251-stable-symbol"="cpp_owned" }
// This side: c_owned bind, cpp_owned owned -- the same two identities, the
// opposite ownership.
// CPP-SIDE-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x100,data,object,bind,0" "mcs251-stable-symbol"="c_owned" }
// CPP-SIDE-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x200,data,object,owned,0" "mcs251-stable-symbol"="cpp_owned" }
// The identity sets are equal (the `diff` RUN) and each identity is a bare
// identifier with no language tag.
// CHECK-DAG: "mcs251-stable-symbol"="c_owned"
// CHECK-DAG: "mcs251-stable-symbol"="cpp_owned"