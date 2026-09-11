// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fforce-enable-int128 -Wno-unused-value -Wno-empty-body -Wno-unsequenced -fsyntax-only -verify %s

// DIALECT-FRONTEND-DESIGN §7.5 forced-operation rules for controlled fixed bit
// lvalues. Allowed: constant 0/1 writes, and the CPL toggle forms (X ^= 1,
// X = !X) only where their value is discarded. Rejected: using a toggle result,
// and any other self-reading read-modify-write (notably X = ~X, which is not a
// logical toggle), plus `^=` with any RHS other than the constant 1.

sbit X = 0x24;
sbit Y = 0x24; // same fixed address as X

// Allowed forms.
void allowed(void) {
  X = 0;
  X = 1;
  X ^= 1;      // CPL toggle, result discarded
  X = !X;      // CPL toggle, result discarded
  (void)(X ^= 1); // the void cast discards the toggle result
  int r = 0;
  r = (X ^= 1, 0); // comma left operand result is discarded
  (void)r;
  __bit y = X; // a plain read is fine
  (void)y;
}

// g's argument consumes the toggle result -> reject.
void g(int);
void call_argument(void) {
  g(X ^= 1);   // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void assign_used(void) {
  int y;
  y = (X ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
  (void)y;
}
void not_call_argument(void) {
  g(X = !X);   // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
int return_used(void) {
  return (X ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}

// `^=` is the CPL toggle only for the constant 1. Everything else reads the bit
// for an ordinary computation and is a rejected complex RMW.
void xor_zero(void) {
  X ^= 0; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void xor_two(void) {
  X ^= 2; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void xor_variable(int y) {
  X ^= y; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void or_assign(void) {
  X |= 1; // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void bitwise_not_assignment(void) {
  X = ~X; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void increment(void) {
  ++X; // expected-error {{read-modify-write '++' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void postfix_decrement(void) {
  X--; // expected-error {{read-modify-write '--' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void arithmetic_rmw(void) {
  X = X + 1; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// Address aliasing is by resolved bit address: two distinct sbit names bound to
// the same address are the same fixed location, and the builtin at that address
// aliases it too.
void alias_sbit_sbit(void) {
  X = ~Y; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void alias_sbit_builtin(void) {
  X = ~__builtin_mcs251_bit_lvalue(0x24); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// A read inside an unevaluated operand (sizeof) is not a runtime self-read.
void unevaluated_read(void) {
  X = sizeof(+X);
}

// A function call that only looks like a fixed reference is not one, and has no
// argument to inspect: it must not be mistaken for a controlled lvalue (and
// must not crash).
int not_builtin();
void ordinary_call_rhs(void) {
  __builtin_mcs251_bit_lvalue(0x24) = !not_builtin();
}

// The same rules apply to the builtin-controlled lvalue.
void builtin_rmw(void) {
  __builtin_mcs251_bit_lvalue(0x24) = 1;  // allowed
  __builtin_mcs251_bit_lvalue(0x24) ^= 1; // allowed, discarded
  __builtin_mcs251_bit_lvalue(0x24) ^= 2; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
  int r = (__builtin_mcs251_bit_lvalue(0x24) ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
  (void)r;
}

// A read-modify-write on an ordinary value is unaffected.
void ordinary_unaffected(void) {
  int n = 0;
  n = ~n;
  ++n;
  n ^= 1;
  n ^= 2;
  (void)n;
}

// `^=` is the CPL toggle only for the exact constant 1. A wide value whose low
// bits happen to be 1 is not the constant 1 and must be rejected on its full
// width (not narrowed to 1).
void wide_not_one(void) {
  X ^= (((unsigned __int128)1 << 64) | 1); // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// Legal spellings of the constant 1 are still accepted.
typedef unsigned int U;
enum { ONE = 1 };
void ones(void) {
  (X) ^= (1);
  X ^= 0x1;
  X ^= 1U;
  X ^= ONE;
  X ^= (U)1;
  X ^= (1 + 0);
  X ^= (unsigned __int128)1;
}

// A toggle used as a call argument or an array subscript consumes its value.
void call_arg(void) {
  g(X ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void subscript(int *p) {
  int i = p[X ^= 1]; // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
  (void)i;
}

// _Generic: the controlling expression is not evaluated, and only the selected
// association is evaluated.
void generic_control(void) {
  _Generic(+(X ^= 1), int : 0);
}
void generic_unselected(void) {
  _Generic(0, int : 0, default : (X ^= 1));
}
void generic_selected_discarded(void) {
  _Generic(0, int : (X ^= 1));
}
void generic_self_control(void) {
  X = _Generic(+X, int : 1);
}
int generic_selected_used(void) {
  return _Generic(0, int : (X ^= 1)); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}

// __builtin_choose_expr: only the chosen branch is evaluated.
void choose_unselected(void) {
  __builtin_choose_expr(1, 0, (X ^= 1));
}
void choose_selected_discarded(void) {
  __builtin_choose_expr(1, (X ^= 1), 0);
}
void choose_self_unselected(void) {
  X = __builtin_choose_expr(1, 0, +X);
}

// GNU `c ?: x`: the common expression is evaluated and used; the selected
// branch value flows out.
void gnu_cond_test(void) {
  (X ^= 1) ?: 0; // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void gnu_cond_discard(int c) {
  c ?: (X ^= 1);
}

// A statement expression evaluates its statements; a self-read inside the body
// is a runtime read.
void stmtexpr_self(void) {
  X = ({ ~X; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void stmtexpr_discarded(void) {
  ({ X ^= 1; });
}
void stmtexpr_inner_discarded(void) {
  int r = ({ X ^= 1; 0; });
  (void)r;
}
int stmtexpr_used(void) {
  return ({ X ^= 1; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}

// An asm register input consumes the expression's value.
void asm_used(void) {
  __asm__("" : : "r"(X ^= 1)); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}

// An asm output operand computes its address at runtime, so a toggle used there
// is likewise rejected.
void asm_output_index(void) {
  int a[2];
  __asm__("" : "=r"(a[X ^= 1])); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void asm_output_index_bad(void) {
  int a[2];
  __asm__("" : "=r"(a[X |= 1])); // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// Statement-kind-aware traversal inside a statement expression: a discarded
// expression statement (in an if/while/for/label/nested block) keeps its CPL
// toggle valid, while a consumed one (condition, return value, nested use) is
// rejected.
void stmt_if_discard(int c) {
  ({ if (c) X ^= 1; 0; });
}
void stmt_while_discard(int c) {
  ({ while (c--) X ^= 1; 0; });
}
void stmt_for_discard(int c) {
  ({ for (; c; c--) X ^= 1; 0; });
}
void stmt_label_discard(void) {
  ({ L: X ^= 1; 0; });
}
void stmt_nested_compound_discard(void) {
  ({ { X ^= 1; } 0; });
}
int stmt_if_while_used(int c) {
  return ({ if (c) while (X ^= 1); 0; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
int stmt_if_if_used(int c) {
  return ({ if (c) if (X ^= 1); 0; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
int stmt_if_return_used(int c) {
  return ({ if (c) return X ^= 1; 0; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void stmt_if_while_bad(int c) {
  ({ if (c) while (c--) X |= 1; 0; }); // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void stmt_if_label_bad(int c) {
  ({ if (c) L2: X |= 1; 0; }); // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// Nested ordinary blocks inside a statement expression still count as being
// inside an unevaluated operand: sizeof of the whole statement expression does
// not evaluate it.
void stmt_block_unevaluated(void) {
  sizeof(+({ { X |= 1; } 0; }));
}
void stmt_generic_unselected(void) {
  _Generic(0, int : 0, default : ({ X |= 1; 0; }));
}

// A pure write to the fixed bit is not a self-read.
void stmt_only_write(void) {
  X = ({ X = 0; 1; });
}
void write_comma(void) {
  X = (X = 0, 1);
}
void stmt_write_other(void) {
  X = ({ Y = 0; 1; });
}
// A nested statement that reads X is a self-read.
void stmt_decl_self(void) {
  X = ({ int a = ~X; a; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}



// N1: the fallback for other statement kinds must recurse into nested
// statements, not only direct expression children. (_Defer is covered in
// mcs251-bit-defer.c, which needs -fdefer-ts.)
void indirect_goto_bad(int c) {
  ({ goto *((X ^= 1) ? &&L : &&L); L:; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}

// N2: the LHS of an assignment is a write target, but its address computation
// (the index of `a[X]`) is evaluated and reads the bit.
void lhs_index_write(void) {
  int a[2];
  X = (a[X] = 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void lhs_index_compound(void) {
  int a[2];
  X = (a[X] += 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void lhs_index_inc(void) {
  int a[2];
  X = (a[X]++, 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
// A write to a plain fixed bit (no address computation) is still not a read.
void plain_write_only(void) {
  // Sequenced via a comma so this is not a same-object double modification.
  X = (0, X = 1);
}

// N3: an asm statement inside an unevaluated operand is not evaluated.
void asm_unevaluated(void) {
  int a[2];
  sizeof(+({ __asm__("" : : "r"(X ^= 1)); 0; }));
}
void asm_output_unevaluated(void) {
  int a[2];
  sizeof(+({ __asm__("" : "=r"(a[X |= 1])); 0; }));
}
void asm_generic_unselected(void) {
  int a[2];
  _Generic(0, int : 0, default : ({ __asm__("" : "=r"(a[X |= 1])); 0; }));
}
void asm_choose_unselected(void) {
  int a[2];
  __builtin_choose_expr(1, 0, ({ __asm__("" : "=r"(a[X |= 1])); 0; }));
}
// An asm output that writes the fixed bit (no read) is not a self-read.
void asm_output_write_only(void) {
  X = ({ __asm__("" : "=r"(X)); 1; });
}

// N6: a builtin with UnevaluatedArguments does not evaluate its operand.
void builtin_constant_p_unevaluated(void) {
  __builtin_constant_p(X |= 1);
}

// R6-2: the operand of a dereference is the address computation of the write
// target and is fully evaluated: a bit read there is a self-read.
int *h(int);
void deref_addr_write(int *p) {
  X = (*(p + X) = 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void deref_call_base(void) {
  X = (*h(X) = 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void deref_cond_addr(int *p) {
  X = (*(X ? p : p + 1) = 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void deref_inc_addr(int *p) {
  X = (++*(p + X), 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void deref_compound_addr(int *p) {
  X = (*(p + X) += 1); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void deref_no_read(int *p) {
  X = (*(p + 1) = 1); // a bit-free address computation is not a read
}

// R6-3: an asm read-write output ("+r") loads the operand's value into the
// constraint before the asm runs, so it reads the bit.
void asm_readwrite_self(void) {
  X = ({ __asm__("" : "+r"(X)); 1; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void asm_readwrite_alone(void) {
  // Reading the bit into the asm register is a sample; writing it back is a
  // write. Neither is a self-read by itself.
  __asm__("" : "+r"(X));
}

// R6-4: the operand of an unevaluated-argument builtin is not a runtime read,
// in the read scanner as well as the usage checker.
void unevaluated_builtin_self(void) {
  X = __builtin_constant_p(X);
  X = __builtin_classify_type(X);
}

// R7: __builtin_object_size is unevaluated like the others above (its result
// is always a compile-time constant, so its pointer argument is never a
// runtime read of the bit).
void object_size_unevaluated(void) {
  X = __builtin_object_size((void *)(unsigned long)X, 0);
  __builtin_object_size((X |= 1, (void *)0), 0);
}

// R7: __builtin_dynamic_object_size also carries UnevaluatedArguments, but it
// may lower to a runtime objectsize computation that consumes its argument
// pointer, so for the controlled-bit read rules its argument counts as
// evaluated: a self-read, a complex RMW, and a used toggle inside it are
// rejected, while a discarded toggle stays allowed.
void dynamic_object_size_self_read(void) {
  X = __builtin_dynamic_object_size((void *)(unsigned long)X, 0); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void dynamic_object_size_complex_rmw(void) {
  __builtin_dynamic_object_size((X |= 1, (void *)0), 0); // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void dynamic_object_size_used_toggle(char *p) {
  __builtin_dynamic_object_size(p + (X ^= 1), 0); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void dynamic_object_size_discarded_toggle(char *p) {
  __builtin_dynamic_object_size((X ^= 1, p), 0);
}

// Dead-branch folding (short-circuit, ?:, GNU ?:, if (0)/while (0)/for (; 0;)):
// the dead side is never evaluated and cannot read the bit. The live side is
// still a self-read.
void fold_and_dead(void) { X = (0 && X); }
void fold_or_dead(void) { X = (1 || X); }
void fold_and_live(void) {
  X = (1 && X); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fold_or_live(void) {
  X = (0 || X); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fold_cond_dead_true(void) { X = (1 ? 0 : X); }
void fold_cond_dead_false(void) { X = (0 ? X : 1); }
void fold_cond_live(void) {
  X = (1 ? X : 0); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fold_gnu_dead(void) { X = (1 ?: X); }
void fold_gnu_live(void) {
  X = (0 ?: X); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fold_if_dead(void) {
  X = ({ if (0) { int y = X; (void)y; } 1; });
}
void fold_if_live(void) {
  X = ({ if (1) { int y = X; (void)y; } 1; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fold_if_dynamic(int c) {
  X = ({ if (c) { int y = X; (void)y; } 1; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fold_while_dead(void) {
  X = ({ while (0) { int y = X; (void)y; } 1; });
}
void fold_for_dead(void) {
  X = ({ for (; 0;) { int y = X; (void)y; } 1; });
}
int fold_used_toggle_dead(void) {
  // The used toggle is in the dead branch of a constant if.
  return ({ if (0) return (X ^= 1); 0; });
}
