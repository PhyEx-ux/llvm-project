// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify=keil %t/keil.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify=plain %t/plain.c
//
// WP4 B4: the recognized-but-unsupported Keil C dialect spellings. Before
// WP4 they were ordinary parse errors ("expected ';' after top level
// declarator" / "expected function body after function declarator"), which
// does not tell the user that the toolchain KNOWS the word. They now get a
// dedicated "recognized spelling, unsupported semantics" diagnostic, in the
// exact syntactic position where the word can only be the dialect
// construct.
//
// The two sections keep the -fmcs251-keil and plain C modes apart: the bare
// `using` FUNCTION SUFFIX is only recognized under the flag, so without it
// the spelling must keep its ordinary-identifier semantics (a plain parse
// error on the suffix, and a legal variable named `using`). The
// double-underscore spellings are target words and behave the same in both
// modes; they are covered in the keil section.
//
// The shapes are the exact ones WP1 measured in FUNCTIONAL-GAPS/repro/t17a..t17h.

//--- keil.c
// ------------------------------------------------ storage-word positions --
unsigned char __idata a; // keil-error {{'__idata' is a recognized MCS251 (Keil C) dialect spelling}}
unsigned char __pdata b; // keil-error {{'__pdata' is a recognized MCS251 (Keil C) dialect spelling}}
unsigned char __near c; // keil-error {{'__near' is a recognized MCS251 (Keil C) dialect spelling}}
unsigned char __far d; // keil-error {{'__far' is a recognized MCS251 (Keil C) dialect spelling}}
unsigned char v _at_ 0x30; // keil-error {{'_at_' is a recognized MCS251 (Keil C) dialect spelling}}
unsigned char __at(0x30) w; // keil-error {{'__at' is a recognized MCS251 (Keil C) dialect spelling}}

// ----------------------------------------------- function-suffix positions --
void f_using(void) using 1 { } // keil-error {{'using' is a recognized MCS251 (Keil C) dialect spelling}}
int f_reentrant(int x) __reentrant { return x; } // keil-error {{'__reentrant' is a recognized MCS251 (Keil C) dialect spelling}}
void f_banked(void) __banked { } // keil-error {{'__banked' is a recognized MCS251 (Keil C) dialect spelling}}

// ----------------------------------------------- ordinary-identifier uses --
// A dialect word used as a plain identifier with no following declarator
// name is NOT a misplaced qualifier and must keep its ordinary semantics.
// These lines carry no directive on purpose: any diagnostic here is
// unexpected and fails -verify.
int __idata = 5;
int using = 1;
int __near = 2;
int user(int __near) { return __near + __idata + using + __near; }
// The `__at` word is only a dialect spelling in the PLACEMENT form
// `__at ( CONST ) declarator-name`. A parenthesized declarator --
// `int __at(int);` -- is an ordinary function declaration named `__at`, and
// must keep compiling. This is the positive that pins the lookahead.
int __at(int);
int use_at_fn(void) { return __at(1); }

// --------------------------------------------- supported dialect spellings --
__xdata unsigned char xd;
__code const unsigned char cd = 1;
__bit bb;
void isr(void) interrupt 1 { xd = 1; }

//--- plain.c
// Without -fmcs251-keil the bare `using` suffix is not a dialect word: it
// stays an ordinary identifier. A variable named `using` is perfectly legal
// (checked first, so the errored function below cannot disturb it through
// parse recovery), while the suffix spelling fails with the ordinary
// "expected function body" parse error, NOT with the dialect diagnostic.
int using = 1;
int user(void) { return using; }
// The double-underscore spellings are target words in both modes, so the
// `__at` placement form is still a dialect diagnostic here; the parenthesized
// declarator form stays an ordinary declaration.
int __at(int);
int use_at_fn(void) { return __at(1); }
unsigned char __at(0x30) w; // plain-error {{'__at' is a recognized MCS251 (Keil C) dialect spelling}}

void f_using(void) using 1 { } // plain-error {{expected function body after function declarator}}
