// R4a (G5 6.2): a same-TU prototype/definition mismatch on the address
// space of a pointer parameter is diagnosed by the front end as a
// conflicting-type error, in both directions and regardless of whether the
// AS0 or AS3 side comes first. This is the only enforceable layer: the
// cross-TU variant of the same mismatch links with zero diagnostics
// because the v2 signature record carries no address-space dimension
// (known P-4 blind spot; probe evidence GAP-G5-PROBES/rev1/r3ctu/
// mismatch.map). The call-site form below (AS0 prototype fed an AS3
// actual) is the TU-internal shape of that cross-TU mismatch.

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

typedef unsigned char BYTE;

// Negative: AS3 prototype vs AS0 definition.
void take_xdata(BYTE __xdata *p); // expected-note {{previous declaration is here}}
// expected-error@+1 {{conflicting types for 'take_xdata'}}
void take_xdata(BYTE *p) { p[0] = 0; }

// Negative: AS0 prototype vs AS3 definition (mirror direction).
void take_plain(BYTE *p); // expected-note {{previous declaration is here}}
// expected-error@+1 {{conflicting types for 'take_plain'}}
void take_plain(BYTE __xdata *p) { p[0] = 0; }

// Negative: definition first, conflicting AS0 redeclaration after.
void def_first(BYTE __xdata *p) { p[0] = 0; } // expected-note {{previous definition is here}}
// expected-error@+1 {{conflicting types for 'def_first'}}
void def_first(BYTE *p);

// Negative: AS3 vs AS4 (__code) on a parameter is the same conflict.
void code_proto(BYTE __xdata *p); // expected-note {{previous declaration is here}}
// expected-error@+1 {{conflicting types for 'code_proto'}}
void code_proto(BYTE __code *p) { (void)p[0]; }

// Negative: the mismatch matters in any parameter position, not only the
// first.
void second_param(BYTE *p, BYTE __xdata *q); // expected-note {{previous declaration is here}}
// expected-error@+1 {{conflicting types for 'second_param'}}
void second_param(BYTE *p, BYTE *q) { p[0] = q[0]; }

// Negative: AS0 prototype called with an AS3 actual -- the argument is
// rejected here even though a cross-TU link of the same shape is silent.
BYTE __xdata DmaTxBuffer[16];
void fill_plain(BYTE *p); // expected-note {{passing argument to parameter 'p' here}}
// expected-error@+1 {{changes address space of pointer}}
void call_mismatch(void) { fill_plain(&DmaTxBuffer[0]); }

// Positive control: a consistent AS3 declaration group -- repeated
// prototype, definition, and an AS3-actual call all agree, and the
// definition writes through the AS3 parameter without diagnostics.
void consistent(BYTE __xdata *p, BYTE v, BYTE __xdata *q);
void consistent(BYTE __xdata *p, BYTE v, BYTE __xdata *q);
void call_consistent(BYTE __xdata *src) { consistent(src, 1, &DmaTxBuffer[0]); }
void consistent(BYTE __xdata *p, BYTE v, BYTE __xdata *q) {
  p[0] = v;
  q[0] = v;
}
