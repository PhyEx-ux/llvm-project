// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

// Regression guard: the controlled-bit analysis must never mistake an ordinary
// CallExpr for the fixed-bit builtin. Before this test the `isSameMCS251FixedBit`
// helper treated any two CallExprs as fixed references and unconditionally read
// getArg(0), crashing on a zero-argument ordinary call used as the RHS of an
// assignment to the builtin lvalue.

int g(void);
int g1(int);

// The RHS is an ordinary zero-argument call, not a fixed bit reference, and
// there is no self-read of the builtin's fixed bit. No crash, no diagnostic.
void ordinary_call_rhs(void) {
  __builtin_mcs251_bit_lvalue(0x24) = !g();
}

// The RHS is an ordinary one-argument call whose argument happens to be 0x24;
// it is still not the builtin, so it is not the same fixed location.
void ordinary_call_arg(void) {
  __builtin_mcs251_bit_lvalue(0x24) = g1(0x24);
}

// The builtin itself is not misidentified by arity; the frontend diagnoses the
// wrong argument count instead of reading past the argument list.
void wrong_arity(void) {
  __builtin_mcs251_bit_lvalue();     // expected-error {{too few arguments to function call, expected 1, have 0}}
  __builtin_mcs251_bit_lvalue(0, 1); // expected-error {{too many arguments to function call, expected 1, have 2}}
}
