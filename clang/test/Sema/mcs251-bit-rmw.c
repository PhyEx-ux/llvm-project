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

// ---------------------------------------------------------------------------
// P09 object matrix (design sections 2.3-2.5): the same physical
// forced-operation rules on persistent bit objects. Every RMW rule is
// instantiated for the extern+global merged object (OE/OG), the file static
// (FS), and the function-local static (L; a same-named static in each
// function is a *different* object, which doubles as the shadowing-distinct
// identity test). The fixed form is the whole file above. Automatic and
// parameter bit objects close the matrix as positive controls: they are in
// the identity domain but NOT subject to the physical table (P-2 value
// semantics), so none of the errors below may fire for them.
// ---------------------------------------------------------------------------

extern __bit OE;
__bit OG;               // extern + definition: one canonical object
static __bit FS = 1;    // file static
__bit OTHER1 = 1;       // different object, same initial value
__bit OTHER2 = 1;
int dyn(void);
void g(int);

// --- extern+global form: the section 2.4 forced table ---

void obj_allowed(void) {
  OG = 0;
  OG = 1;
  OG = dyn();           // dynamic RHS: one write, no MOV bit,C
  OE = dyn();
  OG = !OTHER1;         // different identity: source read + dest write
  OG = OTHER2;          // plain copy from a different object
  OG ^= 1;              // CPL toggle, discarded
  OG = !OG;             // CPL toggle, discarded
  (void)(OG ^= 1);
  OG = (OE = dyn());    // nested write, result reused, no read-back
  if (OG) OG = 0;       // one test + one path write (no JBC merge)
  if (!OG) { OG = 1; }
}
int obj_read_value(void) {
  int r = OG;
  r += OG + OE;
  return OG ? 1 : 0;
}
int obj_assign_result(void) {
  int r = (OG = dyn()); // the assignment result reuses the RHS, no read-back
  return r;
}

