// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c++17 -fsyntax-only -verify %s
// expected-no-diagnostics
//
// C11/GNU atomic operations (including fences) are supported on MCS251, so
// condition-variable initializers containing atomic operations are accepted.

int cpp_if_condvar(void) {
  return ({ if (int y = (__atomic_signal_fence(0), 1)) {} 0; });
}

int cpp_while_condvar(void) {
  return ({ while (int y = (__atomic_signal_fence(0), 0)) {} 0; });
}

int cpp_for_condvar(void) {
  return ({ for (; int y = (__atomic_signal_fence(0), 0);) {} 0; });
}

int cpp_switch_condvar(void) {
  return ({ switch (int y = (__atomic_signal_fence(0), 1)) { default: break; } 0; });
}

int cpp_condvar_ordinary(void) {
  if (int y = (__atomic_signal_fence(0), 1)) {}
  return 0;
}

int cpp_if_initstmt(void) {
  return ({ if (int y = (__atomic_signal_fence(0), 1); y) {} 0; });
}

int cpp_condvar_sizeof(void) {
  return sizeof(({ if (int y = (__atomic_signal_fence(0), 1)) {} 0; }));
}

int cpp_condvar_plain(void) {
  return ({ if (int y = 1) {} 0; });
}
