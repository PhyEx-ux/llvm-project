// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu++17 -fsyntax-only -verify -Wno-pointer-bool-conversion %s
//
// A2 aggregate selection is based on the final folded value. Constant pointer
// and floating conditions are side-effect-free constant bools even though they
// are not integer constant expressions.
//
// The `&v` conditions below are intentional pointer conditions. Testing them
// also emits an unrelated `address of 'v' will always evaluate to 'true'`
// diagnostic, so -Wno-pointer-bool-conversion keeps this test focused on the
// MCS251 absolute-address rule.

struct S {
  int *p;
};
int v;

// Selected null remains supported; the dead arm's absolute address is ignored.
S pointer_true_null = &v ? S{nullptr} : S{(int *)0x1234};
S float_true_null = 1.0 ? S{nullptr} : S{(int *)0x1234};
S float_false_null = 0.0 ? S{(int *)0x1234} : S{nullptr};
S integer_true_null = 1 ? S{nullptr} : S{(int *)0x1234};

// The selected absolute address is rejected for each condition category.
// expected-error@+1 {{absolute-address (integer-to-pointer) pointer initialization}}
S pointer_true_absolute = &v ? S{(int *)0x1234} : S{nullptr};
// expected-error@+1 {{absolute-address (integer-to-pointer) pointer initialization}}
S float_false_absolute = 0.0 ? S{nullptr} : S{(int *)0x1234};
// expected-error@+1 {{absolute-address (integer-to-pointer) pointer initialization}}
S integer_true_absolute = 1 ? S{(int *)0x1234} : S{nullptr};

// Null and symbol-based selected values remain in the supported domain.
S selected_symbol = &v ? S{&v} : S{(int *)0x1234};
S selected_null = 0.0 ? S{(int *)0x1234} : S{(int *)0};
