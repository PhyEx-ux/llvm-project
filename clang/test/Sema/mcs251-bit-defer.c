// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fdefer-ts -fmcs251-keil -Wno-unused-value -fsyntax-only -verify %s

// N1: a _Defer body is a statement, not a direct expression child of the
// statement expression. The controlled-bit statement fallback must recurse into
// it, otherwise `X |= 1` inside a _Defer would be missed.
sbit X = 0x24;

void defer_bad(void) {
  ({ _Defer { X |= 1; } 0; }); // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

void defer_toggle_discarded(void) {
  ({ _Defer { X ^= 1; } 0; }); // allowed: the toggle result is discarded
}

void defer_direct_bad(void) {
  _Defer { X |= 1; } // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// Dead-branch folding must apply inside a _Defer body too: the dead branch of
// a constant condition never reads the bit.
void defer_dead_branch(void) {
  X = ({ _Defer { if (0) { int y = X; (void)y; } } 1; });
}
void defer_live_branch(void) {
  X = ({ _Defer { if (1) { int y = X; (void)y; } } 1; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
