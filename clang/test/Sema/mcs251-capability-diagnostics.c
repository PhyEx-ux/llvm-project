// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s
//
// WP4 (FUNCTIONAL-GAPS-PLAN-Alice.md §三/§四): the default correct-failure
// capabilities at the C source level. Each rejected construct gets one
// actionable diagnostic; the IR-layer contract check
// (llvm/test/CodeGen/MCS251/unsupported-capabilities.ll) is the safety net
// for direct llc input.
//
// Covered here:
//   A4/A5/A6  atomic family (type + every builtin operation form; the pure
//             lock-free queries stay accepted and answer "not lock-free")
//   A2        absolute-address (integer-to-pointer) static pointer init
//   A7 (EC1)  multi-argument indirect call
//   A8        weak definitions (weak declarations stay accepted)
//   D1        computed goto (address-of-label and indirect goto)
//   A9        module-level inline assembly
// Plus the supported neighbours that must keep compiling.

// ---------------------------------------------------------------- A4/A5/A6 --

// Note: the atomic OBJECT declaration is its own diagnostic (A4); the
// operations below use plain objects so the operation rule is what is under
// test here.
_Atomic unsigned short atomic_object; // expected-error {{atomic types}}

unsigned short plain_object;
volatile unsigned short volatile_plain_object;

// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int atomic_load_builtin(void) { return __atomic_load_n(&plain_object, __ATOMIC_SEQ_CST); }

// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
void atomic_store_builtin(void) { __atomic_store_n(&plain_object, 1, __ATOMIC_SEQ_CST); }

// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int atomic_rmw_builtin(void) { return __atomic_fetch_add(&plain_object, 1, __ATOMIC_SEQ_CST); }

// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
void atomic_fence_builtin(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int sync_rmw_builtin(void) { return __sync_fetch_and_add(&plain_object, 1); }

// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
void sync_fence_builtin(void) { __sync_synchronize(); }

// The compile-time lock-free query stays (it constant-folds to false -- the
// target makes no lock-free claim), but the RUNTIME query is part of the
// rejected family: implementing it would need a libcall the target does not
// provide.
_Static_assert(!__atomic_always_lock_free(2, 0),
               "MCS251 must not claim lock-free atomic support");
int runtime_lock_free_query(void) { return __atomic_is_lock_free(4, 0); } // expected-error {{atomic operations (C11/GNU) are not supported on MCS251}}

// A non-atomic volatile access is the supported way to touch shared memory;
// volatile does not make the atomic family legal.
volatile unsigned short shared_word;
unsigned short volatile_access(void) { return shared_word; }

// ---------------------------- A4/A5/A6: not-evaluated positions stay accepted
//
// The rejection is a property of PERFORMING an atomic operation, not of
// writing one down. The decision is taken once per full expression
// (SemaMCS251::CheckMCS251AtomicUse), so a construct the language never
// evaluates is accepted. These are the shapes the revision round measured as
// wrongly refused; each is a positive with no directive on purpose -- any
// diagnostic here fails -verify.
//
// The operands go through pointers so no _Atomic OBJECT declaration is
// involved: an `_Atomic T` object is separately diagnosed at its declaration,
// which would confound the evaluation rule under test.
int *plain_ptr;
_Atomic int *atomic_ptr_param;

// sizeof / _Alignof / typeof operands are not evaluated. The
// -Wunevaluated-expression warnings are expected (a side-effecting operand in
// an unevaluated context is exactly what these shapes are) and are not part
// of the rule under test.
// expected-warning@+1 {{expression with side effects has no effect in an unevaluated context}}
int uneval_sizeof_load(void) { return sizeof(__atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST)); }
int uneval_sizeof_add(void) { return sizeof(*atomic_ptr_param + 1); }
// expected-warning@+1 {{expression with side effects has no effect in an unevaluated context}}
int uneval_sizeof_store(void) { return sizeof(*atomic_ptr_param = 1); }
// expected-warning@+1 {{expression with side effects has no effect in an unevaluated context}}
int uneval_sizeof_preincr(void) { return sizeof(++*atomic_ptr_param); }
int uneval_sizeof_fence(void) { return sizeof((__c11_atomic_thread_fence(__ATOMIC_SEQ_CST), 1)); }
int uneval_alignof_typeof(void) { return _Alignof(__typeof__(*atomic_ptr_param)); }

// The _Generic controlling expression and the unselected associations are
// not evaluated; only the selected association is. (No -Wunevaluated-expression
// fires for the association bodies: clang builds them in a potentially
// evaluated context and only the selection makes them dead.)
// expected-warning@+1 {{expression with side effects has no effect in an unevaluated context}}
int uneval_generic_controlling(void) { return _Generic(__atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST), int: 1, default: 2); }
int uneval_generic_deref(void) { return _Generic(*atomic_ptr_param, int: 1, default: 2); }
int uneval_generic_default_assoc(void) { return _Generic(0, int: 1, default: __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST)); }
int uneval_generic_unselected_store(void) {
  return _Generic(0, int: 1, default: (*atomic_ptr_param = 1));
}

