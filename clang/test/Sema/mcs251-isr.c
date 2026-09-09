// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

#define VEC (24)
void good(void) __attribute__((interrupt(VEC)));
void good(void) {}

void neg(void) __attribute__((interrupt(-1))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void gap(void) __attribute__((interrupt(7))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void transfer(void) __attribute__((interrupt(13))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void system14(void) __attribute__((interrupt(14))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void system15(void) __attribute__((interrupt(15))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void high(void) __attribute__((interrupt(52))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void wide(void) __attribute__((interrupt(0x100000001ULL))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}

int runtime_slot;
void nonice(void) __attribute__((interrupt(runtime_slot))); // expected-error {{integer constant expression}}
void zero(void) __attribute__((interrupt)); // expected-error {{takes one argument}}
void two(void) __attribute__((interrupt(1, 2))); // expected-error {{takes one argument}}

int badret(void) __attribute__((interrupt(1))); // expected-error {{MCS251 interrupt function must have type void(void)}}
void arg(int x) __attribute__((interrupt(2))); // expected-error {{MCS251 interrupt function must have type void(void)}}
void variadic(int x, ...) __attribute__((interrupt(3))); // expected-error {{MCS251 interrupt function must have type void(void)}}

void late(void);
void late(void) __attribute__((interrupt(4))); // expected-error {{MCS251 interrupt identity must be established on the first declaration}}

void mismatch(void) __attribute__((interrupt(5)));
void mismatch(void) __attribute__((interrupt(6))); // expected-error {{conflicting MCS251 interrupt vector}}

void duplicate_a(void) __attribute__((interrupt(8)));
void duplicate_a(void) {}
void duplicate_b(void) __attribute__((interrupt(8)));
void duplicate_b(void) {} // expected-error {{duplicate MCS251 interrupt vector}}

void naked_isr(void) __attribute__((interrupt(9), naked)); // expected-error {{incompatible with MCS251 interrupt}}
void inline_isr(void) __attribute__((interrupt(10), always_inline)); // expected-error {{incompatible with MCS251 interrupt}}

void use(void) {
  good(); // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  void (*p)(void) = good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  (void)&good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  (void)(unsigned long)&good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
}
