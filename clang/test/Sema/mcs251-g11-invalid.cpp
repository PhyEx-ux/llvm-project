// RUN: %clang_cc1 -triple mcs251 -std=c++17 -fsyntax-only -verify %s
struct S {
  int member __attribute__((mcu_place_at(0x100))); // expected-error {{'mcu_place_at' attribute only applies to variables and functions}}
};
template <int N> struct Dependent {
  static int value __attribute__((mcu_place_at(N))); // expected-error {{expression is not an integer constant expression}}
};
void parameter(int p __attribute__((mcu_place_at(0x200)))); // expected-error {{`mcu::place_at` requires a static object or function definition}}

// The automatic/static boundary goes through VarDecl::hasLocalStorage: a
// function-local `static` object has static storage duration and is accepted
// (§2.2 allows static-storage placement and rejects only automatic objects),
// while an automatic object, a parameter and a non-static data member are all
// rejected. The place/noinit/retain policy of the accepted local static must
// reach the emitted global (CodeGen/mcs251-g11-local-static.c).
void locals(void) {
  static int ok __attribute__((mcu_place_at(0x300)));
  static int ok_noinit __attribute__((mcu_place_at(0x310), noinit));
  int automatic __attribute__((mcu_place_at(0x320))); // expected-error {{`mcu::place_at` requires a static object or function definition}}
  static int noinit_only __attribute__((noinit)); // expected-error {{`noinit` requires a fixed placement (`place_at`) in this profile}}
  static int noinit_init __attribute__((mcu_place_at(0x330), noinit)) = 1; // expected-error {{`noinit` cannot be combined with an initializer}}
  static int bound __attribute__((mcu_bind_at(0x340))); // expected-error {{`mcu::bind_at` requires an entity with external linkage}}
}
