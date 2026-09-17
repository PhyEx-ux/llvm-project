// G11-N1 blocking fix (2026-09-17): the G11 fixed-placement contract has no
// bit storage class. A P09 `bit` entity is object identity (a kind-1
// `.mcs251.bit` record / a symbolic BITADDR8 reference), never a byte
// object, so it can be neither placed nor bound:
//
//   * owned  (`place_at`) used to be accepted and then silently swallowed by
//     the emitter's bit dispatch -- a successful object carried only the
//     dynamic bit record, with no `.mcu.fixed.*` section and no
//     `.mcs251.placement` NOTE, so the user's constraint disappeared;
//   * bind   (`bind_at`) used to be accepted and encoded the bit handle as a
//     "DATA/object/bind size=1" NOTE, misrepresenting object identity as a
//     one-byte DATA object.
//
// Sema rejects the combination fail-closed instead of inventing a bit
// storage class (the design schema is unchanged) or swapping the emitter
// dispatch (which would emit bit identity as bytes). The rejection is
// diagnosed on every G11 attribute a bit entity can reach -- place, bind,
// retain and noinit -- so no combination slips through to the emitter.
// The emitter-side receiving face for hand-written IR is
// llvm/test/CodeGen/MCS251/placement-bit-defensive.ll.
//
// RUN: %clang_cc1 -triple mcs251 -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c23 -fsyntax-only -verify %s
// RUN: not %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=CODEGEN
//
// CODEGEN: error: fixed placement on a 'bit' entity 'placed' is not supported

#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))
#define RETAIN __attribute__((mcu_retain))

// Owned placement on a bit entity: constraint used to vanish.
__bit placed PLACE(0x30); // expected-error {{fixed placement on a 'bit' entity 'placed' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
// Bind placement on a bit entity: identity used to become a 1-byte DATA object.
extern __bit bound BIND(0x30); // expected-error {{fixed placement on a 'bit' entity 'bound' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
// retain requires a placed definition; on a bit entity the bit rejection wins.
__bit kept PLACE(0x40) RETAIN; // expected-error {{fixed placement on a 'bit' entity 'kept' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
// noinit rides a place_at definition; on a bit entity the bit rejection wins.
__bit warm PLACE(0x50) __attribute__((noinit)); // expected-error {{fixed placement on a 'bit' entity 'warm' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
// The reverse spelling order must be caught identically (attribute order
// independence is a frozen G11 property).
__bit warm_reverse __attribute__((noinit)) PLACE(0x60); // expected-error {{fixed placement on a 'bit' entity 'warm_reverse' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
// bind+retain on a bit entity is refused by the ATTRIBUTE HANDLER before the
// TU-final bit rule can run (mcu_bind_at / mcu_retain see each other on the
// same declaration and emit the frozen §8 retain-no-definition message).
// That pre-existing handler diagnostic is preserved unchanged: an error is
// produced, the declaration is invalid, and no object can be emitted. The
// bit-specific wording is not required here.
extern __bit bound_kept BIND(0x70) RETAIN; // expected-error {{`mcu::retain` requires a definition}}
extern __bit bound_kept_reverse RETAIN BIND(0x71); // expected-error {{`mcu::retain` requires a definition}}
// A G11 attribute that needs a placement is still refused on a bit entity
// before the "needs a placement" wording can obscure the real problem.
extern __bit only_retain RETAIN; // expected-error {{fixed placement on a 'bit' entity 'only_retain' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
__bit only_noinit __attribute__((noinit)); // expected-error {{fixed placement on a 'bit' entity 'only_noinit' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}
// place_at and bind_at on the same bit entity: the bit rejection preempts the
// place/bind incompatibility message; either way it is an error, never a
// successful object.
__bit both PLACE(0x80) BIND(0x90); // expected-error {{fixed placement on a 'bit' entity 'both' is not supported: a bit entity is object identity, not byte storage, and the placement contract has no bit storage class}}

// Reverse control: a bit entity WITHOUT any G11 attribute is untouched, and
// an ordinary placed object keeps the ordinary placement diagnostics path.
__bit plain_bit;
int fixed_object PLACE(0x100) = 1;
void fixed_function(void) PLACE(0x200);
void fixed_function(void) {}
// A function RETURNING a bit type is a function entity, not a bit object:
// placement on it keeps working (the predicate is the entity's own type, not
// a bit-returning signature).
__bit fixed_bit_function(void) PLACE(0x300);
__bit fixed_bit_function(void) { return 0; }