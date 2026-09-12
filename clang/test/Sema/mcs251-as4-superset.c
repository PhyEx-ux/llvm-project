// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %s

// A1 (RUNTIME-AS-PTR-DESIGN-A.md §3-A1, §4.1/§4.2): the target declares a
// 32-bit AS0 generic data pointer a superset of the AS4 CODE data pointer, so
// AS4 -> AS0 implicit conversion is a normal C conversion. Everything else
// stays rejected: the relation is one-way (AS0 -> AS4 implicit stays an
// error), AS3 is NOT carried along, and explicit const/volatile is still
// discarded only with the ordinary C diagnostic.
//
// The default (no -mcs251-memory-contract) layout is the v2-shaped 32/8 AS0
// model; the explicit v1 and Tiny(16-bit) spellings live in
// mcs251-as4-superset-contract.c.

typedef unsigned char uint8;

char __code rom_table[4] = {1, 2, 3, 4};
char __code *romptr;
const char *cptr;
char *dptr;

extern volatile char __code vrom;
extern char __xdata xd[4];

// --- Positive: initialization, assignment, comparison ---------------------

const char *init_from_code = rom_table;   // AS4 array decays to AS4 pointer

void assign_from_code(void) {
  cptr = rom_table;
  cptr = romptr;
}

char __code *ret_code(void) { return rom_table; }

// Same-object pointer difference across the conversion is allowed.
long diff(void) { return (const char *)rom_table - cptr; }

int cmp_code_vs_plain(void) {
  return rom_table == dptr || rom_table != cptr;
}

// --- Positive: parameter passing -----------------------------------------

void takes_const_code_str(const char *s);
void call_with_code(void) { takes_const_code_str(rom_table); }

// --- Negative: AS0 -> AS4 implicit is still rejected ---------------------

void as0_to_as4_implicit(const char *s) {
  char __code *cp;
  cp = s; // expected-error {{changes address space of pointer}}
}

// --- Negative: AS3 is not carried along ----------------------------------

void as3_to_as0_implicit(void) {
  cptr = xd; // expected-error {{changes address space of pointer}}
}

// --- Negative: const/volatile are discarded only with the C diagnostic ----

void takes_plain(char *p); // expected-note {{passing argument to parameter 'p' here}}
void qualifier_drop(void) {
  takes_plain(rom_table); // expected-warning {{discards qualifiers}}

  char *p = rom_table; // expected-warning {{discards qualifiers}}
  (void)p;
}

// A volatile-qualified CODE object keeps its volatile through the AS
// conversion; the pointer value still cannot become a bare `char *` without
// discarding volatile, and the array name is not an object designator.
void volatile_drop(void) {
  // expected-error@+1 {{incompatible integer to pointer conversion}}
  char *p = vrom;
  (void)p;
}

// --- Explicit casts remain the supported crossing ------------------------

char *explicit_to_plain(void) { return (char *)rom_table; }
char __code *explicit_to_code(char *p) { return (char __code *)p; }

// --- Function pointers are not objects and are unaffected ---------------

typedef int (*ifp)(void);
int plain_fn(void) { return 0; }
void function_pointer_regression(void) {
  ifp f = plain_fn;
  (void)f;
}

// --- Negative: the relation does NOT lift to nested pointers --------------

void nested_negatives(char __code **ppc, char **pp) {
  pp = ppc; // expected-error {{changes address space of nested pointer}}
  ppc = pp; // expected-error {{changes address space of nested pointer}}
}

// --- Function pointers vs object pointers are still separate --------------

typedef void (*vfp)(void);
void fn_ptr_regression(void) {
  vfp f = 0;
  (void)f;
}
