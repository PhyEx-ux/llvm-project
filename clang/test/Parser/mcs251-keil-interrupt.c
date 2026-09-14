// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/main.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-array.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-funcptr.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-paren.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-using.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-dunder-using.c
// G1-3: the Keil suffix is gated on -fmcs251-keil; without the option the
// suffix is not part of the language and the definition is rejected before
// any slot semantics apply.
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %t/no-keil.c

//--- main.c
#define SLOT 36
void a() interrupt 0 {}
void b() interrupt SLOT {}
void c() interrupt (48 + 1) {}
void ordinary();

int value interrupt 1; // expected-error {{interrupt suffix requires a function declarator}}
void bad() interrupt 7 {} // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void oldarg(x) interrupt 1 int x; {} // expected-error {{MCS251 interrupt function must have type void(void)}} expected-warning {{a function definition without a prototype is deprecated}}
void using_bad() interrupt 2 using 1 {} // expected-error {{using is not supported for MCS251 interrupt functions}}
void dunder_using_bad() interrupt 3 __using(1) {} // expected-error {{using is not supported for MCS251 interrupt functions}}

// G1-3: the suffix rules on the same integer values as the GNU attribute --
// the profile maximum, the reclassified 31/45/46 and the parenthesized high
// arithmetic form are accepted, while the transfer slot 13, the HeaderOnly
// 81 and the first out-of-profile number 127 keep the shared-constant
// diagnostic.
void highkeil() interrupt 126 {}
void relin2() interrupt 31 {}
void relp8() interrupt 45 {}
void relp9() interrupt 46 {}
void highparen() interrupt (100 + 2) {}
void transfer13() interrupt 13 {} // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void headeronly81() interrupt 81 {} // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void over127() interrupt 127 {} // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}

//--- no-keil.c
void k() interrupt 1 {} // expected-error {{expected function body after function declarator}}

//--- recover-array.c
// Bounded recovery: the malformed suffix operand stops at ';' and the
// following function is parsed as an independent definition.
void bad_var[2] interrupt (1; void next(void) {} // expected-error {{interrupt suffix requires a function declarator}} expected-error {{array has incomplete element type 'void'}}

//--- recover-funcptr.c
void (*bad_ptr)(void) interrupt (1; void next(void) {} // expected-error {{interrupt suffix requires a function declarator}}

//--- recover-paren.c
void bad(void) interrupt (1; void next(void) {} // expected-error {{expected ')'}} expected-note {{to match this '('}}

//--- recover-using.c
void bad(void) interrupt 1 using (1; void next(void) {} // expected-error {{using is not supported for MCS251 interrupt functions}} expected-error {{expected ')'}} expected-note {{to match this '('}}

//--- recover-dunder-using.c
void bad(void) interrupt 1 __using(1; void next(void) {} // expected-error {{using is not supported for MCS251 interrupt functions}} expected-error {{expected ')'}} expected-note {{to match this '('}}