void obj_rmw_not(void) {
  OG = ~OG; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_add(void) {
  OG = OG + 1; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_self(void) {
  OG = OG; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_or(void) {
  OG |= 1; // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_add_assign(void) {
  OG += 1; // expected-error {{read-modify-write '+=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_xor_zero(void) {
  OG ^= 0; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_xor_two(void) {
  OG ^= 2; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_rmw_xor_var(int v) {
  OG ^= v; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_inc(void) {
  ++OG; // expected-error {{read-modify-write '++' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_dec(void) {
  OG--; // expected-error {{read-modify-write '--' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
int obj_used_toggle(void) {
  return (OG ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
int obj_used_not_toggle(void) {
  return (OG = !OG); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void obj_used_toggle_arg(void) {
  g(OE ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void obj_extern_same_object(void) {
  // The file-scope extern OE and its definition below are one canonical
  // object: a self-read through either spelling is the same self-read.
  OE = ~OE; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
__bit OE; // the definition that merges with the extern above
void obj_block_scope_extern(void) {
  // A block-scope extern merges with the file-scope object (one identity).
  extern __bit OG;
  OG = !OG; // the discarded toggle is fine through the inner declaration
}
void obj_block_scope_extern_bad(void) {
  extern __bit OG;
  OG = ~OG; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}

// --- extern+global form: the section 2.5 evaluation-aware rows ---

void obj_fold_dead(int c) {
  OG = (0 && OG);
  OG = (1 || OG);
  OG = (1 ? 0 : OG);
  0 && (OG ^= 1);
  1 || (OG ^= 1);
  // Dead branches *inside a full expression* (statement expression): the
  // constant-false branch is never evaluated, so neither a read nor a
  // physical RMW inside it is diagnosed.
  ({ if (0) { OG = ~OG; } 0; });
  ({ while (0) { OG |= 1; } 0; });
  (void)c;
}
// Boundary (parity with the fixed form): a *statement-level* constant-false
// branch is outside the full-expression evaluation framework -- the inner
// assignment is its own full expression -- so the physical RMW is diagnosed
// there, for objects exactly as for fixed references.
void obj_stmt_level_dead_branch(void) {
  if (0) { OG = ~OG; } // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_fold_live(int c) {
  OG = (c && OG); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_fold_cond_live(int c) {
  OG = (c ? 0 : OG); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_used_toggle_live(int c) {
  c && (OG ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void obj_discarded_forms(int c) {
  (OG ^= 1, 0);
  for (; c; OG ^= 1) { }
  do { OG ^= 1; } while (c);
  switch (c) { case 0: OG ^= 1; break; default: break; }
}
void obj_stmtexpr_discarded(void) {
  ({ OG ^= 1; });
}
int obj_stmtexpr_used(void) {
  return ({ OG ^= 1; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
int obj_stmtexpr_inner_discarded(void) {
  int r = ({ OG ^= 1; 0; });
  return r;
}
void obj_stmtexpr_dead_read(void) {
  OG = ({ if (0) { int y = OG; (void)y; } 1; });
}
void obj_stmtexpr_live_read(int c) {
  OG = ({ if (c) { int y = OG; (void)y; } 1; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_stmtexpr_nested_write(void) {
  OG = ({ OG = 0; 1; }); // a nested write is not a read
}
void obj_asm_used(void) {
  __asm__("" : : "r"(OG ^= 1)); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void obj_asm_readwrite_self(void) {
  OG = ({ __asm__("" : "+r"(OG)); 1; }); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void obj_asm_unevaluated(void) {
  sizeof(+({ __asm__("" : : "r"(OG ^= 1)); 0; }));
}
void obj_unevaluated_builtin(void) {
  OG = __builtin_constant_p(OG);
  __builtin_constant_p(OG |= 1);
}

// --- file-static form: the section 2.4 forced table ---

void fs_allowed(void) {
  FS = 0;
  FS = dyn();
  FS ^= 1;
  FS = !FS;
  FS = !OG;   // cross-form copy: object read + object write
  FS = !X;    // Fixed/Object mixed identities: never a toggle, source read + write
  (void)FS;
}
void fs_rmw_not(void) {
  FS = ~FS; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fs_rmw_or(void) {
  FS |= 1; // expected-error {{read-modify-write '|=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fs_rmw_xor_two(void) {
  FS ^= 2; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fs_inc(void) {
  ++FS; // expected-error {{read-modify-write '++' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
int fs_used_toggle(void) {
  return (FS ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void fs_fold_dead(void) {
  FS = (0 && FS);
  if (0) { FS ^= 1; }
}
void fs_fold_live(int c) {
  FS = (c && FS); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void fs_stmtexpr_used(void) {
  int r = ({ FS ^= 1; }); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
  (void)r;
}

// --- local-static form: the section 2.4 forced table (one static per
// function; same-named statics in different functions are distinct objects,
// so no cross-function false identity may fire) ---

void ls_allowed(void) {
  static __bit L;
  L = 1;
  L = dyn();
  L ^= 1;
  L = !L;
  L = !OG;
  (void)L;
}
void ls_rmw_not(void) {
  static __bit L;
  L = ~L; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void ls_rmw_self(void) {
  static __bit L;
  L = L; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void ls_rmw_xor_var(int v) {
  static __bit L;
  L ^= v; // expected-error {{read-modify-write '^=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void ls_dec(void) {
  static __bit L;
  --L; // expected-error {{read-modify-write '--' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
int ls_used_toggle(void) {
  static __bit L;
  return (L ^= 1); // expected-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}
void ls_fold_live(int c) {
  static __bit L;
  L = (c ? L : 0); // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void ls_fold_dead(int c) {
  static __bit L;
  L = (0 ? L : 1);
  ({ if (0) { L = ~L; } 0; }); // dead branch inside a full expression: silent
  (void)c;
}
// Cross-object reads between two different local statics are never
// self-reads (each function's L is a distinct object).
void ls_cross_ok(void) {
  static __bit L1;
  static __bit L2;
  L1 = ~L2; // L2 read is a different object: allowed
  L2 = !L1;
}

// --- shadowing and identity boundaries ---

__bit SH; // the global shadowed below
void set_global_sh(void);
void shadow_static(void) {
  static __bit SH; // a distinct object that shadows the global
  SH = ~SH; // expected-error {{read-modify-write '=' of a controlled MCS251 bit is not supported; sample the bit into an ordinary value first}}
}
void shadow_static_cross(void) {
  static __bit SH; // the local static, not the global: a write to the global
  SH = ({ set_global_sh(); 1; }); // inside the RHS is a cross-object write, not a self-read
}
void set_global_sh(void) { SH = 0; } // this function sees the file-scope SH

// Different objects with the same initial value never merge identities.
void same_init_distinct(void) {
  OTHER1 = ~OTHER2; // allowed: distinct objects
}

// Fixed/Object mixed identities: a fixed reference and an object are never
// the same target, in either direction.
void fixed_object_mix(void) {
  X = !OG;  // allowed: RHS is an object read, not the fixed target
  OG = !X;  // allowed: RHS is a fixed read, not the object target
  X = ~OG;  // allowed: the RHS reads a different target
}

// --- automatic/parameter positive controls (P-2 value semantics): the
// physical table must NOT fire for them, including for the used toggle. ---

void auto_control(void) {
  __bit a = OG;        // initialized from a physical read: fine
  a = ~a;              // ordinary value rules
  a = a + 1;
  a = a;
  a += 1;
  a |= 1;
  a ^= 0;
  a ^= 2;
  a ^= OG;
  ++a;
  a--;
  int r = (a ^= 1);    // the used toggle is legal for an automatic value
  (void)r;
  r = (a = !a);
  (void)r;
  a = ({ a ^= 1; });   // statement-expression result used: fine
  (void)a;
}
void param_control(__bit p) {
  p = ~p;
  p = p + 1;
  p |= 1;
  p ^= 2;
  ++p;
  p--;
  int r = (p ^= 1);
  (void)r;
  p = ({ p ^= 1; });
  (void)p;
}
