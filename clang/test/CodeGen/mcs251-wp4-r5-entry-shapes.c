// WP4 revision round 5: a constant-false branch is dead by FALL-THROUGH only.
//
// A `goto` can enter a labelled statement inside such a branch from outside,
// and a `case` label of an enclosing switch can enter statements that sit in
// one. Those statements therefore really do run, and an atomic operation
// written in one is an operation. Before this rule was added the shapes below
// were measured as wrongly accepted at BOTH O0 and O2: the pruning looked only
// at the constant condition and never asked whether the branch had another
// entry point.
//
// The ACCEPTED half here is what the two cc1 RUN lines produced: ordinary
// label-free constant-false branches still prune, and a strict ELF object is
// written at both optimizations. The REJECTED half is re-run by the harness
// with an asserted exit status, because `not` would only prove "nonzero".
//
// The Sema-level half lives in clang/test/Sema/mcs251-capability-diagnostics.c
// and clang/test/Sema/mcs251-wp4-r5-condvar.cpp.
//
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu11 -mllvm -mcs251-object-format=elf -O0 -emit-obj %s -o %t.O0.o
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu11 -mllvm -mcs251-object-format=elf -O2 -emit-obj %s -o %t.O2.o
// RUN: %python %S/Inputs/mcs251-r5-obj-check.py %t.O0.o %t.O2.o -- %clang_cc1

// --- constant-false branches with NO extra entry point still prune ---------
// A relaxed fence lowers to nothing in IR, so the IR layer cannot repair a
// wrong decision here: the source-level decision is the only one that matters.
int plain_if_dead(void) { return ({ if (0) { __atomic_signal_fence(0); } 1; }); }
int plain_while_dead(void) { return ({ while (0) { __atomic_signal_fence(0); } 1; }); }
int plain_for_dead(void) { return ({ for (; 0;) { __atomic_signal_fence(0); } 1; }); }

// A case label whose switch is INSIDE the dead branch is not an entry point
// from outside that branch, so the branch still prunes.
int nested_switch_case_dead(int x) {
  return ({ if (0) { switch (x) { case 1: __atomic_signal_fence(0); } } 1; });
}

// --- the same shapes with a label: the statements run, so they are rejected -
// These are the four spellings measured as wrongly accepted. They are named in
// the helper below and re-run there with an exact-status assertion.
//
// A relaxed fence lowers to nothing in IR and a C11 atomic store may fold
// away, so neither is recoverable by a later layer: the source-level decision
// is the only one that decides them.

// A labelled constant-false branch neighbour stays accepted: the label is on a
// statement with no atomic operation, and an operation elsewhere in the same
// block is on the ordinary path.
int label_without_operation(void) { return ({ L: ; 1; }); }

// --- the object really carries the accepted functions and their data -------
// The helper checks these DEFINED symbols and the relocation that resolves the
// supported '&symbol' pointer initializer, so an accepted object cannot pass on
// a magic number and a length alone.
int r5_pointer_target;
int *r5_symbol_pointer = &r5_pointer_target;

// --- WP4 round 6: union initializers decide on the member actually named ----
// A union initializes exactly one member. The walk must judge the initializer
// under THAT member's type, not the first field's: an integer member whose
// initializer merely contains a cast is fine (union_int_member), a pointer
// member initialized to null or to a symbol is fine (union_ptr_member), and a
// union nested in a struct follows the same rule per member (union_in_struct).
// The rejected counterparts (a non-first POINTER member initialized with an
// absolute address) run in the helper below.
union BoxA { int *ptr; unsigned tag; };
union BoxA union_int_member_box = { .tag = (unsigned)(long)(int *)0x1234 };
int union_int_member(void) { return (int)union_int_member_box.tag; }

union BoxB { int *ptr; unsigned tag; };
union BoxB union_ptr_member_box = { .ptr = (int *)0 };
int union_ptr_member(void) { return union_ptr_member_box.ptr != 0; }

union U6 { int tag; int *ptr; };
struct S6 { int n; union U6 u; };
struct S6 union_in_struct_s = { 1, { .tag = 5 } };
int union_in_struct(void) { return union_in_struct_s.u.tag; }

// An ANONYMOUS union member follows the same rule (the member actually named
// by the designator decides, not the member order).
struct ABox { int n; union { int *ptr; unsigned tag; }; };
struct ABox anon_union_int_s = { 1, { .tag = (unsigned)(long)(int *)0x1234 } };
int anon_union_int(void) { return (int)anon_union_int_s.tag; }
