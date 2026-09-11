// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
// RUN:   -fsyntax-only -verify %s

// X1-3 (DESIGN.md B.4): the post-'*' qualifier names the storage of the
// *pointer object itself*, in contrast to the specifier position, which
// qualifies what the pointer designates. `char * xdata p` puts p in XDATA;
// `char xdata *p` points into XDATA. Both spellings parse and the automatic
// object ban applies to pointer objects too.

typedef unsigned char BYTE;

// Accepted file-scope forms, core and Keil spellings, next to the existing
// pointer-into-space form.
char * xdata p1;
char * __xdata p2;
char * code p3;
char * __code p4;
char xdata * into_xdata;
BYTE * xdata ptab[4];

// A qualifier after the * combines with an outer pointer: pp itself is
// default, and *pp is a pointer object in XDATA.
char * xdata * pp;

// Automatic storage: the pointer object itself cannot live in the space.
void auto_bad(void) {
  char * xdata l; // expected-error {{automatic variable qualified with an address space}}
}

// Two spaces in the same position are rejected.
char * xdata code both; // expected-error {{multiple address spaces specified for type}}

// The forms stay distinguishable: storing through the (writable) XDATA
// pointer object is fine, but the value-level address spaces still must not
// be crossed implicitly.
void conversions(char *d, char xdata *xp) {
  p1 = d;  // no error: the *value* is char *, and the XDATA object is writable
  into_xdata = (char xdata *)d;
  p1 = (char * xdata)d;
  d = xp;  // expected-error {{changes address space of pointer}}
  xp = p1; // expected-error {{changes address space of pointer}}
}
