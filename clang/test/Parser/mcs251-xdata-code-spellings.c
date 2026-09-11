// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify=keil %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify=nokeil %s
// keil-no-diagnostics

// Registration matrix for the xdata/code address space qualifiers (X1):
//  * the core spellings `__xdata`/`__code` are MCS251 target keywords and
//    parse with or without -fmcs251-keil (the declarations without a
//    nokeil expectation below);
//  * the bare Keil spellings `xdata`/`code` parse only under -fmcs251-keil;
//    without the switch they stay ordinary identifiers, so the same source
//    lines fail as parse errors (nokeil). This mirrors the bit/sbit dialect
//    registration.

// Core spellings work in both runs, in every accepted position.
char __code TAB[4] = {1, 2, 3, 4};
char __xdata XBUF[4];
char __xdata *XP;
char __code *CP;
void send(char __xdata *p);
typedef char __xdata XBYTE;
typedef char __code CBYTE;

// Bare spellings parse as qualifiers in the keil run only. Leading position
// (before the type specifier) fails as an unknown type name without the
// dialect; trailing position (after the type specifier or a typedef name)
// fails as an expected ';'.
// nokeil-error@+1 {{unknown type name 'xdata'}}
xdata int xobj;

// nokeil-error@+1 {{unknown type name 'code'}}
code int cobj;

// nokeil-error@+1 {{expected ';'}}
char xdata cx;

// nokeil-error@+1 {{expected ';'}}
char code cc;

// nokeil-error@+1 {{expected ';'}}
extern char xdata big[256];

// nokeil-error@+1 {{expected ';'}}
extern char code romtab[8];

// nokeil-error@+2 {{expected ')'}}
// nokeil-note@+1 {{to match this '('}}
void takes(char xdata buf[], unsigned n);
