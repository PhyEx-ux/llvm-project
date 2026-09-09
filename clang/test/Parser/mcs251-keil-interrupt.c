// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/main.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-array.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-funcptr.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-paren.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-using.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/recover-dunder-using.c

//--- main.c
#define SLOT 36
void a() interrupt 0 {}
void b() interrupt SLOT {}
void c() interrupt (48 + 1) {}
void ordinary();

int value interrupt 1; // expected-error {{interrupt suffix requires a function declarator}}
void bad() interrupt 7 {} // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void oldarg(x) interrupt 1 int x; {} // expected-error {{MCS251 interrupt function must have type void(void)}} expected-warning {{a function definition without a prototype is deprecated}}
void using_bad() interrupt 2 using 1 {} // expected-error {{using is not supported for MCS251 interrupt functions}}
void dunder_using_bad() interrupt 3 __using(1) {} // expected-error {{using is not supported for MCS251 interrupt functions}}

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
