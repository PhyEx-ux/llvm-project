// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c11 -fsyntax-only \
// RUN:   -Wno-atomic-alignment %s

// X1-1: every MCS251-specific check is gated on the target. A legal
// `address_space(4)` variable on another target keeps its ordinary
// assignment, ++/--, builtin-call and asm behavior (host_as4 probe).

int __attribute__((address_space(4))) x;

int f(void) {
  x = 1;
  ++x;
  __atomic_store_n(&x, 1, __ATOMIC_SEQ_CST);
  __builtin_memset(&x, 0, 4);
  return x;
}

void g(void) { __asm__("" : "=r"(x)); }

char *s = "text";

// X1-5/X1-6 forms: also ordinary calls on a host target. bcopy/bzero take
// void* destinations, and the atomic out/expected slots match the first
// argument's address space.
int in;
int __attribute__((address_space(4))) out;

void h(void) {
  __builtin_bcopy(&in, &x, 4);
  __builtin_bzero(&x, 4);
  __atomic_load(&x, &out, __ATOMIC_SEQ_CST);
  __atomic_compare_exchange_n(&x, &out, 1, 0, __ATOMIC_SEQ_CST,
                              __ATOMIC_SEQ_CST);
}

// X1-8/X1-9 rows behave as ordinary calls on a host target too: the
// width-generic overflow forms, the max/min-fetch RMWs, the scoped spellings
// and the fail-closed vector rows never fire off the MCS251 target.
void i(int a, int b) { __builtin_add_overflow(a, b, &x); }
void j(int a, int b) { __builtin_mul_overflow(a, b, &out); }
void k(void) { __atomic_max_fetch(&x, 1, __ATOMIC_SEQ_CST); }
void l(void) { __scoped_atomic_store_n(&x, 1, __ATOMIC_SEQ_CST, 0); }
void m(void) { __atomic_min_fetch(&x, 1, __ATOMIC_SEQ_CST); }
void n(void) { __builtin_nontemporal_store(1, &x); }

// X1-10 rows: the z/OS va_list spellings and objc_memmove_collectable are
// ordinary calls off the MCS251 target, __code objects included (the type
// alias maps to a plain char** pair here).
void o(char **p) { __builtin_zos_va_end(p); }
void p2(char **dst, char **src) { __builtin_zos_va_copy(dst, src); }
void q(char * __attribute__((address_space(4))) *dst, char **src) {
  __builtin_zos_va_end(dst);
  __builtin_zos_va_copy(dst, src);
  __builtin_objc_memmove_collectable(dst, src, 8);
}
