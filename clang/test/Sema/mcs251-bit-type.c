// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -ast-dump %s 2>&1 | FileCheck %s

// expected-no-diagnostics
//
// Independent MCS-251 `__bit` target scalar type: parsing, typedef, cv and
// linkage, plus the value semantics (only 0/1, integer-nonzero -> 1,
// zero-extension to int, promotion to int).

// CHECK: TypedefDecl {{.*}} BOOL '__bit'
typedef __bit BOOL;

// CHECK: VarDecl {{.*}} g1 '__bit'
__bit g1;
// CHECK: VarDecl {{.*}} g2 '__bit' extern
extern __bit g2;
// CHECK: VarDecl {{.*}} g3 '__bit' static
static __bit g3;
// CHECK: VarDecl {{.*}} g4 'const __bit'
const __bit g4;

// A defined function boundary takes and returns the bit type.
__bit identity(__bit b) { return b; }
// A definition without a parameter name uses implicit-int style; use a
// prototype to exercise the unnamed form.
__bit unnamed(__bit);
// Function pointers whose signature contains a bit value are allowed (this is
// not a pointer to a bit object).
typedef __bit (*bit_fn)(__bit);
bit_fn fp;

// 0/1 value semantics: assignment from integer is a non-zero test, not a
// truncation.
void conversions(void) {
  __bit b;
  b = 0;  // CHECK: ImplicitCastExpr {{.*}} '__bit' <IntegralToBoolean>
  b = 1;  // CHECK: ImplicitCastExpr {{.*}} '__bit' <IntegralToBoolean>
  b = 2;  // CHECK: ImplicitCastExpr {{.*}} '__bit' <IntegralToBoolean>
  b = -1; // CHECK: ImplicitCastExpr {{.*}} '__bit' <IntegralToBoolean>
  // bit -> int zero-extends / promotes.
  int i = b;    // CHECK: ImplicitCastExpr {{.*}} 'int' <IntegralCast>
  short s = b;  // CHECK: ImplicitCastExpr {{.*}} 'short' <IntegralCast>
  long l = b;   // CHECK: ImplicitCastExpr {{.*}} 'long' <IntegralCast>
  // Arithmetic and comparison go through integer promotion.
  int sum = b + 1;
  int eq = (b == 1);
  int logic = b && 1;
  int neg = !b;
  (void)i; (void)s; (void)l; (void)sum; (void)eq; (void)logic; (void)neg;
}

// bit is a distinct type, not _Bool: the two can be told apart at the type
// level by taking a function that takes each.
void take_bool(_Bool);
void take_bit(__bit);
void dispatch(void) {
  take_bool(1);
  take_bit(1);
}

// A typedef'd bit keeps its identity.
void through_typedef(BOOL x) {
  __bit y = x;
  (void)y;
}

// Same-TU tentative declarations merge normally.
__bit tentative;
__bit tentative;