// __builtin_choose_expr evaluates only the selected arm.
int uneval_choose_load(void) {
  return __builtin_choose_expr(1, 1, __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST));
}
int uneval_choose_store(void) {
  return __builtin_choose_expr(1, 1, (*atomic_ptr_param = 1));
}

// A conditional with an integer-constant condition evaluates only the taken
// arm. (A condition that merely happens to hold a constant at run time is NOT
// treated as constant: both arms stay reachable and both are rejected -- see
// cond_variable_untaken below.)
int uneval_cond_load(void) {
  return 1 ? 1 : __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST);
}
int uneval_cond_deref(void) { return 1 ? 1 : *atomic_ptr_param; }
int uneval_cond_store(void) { return 1 ? 1 : (*atomic_ptr_param = 1); }

// The short-circuited RHS of a constant-decided && / || is not evaluated.
int uneval_land_rhs(void) {
  return 0 && __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST);
}
int uneval_lor_rhs(void) {
  return 1 || __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST);
}

// The evaluated counterparts of the same shapes must STILL be rejected: the
// rule is evaluation, not spelling.
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int bce_selected_arm(void) { return __builtin_choose_expr(1, __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST), 1); }
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int cond_taken_arm(void) { return 0 ? 1 : __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST); }
// expected-error@+1 {{access to an object with an atomic subobject}}
int cond_variable_untaken(int c) { return c ? 1 : *atomic_ptr_param; }
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int land_rhs_evaluated(int c) { return c && __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST); }
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int lor_rhs_evaluated(int c) { return c || __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST); }

// ------------------- GNU `a ?: b`: the COMMON expression is evaluated --------
//
// The common expression is the operand that is evaluated unconditionally: it
// produces the tested value and, when non-zero, the value of the whole
// operator. It is reached through getCommon(), not getCond() -- the condition
// is written in terms of an OpaqueValueExpr whose children() is empty, so a
// walk that followed only the condition would never see the real operands.
// Each evaluated common expression is an operation like any other.
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int gnu_cond_common_load(void) { return (__atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST)) ?: 0; }
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int gnu_cond_common_fence(void) { return (__atomic_signal_fence(0), 1) ?: 0; }
// expected-error@+1 {{access to an object with an atomic subobject}}
int gnu_cond_common_deref(void) { return (*atomic_ptr_param) ?: 1; }
// The dead false arm of a non-zero constant common expression is not evaluated.
int gnu_cond_false_dead(void) { return 1 ?: __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST); }
// A non-constant common expression leaves the false arm reachable.
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int gnu_cond_false_live(int c) { return c ?: __atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST); }
// The atomic subobject access in the false arm of `1 ?: x` is dead, but the
// common expression is an ordinary value, so the whole form is accepted.
int gnu_cond_false_dead_deref(void) { return 1 ?: *atomic_ptr_param; }

// --------------------- __builtin_offsetof: index operands are evaluated ------
//
// The components of __builtin_offsetof are member designators, but an array
// subscript component's INDEX expression is an ordinary evaluated operand of
// the address computation. It is exposed through OffsetOfExpr::children().
struct offsetof_target { int a[8]; };
int offsetof_index_load(void) {
  // expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
  return __builtin_offsetof(struct offsetof_target, a[__atomic_load_n(plain_ptr, __ATOMIC_SEQ_CST)]);
}
int offsetof_index_store(void) {
  // expected-error@+1 {{access to an object with an atomic subobject}}
  return __builtin_offsetof(struct offsetof_target, a[(*atomic_ptr_param = 1)]);
}
// A plain component designator stays accepted.
int offsetof_plain(void) { return __builtin_offsetof(struct offsetof_target, a); }

