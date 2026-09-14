// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -Wno-deprecated-non-prototype -verify %s
//
// G2 B-S1 (design draft G2-VARIADIC-DESIGN-draft.md R3, §6 B-S1 + §4.4
// N16-N18): the frozen Sema gates of the B1 static-slot variadic ABI.
//
//  - N16/C1: an indirect variadic call (function pointer whose type contains
//    `...`) is rejected regardless of the argument count;
//  - N17/A: a call passing more than the fixed six variadic arguments is a
//    compile-time hard error (prototype form: args minus fixed params;
//    unprototyped form: args minus the register-channel first argument);
//  - N18/D1+D2: an argument in a variadic position, or a va_arg target type,
//    must not be in the frozen rejection set (aggregate/union, integers
//    wider than 32 bits, pointers into non-ordinary address spaces).
//
// The diagnostic messages are frozen verbatim; this file pins every one of
// them character-for-character. bit-in-variadic-signature stays with the
// separate P09 N13 family (regression anchor at the bottom). The llc
// backstops (C2/D3) and the va_list/va_arg lowering are B-S2 and are NOT
// exercised here. -Wno-deprecated-non-prototype keeps the unprototyped-cap
// cases focused on the frozen G2 gates (the deprecation is orthogonal).

// ---------------------------------------------------------------------------
// Positive controls: the allowed set must stay accepted at the Sema layer.
// ---------------------------------------------------------------------------

int vsum(int n, ...);

void ok_prototype_calls(void) {
  vsum(1);                                 // zero variadic arguments
  vsum(2, 10);                             // one
  vsum(6, 1, 2, 3, 4, 5, 6);               // exactly the six-slot cap
  vsum(4, 'x', (short)2, 3, 3.0f);         // promoted i8/i16/i32/f32
  vsum(3, 4.0, "literal", &ok_prototype_calls); // double==f32; ordinary ptrs
}

void ok_widths(long l, unsigned long ul, float f, double d) {
  // long is 32 bits on this target (only `long long` is an i64).
  vsum(2, l, ul);
  vsum(2, f, d);
}

void ok_addrspace_ptrs(__attribute__((address_space(3))) int *p3,
                       __attribute__((address_space(4))) char *p4) {
  // Target AS 3 (__xdata) and AS 4 (__code) are ordinary pointer ABIs.
  vsum(2, p3, p4);
}

__attribute__((address_space(2))) int ok_a2[2];
__attribute__((address_space(3))) char ok_a3[2];
__attribute__((address_space(4))) int ok_a4[2];

void ok_arrays_decay_to_ordinary_ptrs(int a0[2]) {
  // An array argument decays to a pointer before it reaches the variadic
  // slot, so the judgement type is the decayed pointer type: ordinary-space
  // arrays decay to ordinary pointers and stay accepted.
  vsum(2, a0, ok_a2);
  vsum(2, ok_a3, ok_a4);
}

typedef int (*fixedfp)(int, int);
void ok_indirect_fixed(void) {
  fixedfp fp = 0;
  // Only variadic *signatures* are rejected through pointers; an indirect
  // call to a non-variadic prototype keeps its existing behavior.
  fp(1, 2);
}

void ok_vaarg_reads(int count, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, count);
  int i = __builtin_va_arg(ap, int);
  long l = __builtin_va_arg(ap, long);
  double d = __builtin_va_arg(ap, double);
  char *p = __builtin_va_arg(ap, char *);
  __builtin_va_end(ap);
  (void)i; (void)l; (void)d; (void)p;
}

// ---------------------------------------------------------------------------
// [1] N17/A, prototype form: Args.size() - NumParams > 6.
// ---------------------------------------------------------------------------

void cap_7_variadic_args(void) {
  // expected-error@+1 {{MCS251 variadic call exceeds the fixed 6-slot variadic ABI cap (7 variadic arguments given)}}
  vsum(1, 1, 2, 3, 4, 5, 6, 7);
}

void cap_8_variadic_args(void) {
  // expected-error@+1 {{MCS251 variadic call exceeds the fixed 6-slot variadic ABI cap (8 variadic arguments given)}}
  vsum(0, 1, 2, 3, 4, 5, 6, 7, 8);
}

