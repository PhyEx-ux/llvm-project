// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %s

// Negative matrix for the old-style MCS-251 Keil `sbit` declaration. Only the
// initializer's `^` is positioning syntax; malformed addresses, indices and
// declarators must be diagnosed rather than silently accepted.

// Base must be a bit-addressable SFR byte: 0x80-0xFF with low 3 bits zero.
// (Only the `BASE ^ INDEX` form has a base; the plain-address form accepts any
// bit address in 0-255, so `sbit G = 0x89;` is legal.)
sbit A = 0x20 ^ 3; // expected-error {{'sbit' base must be a bit-addressable SFR byte (0x80-0xFF with its low 3 bits zero)}}
sbit F = 0x20 ^ 1; // expected-error {{'sbit' base must be a bit-addressable SFR byte (0x80-0xFF with its low 3 bits zero)}}
sbit G = 0x89 ^ 1; // expected-error {{'sbit' base must be a bit-addressable SFR byte (0x80-0xFF with its low 3 bits zero)}}

// Index must be a constant in 0-7.
sbit C = 0x88 ^ 8;  // expected-error {{'sbit' bit index must be a constant in the range 0-7}}
sbit D = 0x88 ^ -1; // expected-error {{'sbit' bit index must be a constant in the range 0-7}}

// Plain bit-address form must stay in 0-255.
sbit E = 0x100; // expected-error {{'sbit' bit address 256 is outside the valid range 0-255}}
sbit Neg = -1;  // expected-error {{'sbit' bit address -1 is outside the valid range 0-255}}

// Initializer required and must be an integer constant expression.
sbit H; // expected-error {{'sbit' declaration requires a bit-address initializer}}
extern int runvar;
sbit I = runvar; // expected-error {{'sbit' initializer must be an integer constant expression}}

// Same-address redeclaration is fine; a conflicting one is not.
sbit J = 0x88 ^ 2;
sbit J = 0x88 ^ 2;
sbit K = 0x24;
sbit K = 0x25; // expected-error {{conflicting redeclaration of 'sbit' 'K' at a different bit address}}

// Only a plain identifier declarator is supported.
sbit *ptr;    // expected-error {{'sbit' declarator must be a plain identifier}}
sbit arr[4];  // expected-error {{'sbit' declarator must be a plain identifier}}
sbit fn(void); // expected-error {{'sbit' declarator must be a plain identifier}}

// Bounded recovery: the next well-formed declaration is still parsed.
sbit Bad = 0x20 ^ 3; // expected-error {{'sbit' base must be a bit-addressable SFR byte (0x80-0xFF with its low 3 bits zero)}}
sbit Good = 0x88 ^ 1;

// Block scope is not supported for the fixed reference identity.
void block_scope(void) {
  sbit L = 0x24; // expected-error {{'sbit' declaration must be at file scope with static storage}}
}

// Wide integer constants are range-checked before narrowing (see the
// -fforce-enable-int128 variant in mcs251-sbit-wide.c).