// ------------------- statement expressions: deferral is scoped --------------
//
// A statement inside `({ ... })` is its own full expression, checked before the
// enclosing expression -- which decides whether the statement expression runs
// at all -- is known. The inner check therefore defers, and the enclosing full
// expression walks the statements. These three shapes are the ones the
// re-check measured as wrongly refused.
int stmt_expr_sizeof_dead(void) { return sizeof(({ __atomic_signal_fence(0); 0; })); }
int stmt_expr_cond_dead(void) { return 1 ? 1 : ({ __atomic_signal_fence(0); 0; }); }
int stmt_expr_generic_dead(void) {
  return _Generic(0, int: 1, default: ({ __atomic_signal_fence(0); 0; }));
}
// The deferred walk carries the dead-statement rules into the statement
// expression body: a constant-false loop or if never runs its body.
int stmt_expr_if_dead(void) { return ({ if (0) { __atomic_signal_fence(0); } 0; }); }
int stmt_expr_while_dead(void) { return ({ while (0) { __atomic_signal_fence(0); } 0; }); }
int stmt_expr_for_dead(void) { return ({ for (; 0;) { __atomic_signal_fence(0); } 0; }); }
// A constant-false branch is dead by FALL-THROUGH only. When a `goto` outside
// the branch can enter it at a label, the statements inside really do run and
// must be reported. (Measured as wrongly accepted before this rule was added;
// the same shapes are exercised at the object level by
// clang/test/CodeGen/mcs251-wp4-r5-entry-shapes.c.)
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int stmt_expr_goto_if_live(void) {
  return ({ goto L; if (0) { L: __atomic_signal_fence(0); } 0; });
}
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int stmt_expr_goto_while_live(void) {
  return ({ goto L; while (0) { L: __atomic_signal_fence(0); } 0; });
}
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int stmt_expr_goto_for_live(void) {
  return ({ goto L; for (; 0;) { L: __atomic_signal_fence(0); } 0; });
}
// A `case` label of an ENCLOSING switch is an entry point for statements that
// otherwise sit in a constant-false branch of that switch's body.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int switch_case_in_dead_if_live(int x) {
  return ({ switch (x) { if (0) { case 1: __atomic_signal_fence(0); } } 0; });
}
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int switch_case_in_dead_for_live(int x) {
  return ({ switch (x) { for (; 0;) { case 1: __atomic_signal_fence(0); } } 0; });
}
// The label rule is about reachability, not about labels: an operation on the
// ordinary fall-through path next to a label is still reported.
// expected-error@+2 {{atomic operations (C11/GNU) are not supported on MCS251}}
int stmt_expr_label_live(void) {
  return ({ __atomic_signal_fence(0); L2: ; 0; });
}
// The evaluated counterparts must STILL be rejected.
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int stmt_expr_live(void) { return ({ __atomic_signal_fence(0); 0; }); }
// expected-error@+1 {{atomic operations (C11/GNU) are not supported on MCS251}}
int stmt_expr_init_live(void) { int v = ({ __atomic_signal_fence(0); 1; }); return v; }

// --------------------------- A2: the comma expression's value is its RHS ----
//
// The left operand is evaluated and discarded; it does not initialize the
// object, so an absolute-address cast written there is not this check's
// business. Only the right operand is the initializer's value. The discarded
// left operand draws the ordinary -Wunused-value warning, which is not part of
// the rule under test.
// expected-warning@+1 {{left operand of comma operator has no effect}}
unsigned char *a2_comma_lhs_discarded = ((unsigned char *)0x1234, (unsigned char *)0);
unsigned char *a2_comma_lhs_null = ((unsigned char *)0, (unsigned char *)0); // expected-warning {{left operand of comma operator has no effect}}
// The right operand is the value, so an absolute address there is rejected.
unsigned char *a2_comma_rhs_live = ((unsigned char *)0, (unsigned char *)0xFF00); // expected-warning {{left operand of comma operator has no effect}} expected-error {{absolute-address}}

// -------------------------------------------------------------------- A2 --

volatile unsigned char *abs_ptr = (volatile unsigned char *)0xFF00; // expected-error {{absolute-address}}

void local_abs_ptr(void) {
  static volatile unsigned char *p = (volatile unsigned char *)0x1234; // expected-error {{absolute-address}}
  (void)p;
}

// The supported static pointer forms stay accepted: null and &symbol.
unsigned short target_word;
unsigned short *null_ptr = 0;
unsigned short *symbol_ptr = &target_word;
// Macro-style MMIO inside a function is not a static initializer and stays
// supported (this is the documented alternative for fixed device addresses).
#define PB (*(volatile unsigned char *)0xFF00)
void macro_mmio(void) { PB = 1; }

// A2 applies the same value/selection discipline as the atomic family: an
// absolute-address cast the initializer never actually selects is not a
// rejection. Each of these is a positive.
unsigned char *a2_generic_unselected =
    _Generic(0, int: (unsigned char *)0, default: (unsigned char *)0xFF00);
unsigned char *a2_cond_untaken = 1 ? (unsigned char *)0 : (unsigned char *)0xFF00;
int a2_sizeof_operand(void) { return sizeof((unsigned char *)0xFF00); }
// The selected form of the same shape is still rejected.
// expected-error@+1 {{absolute-address}}
unsigned char *a2_cond_taken = 0 ? (unsigned char *)0 : (unsigned char *)0xFF00;