// ---------------------------------------------------------------------------
// [2] N17/A, unprototyped form: Args.size() - 1 > 6.
// ---------------------------------------------------------------------------

int noproto();

void cap_noproto_boundary_ok(void) {
  // Seven arguments: the first occupies the register channel, the remaining
  // six exactly fill the continuation slots.
  noproto(1, 2, 3, 4, 5, 6, 7);
}

void cap_noproto_8_args(void) {
  // expected-error@+1 {{MCS251 variadic call exceeds the fixed 6-slot variadic ABI cap (7 variadic arguments given)}}
  noproto(1, 2, 3, 4, 5, 6, 7, 8);
}

// ---------------------------------------------------------------------------
// [3] N16/C1: indirect variadic calls, rejected regardless of arg count.
// ---------------------------------------------------------------------------

typedef int (*vfuncptr)(int, ...);

void indirect_zero_variadic_args(void) {
  vfuncptr vp = 0;
  // expected-error@+1 {{MCS251 variadic call form 'indirect' is not supported (static slots require a named callee)}}
  (*vp)(1);
}

void indirect_one_variadic_arg(void) {
  vfuncptr vp = 0;
  // expected-error@+1 {{MCS251 variadic call form 'indirect' is not supported (static slots require a named callee)}}
  vp(1, 2);
}

void indirect_many_args(void) {
  vfuncptr vp = 0;
  // Even a call that would also breach the cap reports C1: the indirect
  // form is the frozen first gate.
  // expected-error@+1 {{MCS251 variadic call form 'indirect' is not supported (static slots require a named callee)}}
  vp(1, 2, 3, 4, 5, 6, 7, 8);
}

void indirect_through_typeof(void) {
  __typeof__(vsum) *dp = vsum;
  // expected-error@+1 {{MCS251 variadic call form 'indirect' is not supported (static slots require a named callee)}}
  dp(1, 2);
}

// ---------------------------------------------------------------------------
// [4] N18/D1: aggregate/union variadic arguments (direct prototype call).
// ---------------------------------------------------------------------------

struct S { int a; };
union U { int a; float f; };

void agg_variadic_arg(void) {
  struct S s = {1};
  vsum(1, s); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

void union_variadic_arg(void) {
  union U u = {1};
  vsum(1, u); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

// Each offending variadic position is diagnosed on its own.
void two_rejected_args(void) {
  struct S s = {1};
  long long v = 1;
  vsum(1, s, v); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}} expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

// ---------------------------------------------------------------------------
// [5] N18/D1: i64 (`long long`) variadic arguments.
// ---------------------------------------------------------------------------

void i64_variadic_arg(long long v) {
  vsum(1, v); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  vsum(1, 1ULL); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

// ---------------------------------------------------------------------------
// [6] N18/D1: pointers into non-ordinary address spaces (AS5/AS7), including
//     arrays that decay into such pointers.
// ---------------------------------------------------------------------------

void bad_as_variadic_args(__attribute__((address_space(5))) int *p5,
                          __attribute__((address_space(7))) char *p7) {
  vsum(1, p5); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  vsum(1, p7); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

__attribute__((address_space(5))) int bad_a5[2];
__attribute__((address_space(7))) char bad_a7[2];

void bad_as_array_variadic_args(void) {
  // The as-written type is an array, but the argument decays to an AS5/AS7
  // pointer in the variadic slot, so the same rejection applies.
  vsum(1, bad_a5); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  vsum(1, bad_a7); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

// ---------------------------------------------------------------------------
// [7][8][9] N18/D2: the same rejection set on the va_arg target type.
// ---------------------------------------------------------------------------

typedef __attribute__((address_space(7))) int *as7ptr;

void vaarg_rejected_targets(int count, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, count);
  __builtin_va_arg(ap, struct S); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  __builtin_va_arg(ap, union U);  // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  __builtin_va_arg(ap, long long); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  __builtin_va_arg(ap, as7ptr);   // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  __builtin_va_end(ap);
}

// ---------------------------------------------------------------------------
// [10] N13 regression anchor: bit-in-variadic-signature stays frozen with the
// P09 family; the G2 gates neither shadow nor reword it.
// ---------------------------------------------------------------------------

int bit_variadic(int a, __bit b, ...); // expected-error {{MCS251 bit call form 'variadic' is not supported}}
