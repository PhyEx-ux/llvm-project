// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -mcs251-memory-contract=1,1,32,8,1 -fsyntax-only -verify=compat %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -mcs251-memory-contract=1,2,32,8,1 -fsyntax-only -verify=small %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -mcs251-memory-contract=1,2,16,1,1 -fsyntax-only -verify=tiny %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -mcs251-memory-contract=1,2,16,8,1 -fsyntax-only -verify=tiny %s

// A1 memory-model matrix (RUNTIME-AS-PTR-DESIGN-A.md §1.3, §3-A1): the
// declared AS0 superset of AS4 is conditional on the *default pointer being
// 32 bits wide*. Under the v1 compatibility layout and the v2 32-bit AS0
// models (Small/XSmall/Large), AS4 -> AS0 converts; under Tiny/XTiny
// (16-bit AS0) it stays rejected, because a 16-bit container cannot carry the
// CODE bank and truncation must never be silent.
//
// compat/small: the whole file is diagnostic-free.
// tiny:         every AS4 -> AS0 line is an error.

// compat-no-diagnostics
// small-no-diagnostics

char __code rom[4] = {1, 2, 3, 4};
char __code *romptr;
const char *cptr;

void convert_array(void) {
  // tiny-error@+1 {{changes address space of pointer}}
  cptr = rom;
}

void convert_pointer(char __code *p) {
  // tiny-error@+1 {{changes address space of pointer}}
  cptr = p;
}

// Reading through the converted alias is the same AS0 read.
char read_via_plain(void) { return cptr[0]; }

// The explicit cast to a plain char* is legal in every model (it does not
// depend on the superset relation); it drops the AS4 space but not CVR.
char *explicit_plain(void) { return (char *)rom; }

// The explicit reverse cast (plain -> CODE) is likewise available in every
// model; it is a representation pass-through whose validity is contractual.
char __code *explicit_code(char *p) { return (char __code *)p; }
