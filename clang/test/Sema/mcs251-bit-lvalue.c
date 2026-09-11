// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify=c %t/main.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fforce-enable-int128 -fsyntax-only -verify=wide %t/wide.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -x c++ -std=c++17 -fsyntax-only -verify=cxx %t/main.cpp

// L1 controlled fixed bit lvalue builtin: the argument must be an integer
// constant expression in the bit-address space 0-255 (address 0 is legal), and
// the call yields a controlled, volatile, non-addressable bit lvalue (BT04).
// The builtin is C-only (P08).

//--- main.c
#define BIT_ADDR 0x24
#define BASE 0x88
#define IDX 2

void ok(void) {
  __bit a = __builtin_mcs251_bit_lvalue(BIT_ADDR);
  __bit b = __builtin_mcs251_bit_lvalue(0);
  __bit c = __builtin_mcs251_bit_lvalue(255);
  __bit d = __builtin_mcs251_bit_lvalue(BASE ^ IDX);
  __bit e = __builtin_mcs251_bit_lvalue((1 + 2) * 3);
  (void)a; (void)b; (void)c; (void)d; (void)e;
}

// The builtin is an assignable controlled lvalue, unlike an ordinary call.
void lvalue_writes(void) {
  __builtin_mcs251_bit_lvalue(0x24) = 1;
  __builtin_mcs251_bit_lvalue(0x24) = 0;
  // The CPL toggle form is allowed as a discarded-value statement.
  __builtin_mcs251_bit_lvalue(0x24) ^= 1;
  __builtin_mcs251_bit_lvalue(0x24) = !__builtin_mcs251_bit_lvalue(0x24);
}

// It is still not addressable.
void not_addressable(void) {
  __bit *p = &__builtin_mcs251_bit_lvalue(0x24); // c-error {{cannot form a pointer to MCS251 'bit' type '__bit'}} c-error {{cannot take the address of an MCS251 'bit' object}}
  (void)p;
}

// Using the result of a controlled bit toggle is rejected.
int toggle_result_used(void) {
  return (__builtin_mcs251_bit_lvalue(0x24) ^= 1); // c-error {{the result of a controlled MCS251 bit toggle cannot be used; the toggle is only valid as a discarded-value expression}}
}

// A non-constant argument is rejected.
void non_constant(int runtime_addr) {
  __bit a = __builtin_mcs251_bit_lvalue(runtime_addr); // c-error {{bit address argument to __builtin_mcs251_bit_lvalue must be an integer constant expression}}
  (void)a;
}

// Out-of-range constants, checked on the full value before narrowing.
void out_of_range(void) {
  __bit a = __builtin_mcs251_bit_lvalue(-1);              // c-error {{bit address -1 is outside the valid range 0-255}}
  __bit b = __builtin_mcs251_bit_lvalue(256);             // c-error {{bit address 256 is outside the valid range 0-255}}
  __bit c = __builtin_mcs251_bit_lvalue(0x100000001ULL);  // c-error {{bit address 4294967297 is outside the valid range 0-255}}
  __bit d = __builtin_mcs251_bit_lvalue(0xffffffffffffffffULL); // c-error {{bit address 18446744073709551615 is outside the valid range 0-255}}
  (void)a; (void)b; (void)c; (void)d;
}

// Wrong argument count.
void wrong_arity(void) {
  __bit a = __builtin_mcs251_bit_lvalue();     // c-error {{too few arguments to function call, expected 1, have 0}}
  __bit b = __builtin_mcs251_bit_lvalue(1, 2); // c-error {{too many arguments to function call, expected 1, have 2}}
  (void)a; (void)b;
}

//--- main.cpp
// C++ is not a supported language mode for the bit dialect (P08): the builtin
// is rejected explicitly rather than silently accepted with a `__bit` result.
void cpp_use() {
  auto b = __builtin_mcs251_bit_lvalue(0x24); // cxx-error {{MCS251 bit support is not available in C++ mode; '__builtin_mcs251_bit_lvalue' requires C}}
  (void)b;
}

//--- wide.c
// The raw integer constant expression is checked on its true width, before any
// implicit narrowing to a parameter type. A 128-bit value whose low 8 bits are
// in range must still be rejected.
void wide(void) {
  __bit a = __builtin_mcs251_bit_lvalue(((unsigned __int128)1 << 64) + 1); // wide-error {{bit address 18446744073709551617 is outside the valid range 0-255}}
  __bit b = __builtin_mcs251_bit_lvalue((unsigned __int128)0xff); // ok: exactly 255
  (void)a; (void)b;
}
