// WP4 revision round 4: the evaluation-aware atomic/absolute-address decision
// must not depend on the spelling that hides the operand. Four shapes were
// measured as wrongly decided; this file is the ACCEPTED half at the OBJECT
// level (a real ELF object at both optimizations), and the rejected half is
// re-run by the harness below with an asserted exit status.
//
// Accepted here, rejected when the same operand is made genuinely evaluated:
//   (1) GNU `a ?: b` -- the false arm of a non-zero constant common expression
//       is dead; the common expression itself IS evaluated and is refused;
//   (2) __builtin_offsetof -- a plain member designator is not an operation;
//       an array-subscript INDEX is an evaluated operand and is refused;
//   (3) a statement expression in an unevaluated / untaken position never
//       runs; the same statement expression in value position is refused;
//   (4) A2 -- a comma expression's value is its RIGHT operand, so an
//       absolute-address cast in the discarded LEFT operand is not the
//       initializer's value; the same cast in the right operand is refused.
//
// The Sema-level half lives in clang/test/Sema/mcs251-capability-diagnostics.c.
//
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu11 -mllvm -mcs251-object-format=elf -O0 -emit-obj %s -o %t.O0.o
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu11 -mllvm -mcs251-object-format=elf -O2 -emit-obj %s -o %t.O2.o
// RUN: %python %S/Inputs/mcs251-r4-obj-check.py %t.O0.o %t.O2.o -- %clang_cc1

// --- (1) GNU `a ?: b`: the dead false arm is not evaluated ------------------
int gnu_cond_dead_false(int x) { return 1 ?: (__atomic_signal_fence(0), x); }

// --- (2) __builtin_offsetof: a plain member designator is not an operation --
struct of_target { int a[8]; };
int offsetof_plain(void) { return __builtin_offsetof(struct of_target, a); }

// --- (3) a statement expression that never runs -----------------------------
int stmt_expr_unevaluated(int x) {
  return sizeof(({ __atomic_signal_fence(0); 0; })) +
         (1 ? 1 : ({ __atomic_signal_fence(0); 0; })) + x;
}

// --- (4) A2: a comma expression's VALUE is its right operand ---------------
int *a2_comma_discarded_lhs = ((int *)0x1234, (int *)0);

// The ordinary supported static pointer forms keep compiling.
int *a2_null = 0;
int a2_target;
int *a2_symbol = &a2_target;
