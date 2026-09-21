// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c++20 -fsyntax-only -verify %s
//
// WP4 revision round 7 (R1): the A2 absolute-address pointer-initializer rule
// at the C++ SOURCE layer, and in particular for a class whose aggregate
// initializer list starts with a BASE SUBOBJECT.
//
// Round 6 fixed the C walker's subobject pairing: it now skips unnamed
// bitfields and consumes one semantic initializer per base before pairing the
// rest with the fields. That pairing is correct and is exercised here as well
// -- but it was NOT the reason the C++ base shape passed `-fsyntax-only`. A2's
// only source-level call site was inside Sema::CheckForConstantInitializer,
// which C++ deliberately does not run ("C++ does not restrict the
// initializer"), and both C++ branches there are no-ops. The source layer was
// therefore silent for EVERY C++ static pointer initializer -- base-class or
// not, aggregate or not -- while the IR and object layers rejected the
// absolute ones. Round 7 adds the C++ call in
// Sema::CheckCompleteVariableDeclaration, gated on the initializer being a
// constant initializer (the same domain the IR classifier judges).
//
// This file pins the source layer. The six-cell matrix (O0/O2 x
// syntax/IR/object with exact statuses and artifact checks) lives in
// clang/test/CodeGen/mcs251-wp4-r7-cpp-a2.cpp and its helper
// clang/test/CodeGen/Inputs/mcs251-r7-cpp-a2-check.py.

// ---------------- C++ base subobject, the shape from the round-7 tasking ----
//
// The initializer list is {<base>, <field>}: the FIRST element initializes the
// base subobject `B` (its own `int n`), the SECOND is the pointer member. The
// walker must consume the base element before pairing field `p`, or the cast
// is judged against `B`'s type and nothing is reported.

struct B {
  int n;
};

struct S : B {
  int *p;
};

// expected-error@+1 {{absolute-address}}
S base_abs = {{0}, (int *)0x1234};

// The supported forms stay accepted, in the same shape and position: null and
// '&symbol' with a constant offset -- the domain the IR/object layers share.
int base_target;
S base_null = {{0}, 0};
S base_symbol = {{0}, (int *)&base_target};

// Two bases: the semantic list carries one element per base BEFORE the field
// entries, so both must be consumed. Pairing the pointer element with the
// first base would read it as a `B1`-typed subobject.
struct B1 {
  int a;
};

struct B2 {
  int b;
};

struct S2 : B1, B2 {
  int *p;
};

// expected-error@+1 {{absolute-address}}
S2 base2_abs = {{0}, {0}, (int *)0x1234};
S2 base2_null = {{0}, {0}, 0};
S2 base2_symbol = {{0}, {0}, (int *)&base_target};

// The pointer member may equally live inside the BASE, with the derived part
// providing only non-pointer members; the base element is the first one again.
struct PB {
  int *q;
};

struct DS : PB {
  int n;
};

// expected-error@+1 {{absolute-address}}
DS baseptr_abs = {{(int *)0x1234}, 0};
DS baseptr_null = {{0}, 0};

// A base whose own member is a nested aggregate: the descent happens inside
// the base element, not at the top level.
struct NBInner {
  int m;
};

struct NB {
  NBInner n;
};

struct NS : NB {
  int *p;
};

// expected-error@+1 {{absolute-address}}
NS nested_base_abs = {{{0}}, (int *)0x1234};
NS nested_base_null = {{0}, 0};

// Block-scope static has static storage duration too (the C path covers it via
// its own branch; the C++ path reaches it through CheckCompleteVariableDeclaration).
void local_static() {
  // expected-error@+1 {{absolute-address}}
  static S s = {{0}, (int *)0x1234};
  (void)s;
}

// ---------------- the rest of the C++ static-initializer surface -------------
//
// The same rule decides these, and the round-7 wiring is what makes the source
// layer agree with IR/object here as well. Each rejected shape is paired with
// its supported counterpart.

// A plain scalar static pointer initializer: no aggregate, no base. This was
// silent at the source layer for exactly the same reason.
// expected-error@+1 {{absolute-address}}
int *scalar_abs = (int *)0x1234;
int *scalar_null = 0;
int *scalar_symbol = (int *)&base_target;

// Array and nested-record leaves.
// expected-error@+1 {{absolute-address}}
int *array_abs[2] = {0, (int *)0x1234};
int *array_null_then_symbol[2] = {0, (int *)&base_target};

struct Inner {
  int *q;
};

struct Outer {
  struct Inner in;
};

// expected-error@+1 {{absolute-address}}
Outer nested_abs = {{(int *)0x1234}};
Outer nested_symbol = {{(int *)&base_target}};

// Anonymous-struct member: the initializer list descends into a member whose
// type has no name; the base-first rule and this one share the element cursor.
struct Anon {
  struct {
    int n;
  } a;
  int *p;
};

// expected-error@+1 {{absolute-address}}
Anon anon_abs = {{0}, (int *)0x1234};
Anon anon_null = {{0}, 0};

// Namespace-scope static pointer initializer: still static storage duration,
// still the source layer's business.
namespace N {
// expected-error@+1 {{absolute-address}}
int *ns_abs = (int *)0x1234;
int *ns_null = 0;
} // namespace N

// An out-of-class definition of a static data member is a static-storage
// definition of a pointer; the source layer reports it like any other.
struct Holder {
  static int *member;
};
// expected-error@+1 {{absolute-address}}
int *Holder::member = (int *)0x1234;

// The interprocedural value forms keep their own decisions: a cast whose
// operand is not a constant does not fold, so the diagnostic is the shape
// walk's (and a plain function call initializer is not a constant initializer
// at all, so this rule does not apply to it -- its own path is separately
// registered and unchanged).
// expected-error@+1 {{absolute-address}}
int *call_operand_abs = (int *)(long)0x1234;

// A C++ dynamic (non-constant) initializer is NOT this rule's business: the IR
// classifier judges static initializers, and a runtime value is neither. This
// shape is accepted by the source layer in every cell (its object path has its
// own separately registered pre-existing behavior).
int runtime_value();
int *dynamic_init = (int *)runtime_value();
