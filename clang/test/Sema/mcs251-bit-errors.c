// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

// Semantic boundary negatives for the independent MCS-251 `__bit` type. A bit
// object is a value with no addressable, sized, laid-out or atomically
// accessible representation, and it cannot be placed in a non-default address
// space. These checks must also apply through typedef / typeof indirection.

typedef __bit BOOL;

// Address-of, and therefore pointers, are rejected (direct and via typedef).
void addr_of(__bit b) {
  __bit *p = &b;   // expected-error {{cannot form a pointer to MCS251 'bit' type '__bit'}} expected-error {{cannot take the address of an MCS251 'bit' object}}
  BOOL *q = &b;    // expected-error {{cannot form a pointer to MCS251 'bit' type 'BOOL' (aka '__bit')}} expected-error {{cannot take the address of an MCS251 'bit' object}}
  __bit *r;        // expected-error {{cannot form a pointer to MCS251 'bit' type '__bit'}}
  r = &b;          // expected-error {{cannot take the address of an MCS251 'bit' object}}
  (void)p; (void)q; (void)r;
}

// Pointer-to-bit parameters/returns are also rejected at the declarator.
void takes_ptr(__bit *p); // expected-error {{cannot form a pointer to MCS251 'bit' type '__bit'}}
__bit *returns_ptr(void); // expected-error {{cannot form a pointer to MCS251 'bit' type '__bit'}}

// Arrays of bit, including the parameter array form.
__bit global_array[4]; // expected-error {{array of MCS251 'bit' is not allowed}}
void array_param(__bit a[4]); // expected-error {{array of MCS251 'bit' is not allowed}}

// Struct/union fields and bit-fields.
struct S { __bit f; };   // expected-error {{MCS251 'bit' is not allowed in a struct or union field}}
union U { __bit u; };    // expected-error {{MCS251 'bit' is not allowed in a struct or union field}}
struct T { __bit bf : 1; }; // expected-error {{MCS251 'bit' is not allowed in a struct or union field}}

// Layout queries.
void layout(__bit b) {
  int a = sizeof(b);          // expected-error {{sizeof of MCS251 'bit' type is not allowed}}
  int c = sizeof(__bit);      // expected-error {{sizeof of MCS251 'bit' type is not allowed}}
  int d = _Alignof(__bit);    // expected-error {{alignof of MCS251 'bit' type is not allowed}}
  int e = __alignof__(BOOL);  // expected-error {{alignof of MCS251 'bit' type is not allowed}}
  (void)a; (void)c; (void)d; (void)e;
}

// Atomic bit objects.
_Atomic(__bit) atomic_bit;       // expected-error {{atomic MCS251 'bit' type is not allowed}}
_Atomic(BOOL) atomic_bit_alias;  // expected-error {{atomic MCS251 'bit' type is not allowed}}

// memcpy/memmove of bit objects requires taking their address, which is
// rejected at the &.
void copy(void) {
  BOOL a, b;
  __builtin_memcpy(&a, &b, 1); // expected-error {{cannot take the address of an MCS251 'bit' object}} expected-error {{cannot take the address of an MCS251 'bit' object}}
}

// A conflicting placement/address space is rejected, including via typedef.
__bit __attribute__((address_space(1))) xdata_bit; // expected-error {{MCS251 'bit' cannot be declared in a non-default address space}}
BOOL __attribute__((address_space(6))) xdata_alias; // expected-error {{MCS251 'bit' cannot be declared in a non-default address space}}

// typeof indirect construction keeps the bit identity and is still rejected.
void typeof_indirect(__bit b) {
  __typeof__(b) local;
  __typeof__(b) *p = &b; // expected-error {{cannot form a pointer to MCS251 'bit' type 'typeof (b)' (aka '__bit')}} expected-error {{cannot take the address of an MCS251 'bit' object}}
  (void)local; (void)p;
}
