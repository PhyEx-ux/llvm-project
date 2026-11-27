// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -Wno-deprecated-non-prototype -verify %s
//
// Stack-based reentrant variadic ABI Sema gates:
//  - Multi-argument indirect calls, indirect variadic calls, arbitrary variadic
//    argument counts (>6), and struct/union variadic arguments + va_arg are
//    supported by the stack-based ABI.
//  - Still rejected: integers wider than 32 bits (`long long`) and pointers
//    into non-ordinary address spaces (AS5/AS7) in variadic argument or va_arg
//    positions, plus `__bit` in fixed variadic signatures (N13).

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
void indirect_fixed_supported(void) {
  fixedfp fp = 0;
  // Multi-argument indirect calls are supported under the stack-based ABI.
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
// [1] More than 6 variadic arguments are now accepted (stack-based ABI).
// ---------------------------------------------------------------------------

void cap_7_variadic_args(void) {
  vsum(1, 1, 2, 3, 4, 5, 6, 7);
}

void cap_8_variadic_args(void) {
  vsum(0, 1, 2, 3, 4, 5, 6, 7, 8);
}

// ---------------------------------------------------------------------------
// [2] Unprototyped calls with >7 arguments are now accepted.
// ---------------------------------------------------------------------------

int noproto();

void cap_noproto_boundary_ok(void) {
  noproto(1, 2, 3, 4, 5, 6, 7);
}

void cap_noproto_8_args(void) {
  noproto(1, 2, 3, 4, 5, 6, 7, 8);
}

// ---------------------------------------------------------------------------
// [3] Indirect variadic calls are now accepted (stack-based ABI).
// ---------------------------------------------------------------------------

typedef int (*vfuncptr)(int, ...);

void indirect_zero_variadic_args(void) {
  vfuncptr vp = 0;
  (*vp)(1);
}

void indirect_one_variadic_arg(void) {
  vfuncptr vp = 0;
  vp(1, 2);
}

void indirect_many_args(void) {
  vfuncptr vp = 0;
  vp(1, 2, 3, 4, 5, 6, 7, 8);
}

void indirect_through_typeof(void) {
  __typeof__(vsum) *dp = vsum;
  dp(1, 2);
}

// ---------------------------------------------------------------------------
// [4] Aggregate/union variadic arguments are now accepted (byval on stack).
// ---------------------------------------------------------------------------

struct S { int a; };
union U { int a; float f; };

void agg_variadic_arg(void) {
  struct S s = {1};
  vsum(1, s);
}

void union_variadic_arg(void) {
  union U u = {1};
  vsum(1, u);
}

// The struct argument is accepted; only the i64 (`long long`) argument fails.
void two_args_one_rejected(void) {
  struct S s = {1};
  long long v = 1;
  vsum(1, s, v); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
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

// G11 D5 / placement design §1.1: unmapped AS5 is now rejected at the
// declaration by err_mcs251_address_space_unavailable, before variadic use.
void bad_as_variadic_args(__attribute__((address_space(5))) int *p5, // expected-error {{address space 5 is not defined by the MCS-251 memory contract}}
                          __attribute__((address_space(7))) char *p7) {
  vsum(1, p5);
  vsum(1, p7); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

__attribute__((address_space(5))) int bad_a5[2]; // expected-error {{address space 5 is not defined by the MCS-251 memory contract}}
__attribute__((address_space(7))) char bad_a7[2];

void bad_as_array_variadic_args(void) {
  // AS5 was rejected at declaration; mapped AS7 still reaches the variadic
  // pointer gate after array-to-pointer decay.
  vsum(1, bad_a5);
  vsum(1, bad_a7); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
}

// ---------------------------------------------------------------------------
// [7][8][9] N18/D2: the same rejection set on the va_arg target type.
// ---------------------------------------------------------------------------

typedef __attribute__((address_space(7))) int *as7ptr;

void vaarg_rejected_targets(int count, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, count);
  (void)__builtin_va_arg(ap, struct S);
  (void)__builtin_va_arg(ap, union U);
  __builtin_va_arg(ap, long long); // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  __builtin_va_arg(ap, as7ptr);   // expected-error {{MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer}}
  __builtin_va_end(ap);
}

// ---------------------------------------------------------------------------
// [10] N13 regression anchor: bit-in-variadic-signature stays frozen with the
// P09 family; the G2 gates neither shadow nor reword it.
// ---------------------------------------------------------------------------

int bit_variadic(int a, __bit b, ...); // expected-error {{MCS251 bit call form 'variadic' is not supported}}
