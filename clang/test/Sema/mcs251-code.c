// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

// X1: `__code` maps the qualified object (or the object a pointer
// designates) onto target address space 4: read-only CODE data and the
// function address space (DESIGN.md B.1/B.2).
//
// A2a (RUNTIME-AS-PTR-DESIGN-A.md §3-A2, DESIGN.md B.2.1.1): `__code` now
// *really implies `const`* for object types, so the ordinary C
// modifiable-lvalue rules fire for named objects and members ("cannot assign
// to variable ... with const-qualified type"), while the CODE-space store
// gate keeps covering the shapes the type system alone does not reach
// (stores through a pointer value into CODE, where the qualified object is
// not the lvalue). Both remain hard errors; which one fires is a property of
// the lvalue, not a weakening of the gate.

char __code DEVICEDESC[18] = { // expected-note 2 {{variable 'DEVICEDESC' declared const here}}
    0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
    0x34, 0x12, 0xEF, 0xCD, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00};

char __code *romptr;

// Loads, initializer reads and address-taking are legal: the object's
// address has pointer-to-AS4 type.
char read(unsigned i) { return DEVICEDESC[i]; }
char __code *take_addr(void) { return &DEVICEDESC[0]; }
void ptr_read(void) { char c = *romptr; (void)c; }
char __code *ret_ptr(void) { return DEVICEDESC; }
void takes(char __code *p);
void array_param(char __code tab[], unsigned n);

// Direct stores are rejected. A named const object is caught by the
// modifiable-lvalue check, and so is a store through a pointer whose pointee
// is the (now const) CODE object; the ++/-- forms keep the space gate.
void store(unsigned i, char v) {
  DEVICEDESC[0] = v; // expected-error {{cannot assign to variable 'DEVICEDESC' with const-qualified type}}
  *romptr = v; // expected-error {{read-only variable is not assignable}}
}

// Every RMW form writes too. ++/-- reach the MCS251 CODE-space gate (the
// increment/decrement path checks the space before the lvalue's CVR), so the
// dedicated '__code' store diagnostic stays live.
void rmw(unsigned i, char v) {
  DEVICEDESC[i] += v; // expected-error {{cannot assign to variable 'DEVICEDESC' with const-qualified type}}
  ++DEVICEDESC[0]; // expected-error {{cannot store to an MCS251 '__code' object}}
  romptr[1]--; // expected-error {{cannot store to an MCS251 '__code' object}}
}

// An AS4-qualified value return type is rejected; pointers into CODE are the
// supported signature form (see above).
// expected-error@+1 {{function return type may not be qualified with an MCS251 '__xdata' or '__code' address space}}
char __code bad_ret(void);

// Automatic storage cannot live in CODE.
// expected-error@+1 {{automatic variable qualified with an address space}}
void local(void) { char __code ltab[4]; }

// 'bit' has no address-space representation.
// expected-error@+1 {{MCS251 'bit' cannot be declared in a non-default address space}}
__code __bit bitc;

// Two address spaces on one declaration.
// expected-error@+1 {{multiple address spaces specified for type}}
char __code __xdata both;

// A2a: a store through a pointer VALUE into CODE is caught by the ordinary
// read-only lvalue check now that the pointee carries the implied const; the
// implicit const did not turn it into a silent write.
void pointer_form(char __code *p, char v) {
  p[0] = v; // expected-error {{read-only variable is not assignable}}
}
// A2a: the implicit const is part of the type, so even an explicit de-const
// cast cannot make a named CODE object assignable without first discarding
// the read-only property; here the outer lvalue is still read-only.
void explicit_deconst(unsigned char *p) {
  *(char __code *)p = 1; // expected-error {{read-only variable is not assignable}}
}

// A2a: const is preserved through typedefs and adds no spurious duplicate
// qualifier diagnostic; a typedef of a CODE object is itself const, and an
// explicit const or volatile on top of it must be accepted and kept.
typedef char __code CodeChar;
typedef const char __code ConstCodeChar;
const CodeChar a1;
ConstCodeChar a2;
volatile CodeChar a3;

// A2a orthogonal shapes: the pointer OBJECT in CODE is const (assignment to
// the pointer is rejected) but its pointee stays mutable; a pointer INTO
// CODE leaves the pointer object mutable while the pointee is const.
char * __code pobj; // expected-note {{variable 'pobj' declared const here}}
char __code *pcp;
void orth_obj(char *v) {
  pobj = v; // expected-error {{cannot assign to variable 'pobj' with const-qualified type}}
  *pobj = 'x'; // accepted: the pointee is an ordinary mutable char
}
void orth_pointee(char __code *v) {
  pcp = v; // accepted: the pointer object itself is not const
  *pcp = 'x'; // expected-error {{read-only variable is not assignable}}
}
