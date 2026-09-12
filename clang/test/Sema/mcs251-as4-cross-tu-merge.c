// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -mcs251-memory-contract=1,1,32,8,1 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -mcs251-memory-contract=1,2,32,8,1 -fsyntax-only -verify %s

// A2c declaration-merge diagnostics (RUNTIME-AS-PTR-DESIGN-A.md §2.7,
// §3-A2c): where the compiler can see two declarations of the same entity,
// an incompatible CODE/AS0 or const/volatile change must be diagnosed.
// The design explicitly refuses to demand these diagnostics from an ordinary
// linker, which cannot see C types; the cross-TU compile halves live in
// clang/test/CodeGen/mcs251-as4-cross-tu.c and this file covers the same-TU
// merges.
//
// The two run lines must produce identical diagnostics: the type rules do
// not depend on the AS0 member of the memory contract (v1 compat and v2
// 32-bit agree on AS4 -> AS0; the 16-bit negatives are covered by
// mcs251-as4-superset-contract.c).

typedef unsigned char uint8;

// The shared table shape from the cross-TU header, declared here so this test
// is self-contained.
extern const uint8 __code shared_rom[8];

// Declaring the same function once with the implicit-const CODE pointee and
// once with the explicit-const CODE pointee is the SAME type: clean.
uint8 merged_ok(const uint8 __code *p, unsigned i);
uint8 merged_ok(uint8 __code *p, unsigned i);

// Dropping the CODE address space to a generic pointer changes the type.
uint8 bad_read(const uint8 __code *p, unsigned i); // expected-note {{previous declaration is here}}
uint8 bad_read(const uint8 *p, unsigned i); // expected-error {{conflicting types for 'bad_read'}}

// Dropping explicit volatile changes the type.
uint8 cv_read(volatile uint8 __code *p); // expected-note {{previous declaration is here}}
uint8 cv_read(const uint8 __code *p); // expected-error {{conflicting types for 'cv_read'}}

// Dropping AS4/const from a table declaration changes the type (the
// compiler sees both declarations in this TU).
extern const uint8 __code code_table[8]; // expected-note {{previous declaration is here}}
extern uint8 code_table[8]; // expected-error {{redeclaration of 'code_table' with a different type}}

// The implicit const IS the explicit const: `uint8 __code` and
// `const uint8 __code` name the same type, so this redeclaration is clean.
extern const uint8 __code ro_table[8];
extern uint8 __code ro_table[8];

// The header-shaped `shared_rom` declaration rejects a mutable AS0
// redeclaration too.
extern uint8 shared_rom[8]; // expected-error {{redeclaration of 'shared_rom' with a different type}} expected-note@23 {{previous declaration is here}}

// Reversing the direction (AS0 declared first, AS4 second) is also an error.
extern uint8 plain_table[8]; // expected-note {{previous declaration is here}}
extern const uint8 __code plain_table[8]; // expected-error {{redeclaration of 'plain_table' with a different type}}

// A definition-side declaration that agrees in every aspect is accepted.
uint8 agreed_read(const uint8 __code *p, unsigned i);
uint8 agreed_read(uint8 __code *p, unsigned i);

void use_merges(void) {
  (void)merged_ok(shared_rom, 0);
  (void)agreed_read(shared_rom, 1);
}
