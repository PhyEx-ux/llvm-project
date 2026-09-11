// RUN: %clang_cc1 -triple mcs251-unknown-none -x c++ -std=c++17 -fmcs251-keil -fsyntax-only -verify %s

// Like the bit/sbit dialect, the xdata/code qualifiers are C-only for this
// slice: in C++ neither the core spellings nor the bare Keil spellings are
// registered, so each qualifier token falls back to an ordinary identifier
// and the declaration fails as a plain parse error.

// expected-error@+1 {{expected ';'}}
char __code TAB;

// expected-error@+1 {{expected ';'}}
char __xdata XBUF;

// expected-error@+1 {{expected ';'}}
typedef int code CINT;

// expected-error@+1 {{expected ';'}}
typedef int xdata XINT;
