// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

// X1: `__code` maps the qualified object (or the object a pointer
// designates) onto target address space 4: read-only CODE data and the
// function address space (DESIGN.md B.1/B.2). CODE is read-only *by space*,
// so a store is rejected even without a C `const` on the type; the same rule
// holds through pointers into the space and for every RMW form.

char __code DEVICEDESC[18] = {
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

// Direct stores are rejected.
void store(unsigned i, char v) {
  DEVICEDESC[0] = v; // expected-error {{cannot store to an MCS251 '__code' object; the CODE address space is read-only}}
  // expected-error@+1 {{cannot store to an MCS251 '__code' object}}
  *romptr = v;
}

// Every RMW form writes too.
void rmw(unsigned i, char v) {
  DEVICEDESC[i] += v; // expected-error {{cannot store to an MCS251 '__code' object}}
  // expected-error@+1 {{cannot store to an MCS251 '__code' object}}
  ++DEVICEDESC[0];
  // expected-error@+1 {{cannot store to an MCS251 '__code' object}}
  romptr[1]--;
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
