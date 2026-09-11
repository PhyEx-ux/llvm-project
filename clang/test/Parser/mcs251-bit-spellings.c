// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify=keil %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify=nokeil %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -x c++ -std=c++17 -fmcs251-keil -fsyntax-only -verify=cxx %s

// The bare Keil spellings `bit` and `sbit` are registered only under
// -fmcs251-keil, and (P08) only in C. Without the switch they are ordinary
// identifiers, so the declarations below fail as "unknown type name" /
// "undeclared identifier" (nokeil). With the switch the controlled grammar
// applies (keil). In C++ the tokens stay ordinary identifiers too, so the same
// declarations are rejected as unknown type names, not silently parsed as bit
// dialect.

// cxx-error@+2 {{unknown type name 'bit'}}
// nokeil-error@+1 {{unknown type name 'bit'}}
typedef bit BOOL;

// cxx-error@+2 {{unknown type name 'bit'}}
// nokeil-error@+1 {{unknown type name 'bit'}}
bit g;

// cxx-error@+2 {{unknown type name 'sbit'}}
// nokeil-error@+1 {{unknown type name 'sbit'}}
sbit S = 0x24;

void local(void) {
  // keil-error@+3 {{'sbit' declaration must be at file scope with static storage}}
  // cxx-error@+2 {{unknown type name 'sbit'}}
  // nokeil-error@+1 {{use of undeclared identifier 'sbit'}}
  sbit L = 0x24;
}