// A2 decides on the FINAL constant value and its provenance, matching the
// supported domain the IR-layer classifier shares (null and '&symbol' with a
// constant offset). These four initializers all end up in that domain, so the
// presence of an absolute-address cast somewhere in the expression is not by
// itself a rejection. (Measured as wrongly refused before this rule was
// added.)
int a2_symbol_target;
unsigned char *a2_gnu_cond_symbol = (unsigned char *)&a2_symbol_target ?: (unsigned char *)0x1234;
unsigned char *a2_cond_symbol = (unsigned char *)&a2_symbol_target ? (unsigned char *)&a2_symbol_target
                                                                  : (unsigned char *)0x1234;
unsigned char *a2_compare_selects_null =
    ((unsigned char *)0x1234 == (unsigned char *)0x1234)
        ? (unsigned char *)0 : (unsigned char *)0x1234;
unsigned char *a2_not_nonnull_to_null = (unsigned char *)!((unsigned char *)0x1234);
// The final value is a real absolute address in these, so they stay rejected.
// expected-error@+1 {{absolute-address}}
unsigned char *a2_gnu_cond_abs = 0 ?: (unsigned char *)0x5678;
// expected-error@+2 {{absolute-address}}
unsigned char *a2_cond_selects_abs =
    ((unsigned char *)0x1234 != (unsigned char *)0) ? (unsigned char *)0x9A
                                                    : (unsigned char *)0;
// An aggregate initializer is judged at the same level as a scalar one, so the
// source decision agrees with the IR/object layers (which walk every pointer
// leaf). All elements are walked, not just the first.
int a2_agg_target;
unsigned char *a2_agg_null_then_symbol[2] = {0, (unsigned char *)&a2_agg_target};
unsigned char *a2_agg_symbol[1] = {(unsigned char *)&a2_agg_target};
// expected-error@+1 {{absolute-address}}
unsigned char *a2_agg_abs[1] = {(unsigned char *)0xFF00};
// expected-error@+1 {{absolute-address}}
unsigned char *a2_agg_abs_second[2] = {(unsigned char *)0, (unsigned char *)0x1234};

// -------------------------------------------------------------------- A7 --

typedef unsigned short (*binop_t)(unsigned short, unsigned short);
unsigned short add2(unsigned short a, unsigned short b) { return (unsigned short)(a + b); }

void multiarg_indirect(binop_t fp) {
  // expected-error@+1 {{multi-argument indirect calls are not supported on MCS251}}
  (void)fp(1, 2);
}

typedef unsigned short (*unop_t)(unsigned short);
unsigned short unary_direct(unsigned short x) { return x; }

void supported_calls(void) {
  unop_t up = unary_direct;
  (void)up(1);                 // one-argument indirect: supported
  (void)add2(1, 2);            // direct multi-argument: supported
  (void)unary_direct(3);       // direct: supported
}

// -------------------------------------------------------------------- A8 --

__attribute__((weak)) unsigned short weak_fn(unsigned short a, unsigned short b) { // expected-error {{weak function definitions}}
  return (unsigned short)(a + b);
}

__attribute__((weak)) unsigned short weak_var = 1; // expected-error {{weak variable definitions}}

// A tentative weak definition (file scope, no initializer) is still a weak
// DEFINITION -- it materializes a zero-initialized weak symbol -- and is
// rejected like any other. Only an extern declaration without storage in
// this TU stays accepted.
__attribute__((weak)) unsigned short weak_tentative; // expected-error {{weak variable definitions}}
extern __attribute__((weak)) unsigned short weak_only_decl;
unsigned short use_weak_decl(void) { return weak_only_decl; }

// -------------------------------------------------------------------- D1 --

void computed_goto(void **tab) {
  // expected-error@+1 {{computed goto (indirect 'goto *') is not supported on MCS251}}
  goto *tab[0];
}

void addr_of_label(void) {
  // expected-error@+1 {{computed goto (address-of-label '&&') is not supported on MCS251}}
  void *p = &&here;
here:
  (void)p;
}

// The ordinary switch statement is the supported alternative.
int ordinary_switch(int x) {
  switch (x) {
  case 1: return 1;
  case 2: return 2;
  default: return 0;
  }
}

// -------------------------------------------------------------------- A9 --

// expected-error@+1 {{module-level inline assembly is not supported on MCS251}}
__asm__(".globl myasm\nmyasm:\n ret\n");

// --------------------------------------------------------------------------
// Outside the rejected capability set the target keeps compiling normally.
int main(void) {
  ordinary_switch(1);
  supported_calls();
  macro_mmio();
  (void)volatile_access();
  (void)runtime_lock_free_query();
  return 0;
}
