// RUN: %clang_cc1 -triple mcs251 -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c23 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -fsyntax-only -verify=nonmcs %s
//
// G11-N4 (§8.1 rule 4) top-level identity input boundary in C mode. In C no
// declaration is mangled except an __attribute__((overloadable)) function, so
// the top-level identity component E(D) of every ordinary C declaration is its
// identifier verbatim. Itanium mangled components always start with "_Z", and
// both kinds share the top-level domain, so a participating C declaration
// *named* "_Z..." would collide with the encoding of a different entity (this
// is the mechanism behind the 12/12 cross-domain collision of the sixth-round
// review, now closed at the top level as well as at the function-local host
// level).
//
// The boundary is scoped to entities that actually generate a G11 identity:
// a "_Z"-spelled C declaration with no G11 attribute is untouched, and an
// __attribute__((overloadable)) function whose identifier starts with "_Z" is
// mangled (`_Z` + length + name), so it is accepted -- its component is not a
// bare reserved name.
//
// On x86 the mcu_* attributes do not exist, so the whole file reduces to three
// "unknown attribute ignored" warnings and no MCS251 check runs.

// Rejected: owned object.
int _Z1fi __attribute__((mcu_place_at(0x100))) = 1; // expected-error {{MCS251 fixed placement: entity '_Z1fi' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Rejected: internal (static) object -- the <TU>.<E(D)> shape does not help,
// because E(D) itself is the colliding component.
static int _Z2gi __attribute__((mcu_place_at(0x110), mcu_retain)); // expected-error {{MCS251 fixed placement: entity '_Z2gi' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}} nonmcs-warning {{unknown attribute 'mcu_retain' ignored}}

// Rejected: bind declaration.
extern int _Z3hi __attribute__((mcu_bind_at(0x120))); // expected-error {{MCS251 fixed placement: entity '_Z3hi' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_bind_at' ignored}}

// Rejected: an asm label does not exempt the entity; the label never enters
// the identity, so the reserved component would still be encoded.
int _Z4ii __asm__("detached_slot") __attribute__((mcu_place_at(0x130))) = 4; // expected-error {{MCS251 fixed placement: entity '_Z4ii' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the declaration}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// The function-local case keeps its own, distinct diagnostic (the *host* has
// the reserved name, not the placed entity). It must not be replaced by the
// new top-level rule: the message and its attributions differ, and
// mcs251-g11-c-domain-cross.c exercises the same input from the CodeGen side.
void _Z5hosti(int a) { // expected-error {{MCS251 fixed placement: host function '_Z5hosti' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the host function}}
  static int x __attribute__((mcu_place_at(0x140), mcu_retain)); // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}} nonmcs-warning {{unknown attribute 'mcu_retain' ignored}}
}

// -------- controls (all accepted) --------

// No G11 attribute: the identifier is not policed.
void _Z6plain(void);
int _Z7plain_object;

// Overloadable: mangled to _Z<len>_Z7ovl_z, so the bare reserved name is
// never used as an identity component.
void __attribute__((overloadable)) _Z8ovl_z(int a) __attribute__((mcu_place_at(0x150))); // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}
void __attribute__((overloadable)) _Z8ovl_z(int a) {}

// Ordinary C placement keeps the bare identifier.
int normal_c __attribute__((mcu_place_at(0x160))) = 9; // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}