// RUN: %clang_cc1 -triple mcs251 -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c23 -fsyntax-only -verify %s
// G11 §2.2: the placement address must satisfy the *entity-level* alignment
// constraint. The `aligned` attribute is recorded on the individual
// declaration that carries it, so a check that consults one selected
// declaration (the one holding the placement attribute, or the definition)
// gives an answer that depends on declaration order. Each entity below is
// written in both orders; the diagnostic is anchored on the placement
// attribute, which is the first declaration in the late variant.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))
#define ALIGNED4 __attribute__((aligned(4)))
#define ALIGNED8 __attribute__((aligned(8)))

// bind / object: aligned arrives either before or after the placement.
extern int bind_late BIND(0x201); // expected-error {{placement address 0x201 does not satisfy alignment 4}}
extern int bind_late ALIGNED4;
extern int bind_early ALIGNED4;
extern int bind_early BIND(0x241); // expected-error {{placement address 0x241 does not satisfy alignment 4}}

// place / object: the definition is the first declaration in both variants, so
// this is exactly the order the entity-level aggregation has to handle.
int place_late PLACE(0x281); // expected-error {{placement address 0x281 does not satisfy alignment 4}}
extern int place_late ALIGNED4;
extern int place_early ALIGNED4;
int place_early PLACE(0x2C1); // expected-error {{placement address 0x2C1 does not satisfy alignment 4}}

// bind / function. An MCS-251 function entry has a target alignment of 4, so
// the discriminating constraint here is `aligned(8)` on an address that is
// 4-aligned but not 8-aligned: a check that consults only the declaration
// carrying the placement attribute accepts it.
extern void fn_bind_late(void) BIND(0x304); // expected-error {{placement address 0x304 does not satisfy alignment 8}}
extern void fn_bind_late(void) ALIGNED8;
extern void fn_bind_early(void) ALIGNED8;
extern void fn_bind_early(void) BIND(0x30C); // expected-error {{placement address 0x30C does not satisfy alignment 8}}

// place / function. The aligned declaration must precede the definition (a
// later one is the generic "attribute declaration must precede definition"
// case) -- both orders below are legal spellings of the same entity.
void fn_place_late(void) PLACE(0x314); // expected-error {{placement address 0x314 does not satisfy alignment 8}}
extern void fn_place_late(void) ALIGNED8;
void fn_place_late(void) {}
extern void fn_place_early(void) ALIGNED8;
void fn_place_early(void) PLACE(0x31C); // expected-error {{placement address 0x31C does not satisfy alignment 8}}
void fn_place_early(void) {}

// Addresses that satisfy the entity-level constraint are accepted in both
// orders (no diagnostics on these lines).
extern int ok_late BIND(0x200);
extern int ok_late ALIGNED4;
extern int ok_early ALIGNED4;
extern int ok_early BIND(0x210);
int ok_place_late PLACE(0x220);
extern int ok_place_late ALIGNED4;
extern int ok_place_early ALIGNED4;
int ok_place_early PLACE(0x230);
extern void ok_fn_bind_late(void) BIND(0x400);
extern void ok_fn_bind_late(void) ALIGNED8;
extern void ok_fn_bind_early(void) ALIGNED8;
extern void ok_fn_bind_early(void) BIND(0x408);
void ok_fn_place_late(void) PLACE(0x410);
extern void ok_fn_place_late(void) ALIGNED8;
void ok_fn_place_late(void) {}
extern void ok_fn_place_early(void) ALIGNED8;
void ok_fn_place_early(void) PLACE(0x418);
void ok_fn_place_early(void) {}