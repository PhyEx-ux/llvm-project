// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c++17 -fsyntax-only -verify %s
//
// WP4 revision round 5: a condition-variable DECLARATION is evaluated.
//
// `if (T y = init)` (C++ [stmt.select], extended to switch/while/for) declares
// the condition variable and initializes it; the initializer runs before the
// condition is tested. The condition expression that follows mentions only the
// already-declared variable, so a walk that scans getCond() alone never sees
// the initializer. Before this rule was added the four shapes below were
// measured as wrongly accepted at O0 and O2, in all three output modes.
//
// This is the C++ half of the rule; the C++ counterpart of the "the whole
// statement expression is unevaluated" escape is exercised as well, because
// the enclosing-selection deferral must keep working for condition variables.
//
// WP4 revision round 6: this file stays the fast source-level check. The full
// six-cell matrix (O0/O2 x syntax/IR/object with exact statuses) for the same
// shapes lives in clang/test/CodeGen/mcs251-wp4-r6-condvar-boundary.cpp and
// its helper; the REJECTED counterparts (the four statement forms plus
// `__c11_atomic_init`) also run six cells inside
// clang/test/CodeGen/Inputs/mcs251-r5-obj-check.py.

// The initializer of a condition variable in `if` is evaluated.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cpp_if_condvar(void) {
  return ({ if (int y = (__atomic_signal_fence(0), 1)) {} 0; });
}

// The same for `while`. The condition is a constant zero, so the BODY never
// runs -- but the initializer still does, and that is what is reported.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cpp_while_condvar(void) {
  return ({ while (int y = (__atomic_signal_fence(0), 0)) {} 0; });
}

// The same for `for`'s condition declaration.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cpp_for_condvar(void) {
  return ({ for (; int y = (__atomic_signal_fence(0), 0);) {} 0; });
}

// The same for `switch`.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cpp_switch_condvar(void) {
  return ({ switch (int y = (__atomic_signal_fence(0), 1)) { default: break; } 0; });
}

// A condition variable outside a statement expression is evaluated like any
// other initializer, so the operation is reported where it is written.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cpp_condvar_ordinary(void) {
  if (int y = (__atomic_signal_fence(0), 1)) {}
  return 0;
}

// The C++17 init-statement form was already reported and must stay reported.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cpp_if_initstmt(void) {
  return ({ if (int y = (__atomic_signal_fence(0), 1); y) {} 0; });
}

// The enclosing-selection deferral still applies: a whole statement expression
// -- condition variable included -- that sits in an unevaluated position never
// runs, so nothing in it is an operation.
int cpp_condvar_sizeof(void) {
  return sizeof(({ if (int y = (__atomic_signal_fence(0), 1)) {} 0; }));
}

// A condition variable whose initializer is an ordinary value stays accepted.
int cpp_condvar_plain(void) {
  return ({ if (int y = 1) {} 0; });
}
