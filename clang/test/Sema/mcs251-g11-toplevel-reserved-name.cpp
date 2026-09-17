// RUN: %clang_cc1 -triple mcs251 -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fsyntax-only -verify=nonmcs %s
//
// G11-N4 (§8.1 rule 4) top-level identity input boundary. The top-level
// component E(D) of a declaration the mangler does not mangle is the
// identifier verbatim, and Itanium mangled components always start with "_Z",
// so an unmangled "_Z"-prefixed name that participates in G11 identity
// generation would collide with the encoding of a *different* entity. Sema
// refuses that input class in both language modes instead of encoding it --
// fail-closed, which is what keeps every accepted identity string unchanged.
//
// Which C++ declarations are unmangled is the mangler's own verdict
// (ItaniumMangleContextImpl::shouldMangleCXXName, measured, not assumed):
//   * an extern "C" declaration is not mangled;
//   * a *namespace-scope* variable with external linkage is not mangled
//     ("Variables at global scope are not mangled unless they have internal
//     linkage"), so `int _Z...;` at global scope is in the rejected class;
//   * a namespace-scope variable with internal linkage (a file-scope static)
//     IS mangled -- as `_ZL<len><identifier>` -- and so is a variable inside a
//     namespace (`_ZN1M...E`). Those are accepted, and their identity carries
//     the mangled component (the CodeGen test asserts both strings).
// An asm label is never an exemption: it does not enter the identity, so the
// boundary stays an AST property.
//
// Scoping: the check applies only to entities that actually participate in G11
// identity generation (they carry one of the four placement attributes), so a
// "_Z"-spelled extern "C" declaration without any G11 attribute is untouched.

// Rejected: unmangled extern "C" object with a G11 attribute.
extern "C" int _Z1fi __attribute__((mcu_place_at(0x100))) = 1; // expected-error {{MCS251 fixed placement: entity '_Z1fi' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Rejected: unmangled extern "C" bind declaration.
extern "C" { extern int _Z2gi __attribute__((mcu_bind_at(0x110))); } // expected-error {{MCS251 fixed placement: entity '_Z2gi' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_bind_at' ignored}}

// Rejected: unmangled extern "C" function. Placement on a function needs a
// definition in the chain (the ordinary §2.2 rule), which is why the attribute
// rides the declaration and the definition below carries none.
extern "C" void _Z3hi(void) __attribute__((mcu_place_at(0x120))); // expected-error {{MCS251 fixed placement: entity '_Z3hi' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}
extern "C" void _Z3hi(void) {}

// Rejected: the asm label does not exempt the entity -- the identity is built
// from the declaration, never from the label, so the reserved component would
// still be encoded even though the ELF symbol would be `detached_slot`.
extern "C" int _Z4ii __asm__("detached_slot") __attribute__((mcu_place_at(0x130))) = 4; // expected-error {{MCS251 fixed placement: entity '_Z4ii' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Rejected: a namespace-scope variable with *external* linkage is not mangled
// either, so a "_Z"-prefixed C++ global is in the same rejected class. This is
// the C++-language-mode counterpart of the C file-scope case in
// CodeGen/mcs251-g11-c-domain-cross.c.
int _Z5ji __attribute__((mcu_place_at(0x140))) = 5; // expected-error {{MCS251 fixed placement: entity '_Z5ji' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Rejected with two attributes on one entity: still one input boundary on
// mcs251 (and two independent ignored attributes on x86).
int _Z6ki __attribute__((mcu_place_at(0x150), mcu_retain)); // expected-error {{MCS251 fixed placement: entity '_Z6ki' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}} nonmcs-warning {{unknown attribute 'mcu_retain' ignored}}

// -------- controls (all accepted) --------

// A "_Z"-spelled extern "C" declaration that does NOT participate in G11
// identity generation is untouched: the rule rejects inputs, it does not
// police identifiers.
extern "C" void _Z7plain_extern_c(void);
extern "C" int _Z8plain_extern_c_object;

// Accepted: a file-scope static is mangled in C++ (`_ZL9_Z9static`), so the
// unmangled reserved-name domain is not entered.
static int _Z9static __attribute__((mcu_place_at(0x160), mcu_retain)); // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}} nonmcs-warning {{unknown attribute 'mcu_retain' ignored}}

// Accepted: a namespace-scope variable is mangled (`_ZN1M6_Z10nsE`).
namespace M { int _Z10ns __attribute__((mcu_place_at(0x170))) = 10; } // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Accepted: ordinary extern "C" placement keeps the bare identifier.
extern "C" int normal_c_linkage __attribute__((mcu_place_at(0x180))) = 11; // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Accepted: ordinary C++ placement keeps the mangled component.
namespace N { int normal_namespace __attribute__((mcu_place_at(0x190))) = 12; } // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}