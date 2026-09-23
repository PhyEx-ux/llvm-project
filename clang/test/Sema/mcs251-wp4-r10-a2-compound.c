// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu11 -fsyntax-only -verify -Wno-pointer-bool-conversion %s
//
// A static-storage C compound literal is checked only when its expression is
// evaluated. Folding the aggregate result must not erase creation-time checks
// for the selected literal or revive the unselected literal's initializer.
//
// The A2 condition fold accepts any side-effect-free constant bool, including
// floating and pointer conditions that are not integer constant expressions.
// The `&symbol` conditions below would otherwise also produce an unrelated
// `address ... always evaluates to 'true'` diagnostic; that warning is
// suppressed so this test isolates the MCS251 absolute-address rule.

struct S {
  int *p;
};
int symbol;

// Integer conditions: only the selected compound literal is judged.
struct S *unselected_absolute = 1 ? (struct S *)0 : &(struct S){(int *)0x1234};
struct S *selected_null = 0 ? &(struct S){(int *)0x1234} : &(struct S){(int *)0};
struct S *selected_symbol = 1 ? &(struct S){&symbol} : &(struct S){(int *)0x1234};

// Floating and pointer conditions: a selected supported leaf stays accepted
// even though the dead arm carries an absolute address.
struct S *float_true_null = 1.0 ? &(struct S){(int *)0} : &(struct S){(int *)0x1234};
struct S *float_false_null = 0.0 ? &(struct S){(int *)0x1234} : &(struct S){(int *)0};
struct S *pointer_true_symbol = &symbol ? &(struct S){&symbol} : &(struct S){(int *)0x1234};
struct S *pointer_false_null = (int *)0 ? &(struct S){(int *)0x1234} : &(struct S){(int *)0};

// A selected absolute address is still rejected for floating and pointer
// conditions, exactly as for the integer conditions above.
// expected-error@+1 {{absolute-address (integer-to-pointer) pointer initialization}}
struct S *float_true_absolute = 1.0 ? &(struct S){(int *)0x1234} : (struct S *)0;
// expected-error@+1 {{absolute-address (integer-to-pointer) pointer initialization}}
struct S *pointer_true_absolute = &symbol ? &(struct S){(int *)0x1234} : (struct S *)0;
// expected-error@+1 {{absolute-address (integer-to-pointer) pointer initialization}}
struct S *pointer_false_absolute = (int *)0 ? (struct S *)0 : &(struct S){(int *)0x1234};
