// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only \
// RUN:   -Wno-atomic-alignment -Wno-sync-alignment -verify %s

// X1-2/X1-5/X1-6/X1-7: source-level CODE-store coverage for the write forms
// that bypass the ordinary assignment check: builtin library calls with a
// destination write slot (memcpy/memmove/memset/strcpy family, POSIX
// bcopy/bzero whose destination slot differs, stpncpy), atomic stores and
// RMWs with their out/expected slots, __sync RMWs, asm output operands, and
// (X1-7) the swept remaining fixed-slot writers: strlcpy/strlcat/memccpy
// and their fortified forms, the sprintf/snprintf buffer family, the
// out-slot writers (checked arithmetic, addc carry, frexp/sincos), and
// wide-char copies. Loads and pure queries stay legal.
//
// First-phase boundary (not a permanent answer): a NON-builtin external
// call that receives a __code pointer is governed by a C-const-style
// contract, not by this check -- the callee must not write through it. If
// such a callee is compiled here and does write, the backend rejects the
// AS4 store fatally; hand-written assembly is the user's responsibility.
// Library functions reachable only through a header declaration ('f'
// builtins such as fread/strtok/strtod have no __builtin_ spelling on this
// headerless target) fall under the same contract; their table rows in
// SemaMCS251.cpp become live the day their declarations appear. Vararg
// writers (scanf family) and stream handles (fprintf/fopen) have no
// modeled fixed slot and stay contract-governed.

typedef unsigned long size_t;

char __code a[8];
__code int x;
char *p;

void memcpy_dst(char *s) {
  __builtin_memcpy(a, s, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void memmove_dst(char *s) {
  __builtin_memmove(a, s, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void memset_array(void) {
  __builtin_memset(a, 0, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void memset_pointer(void) {
  __builtin_memset(&x, 0, 4); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void strcpy_dst(char *s) {
  __builtin_strcpy(a, s); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void fortified_dst(char *s) {
  __builtin___memcpy_chk(a, s, 8, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}

void atomic_store(void) {
  __atomic_store_n(&x, 1, __ATOMIC_SEQ_CST); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void atomic_rmw(void) {
  __atomic_fetch_add(&x, 1, __ATOMIC_SEQ_CST); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void sync_test_and_set(void) {
  __sync_lock_test_and_set(&x, 1); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void sync_release(void) {
  __sync_lock_release(&x); // expected-error {{cannot store to an MCS251 '__code' object}}
}

void asm_output(void) {
  __asm__("" : "=r"(x)); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void asm_readwrite_output(void) {
  __asm__("" : "+r"(x)); // expected-error {{cannot store to an MCS251 '__code' object}}
}

// Reads and pure queries are legal.
int atomic_load(void) { return __atomic_load_n(&x, __ATOMIC_SEQ_CST); }
char load(unsigned i) { return a[i]; }
int lock_free(void) { return __atomic_is_lock_free(4, &x); }
void asm_input_only(void) { __asm__("" :: "r"(x)); }

// X1-5: the destination slot is not always argument 0. bcopy's destination
// is its second argument and bzero has no source at all; stpncpy (and its
// fortified variant) was missing from the family list. stpncpy/stpcpy return
// the destination, so the write treatment is the conservative reading.
void bcopy_dst(char *s) {
  __builtin_bcopy(s, a, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void bzero_dst(void) {
  __builtin_bzero(a, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void stpncpy_dst(char *s) {
  __builtin_stpncpy(a, s, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void stpncpy_chk_dst(char *s) {
  __builtin___stpncpy_chk(a, s, 8, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void strncpy_dst(char *s) {
  __builtin_strncpy(a, s, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
// The CODE object is only the read source: legal.
void bcopy_from_code(char *d) { __builtin_bcopy(a, d, 8); }
void bzero_generic(char *d) { __builtin_bzero(d, 8); }
void stpncpy_from_code(char *d) { __builtin_stpncpy(d, a, 8); }

// X1-6: the atomic out/expected slots are writes even when the first
// argument is an ordinary read; compare_exchange's expected write happens
// only on the failure path but is still a store into the CODE object.
int g;
int outg;
void atomic_load_out(void) {
  __atomic_load(&g, &x, __ATOMIC_SEQ_CST); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void atomic_exchange_out(void) {
  int in;
  __atomic_exchange(&g, &in, &x, __ATOMIC_SEQ_CST); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void atomic_compare_expected(void) {
  __atomic_compare_exchange_n(&g, &x, 1, 0, __ATOMIC_SEQ_CST, // expected-error {{cannot store to an MCS251 '__code' object}}
                              __ATOMIC_SEQ_CST);
}
// Generic-space objects everywhere: legal.
void atomic_load_generic_out(void) {
  __atomic_load(&g, &outg, __ATOMIC_SEQ_CST);
}
void atomic_compare_generic(void) {
  __atomic_compare_exchange_n(&g, &outg, 1, 0, __ATOMIC_SEQ_CST,
                              __ATOMIC_SEQ_CST);
}

//===----------------------------------------------------------------------===//
// X1-7: the systematic sweep of Builtins.td closed the fixed-slot writers
// outside the core family. Alice's X1-7 counterexamples lead the list.
//===----------------------------------------------------------------------===//
typedef __WCHAR_TYPE__ wchar_t_wide;
__code char ca[8];
__code wchar_t_wide cwa[8];
wchar_t_wide g_w[8];
__code int ci;
__code unsigned char cub;
__code double cs, cc;
__code char *ccharp;

void strlcpy_chk_dst(char *s) {
  __builtin___strlcpy_chk(ca, s, 8, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void memccpy_chk_dst(char *s) {
  __builtin___memccpy_chk(ca, s, 0, 8, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void strlcat_chk_dst(char *s) {
  __builtin___strlcat_chk(ca, s, 8, 8); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void sprintf_dst(int n) {
  __builtin_sprintf(ca, "%d", n); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void snprintf_dst(int n) {
  __builtin_snprintf(ca, 8, "%d", n); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void sprintf_chk_dst(int n) {
  __builtin___sprintf_chk(ca, 1, 8, "%d", n); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void wmemcpy_dst(void) {
  __builtin_wmemcpy(cwa, g_w, 4); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void wmemmove_dst(void) {
  __builtin_wmemmove(cwa, g_w, 4); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void addc_carry_out(void) {
  __builtin_addcb(1, 2, 0, &cub); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void checked_add_out(void) {
  __builtin_sadd_overflow(1, 2, &ci); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void frexp_exp_out(void) {
  __builtin_frexp(0.5, &ci); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void sincos_out(void) {
  __builtin_sincos(0.5, &cs, &cc); // expected-error {{cannot store to an MCS251 '__code' object}} \
                                   // expected-error {{cannot store to an MCS251 '__code' object}}
}
// __builtin_setjmp's env slot is also a modeled write, but a __code jmp_buf
// is already rejected earlier by the pointer-conversion check ("changes
// address space"), so no probe lives here.
// The unfortified memccpy has no __builtin_ spelling on this headerless
// target: without string.h it is not a frontend-callable builtin at all and
// falls under the external-call contract documented above.
void memccpy_no_chk(char *s) {
  __builtin_memccpy(ca, s, 0, 8); // expected-error {{use of unknown builtin '__builtin_memccpy'}}
}

//===----------------------------------------------------------------------===//
// X1-8: CustomTypeChecking/IgnoreSignature builtins carry placeholder type
// strings ("void(...)"), so the type-string-based X1-7 sweep misjudged them.
// The rows below come from an attribute-driven re-sweep (406 CustomTypeChecking
// + 20 IgnoreSignature + 96 MS-gated rows in the generated registration table)
// with each write slot taken from the SemaChecking custom checker.
//===----------------------------------------------------------------------===//
void add_overflow_out(int a, int b) {
  __builtin_add_overflow(a, b, &ci); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void sub_overflow_out(int a, int b) {
  __builtin_sub_overflow(a, b, &ci); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void mul_overflow_out(int a, int b) {
  __builtin_mul_overflow(a, b, &ci); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void atomic_max_fetch_rmw(void) {
  __atomic_max_fetch(&ci, 1, __ATOMIC_SEQ_CST); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void atomic_min_fetch_rmw(void) {
  __atomic_min_fetch(&ci, 1, __ATOMIC_SEQ_CST); // expected-error {{cannot store to an MCS251 '__code' object}}
}
// The scoped spellings normalize onto the shared atomic rows.
void scoped_store_n(void) {
  __scoped_atomic_store_n(&ci, 1, __ATOMIC_SEQ_CST, 0); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void scoped_store(void) {
  __scoped_atomic_store(&ci, 1, __ATOMIC_SEQ_CST, 0); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void scoped_exchange_n(void) {
  __scoped_atomic_exchange_n(&ci, 1, __ATOMIC_SEQ_CST, 0); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void scoped_fetch_or(void) {
  __scoped_atomic_fetch_or(&ci, 1, __ATOMIC_SEQ_CST, 0); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void scoped_compare_exchange_expected(void) {
  // The object (argument 0) and the expected pointer (argument 1) are both
  // writes, so this call is diagnosed twice.
  __scoped_atomic_compare_exchange_n(&ci, &ci, 1, 0, 5, 5, 0); // expected-error {{cannot store to an MCS251 '__code' object}} expected-error {{cannot store to an MCS251 '__code' object}}
}
// Reads across the scoped family stay legal.
int scoped_load_n(void) {
  return __scoped_atomic_load_n(&ci, __ATOMIC_SEQ_CST, 0);
}
int scoped_load_out(int *d) {
  __scoped_atomic_load(&g, d, __ATOMIC_SEQ_CST, 0);
  return g;
}
// C23/K&R va_start spellings store into the va_list (argument 0), so they
// normalize onto the same treatment as __builtin_va_start.
// (__builtin_c23_va_start is C23-language-gated and cannot appear in this
// -std=c11 file; the MS spellings are target-rejected, see the
// -fms-extensions test.)
// A __code va_list is already rejected earlier by the reference binding
// ("changes address space"), like the __code jmp_buf above, so the stdarg
// probe uses a generic list; the row covers the declared-later cases.
void stdarg_start_code(int n, ...) {
  __builtin_va_list l;
  __builtin_stdarg_start(l, n);
}
void stdarg_start_generic(int n, ...) {
  __builtin_va_list l2;
  __builtin_stdarg_start(l2, n);
}
// The IgnoreSignature setjmp family: sigsetjmp/savectx save into argument 0,
// so they carry rows. On this headerless target the call stays an ordinary
// external call (no builtin ID is assigned even with a user declaration), so
// the rows stay dormant exactly like the fread/strtok rows from X1-7 and the
// call falls under the external-call contract.
void sigsetjmp_env(void) {
  __builtin_sigsetjmp(&ci, 1); // expected-error {{use of unknown builtin '__builtin_sigsetjmp'}}
}
void savectx_env(void) {
  __builtin_savectx(&ci); // expected-error {{use of unknown builtin '__builtin_savectx'}}
}
// Fail-closed rows for the vector/matrix/log custom-checking families.
void nontemporal_store_ptr(void) {
  __builtin_nontemporal_store(1, &ci); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void nontemporal_load_ptr(void) {
  (void)__builtin_nontemporal_load(&ci); // read: legal
}

// The CODE object is only read or untouched: legal.
void strlcpy_from_code(char *d) { __builtin___strlcpy_chk(d, ca, 8, 8); }
void scoped_store_from_code(char *d) {
  __scoped_atomic_store_n((int *)d, 1, __ATOMIC_SEQ_CST, 0);
}
void scoped_max_fetch_generic(int *p) {
  __scoped_atomic_max_fetch(p, 1, __ATOMIC_SEQ_CST, 0);
}
void nontemporal_store_generic(int *p) { __builtin_nontemporal_store(1, p); }
void strlcat_generic_dst(char *s) { __builtin___strlcat_chk(s, ca, 8, 8); }
void wmemcpy_from_code(void) { __builtin_wmemcpy(g_w, cwa, 4); }
void checked_add_generic_out(void) { int r; __builtin_sadd_overflow(1, 2, &r); }
void frexp_generic_out(void) { int e; __builtin_frexp(0.5, &e); }

// Pure-read builtins keep accepting CODE strings (official demos do this).
unsigned read_controls(void) {
  unsigned n = 0;
  n += __builtin_strlen(ca);
  n += __builtin_strcmp(ca, "x") == 0;
  n += __builtin_strncmp(ca, "x", 2) == 0;
  n += __builtin_strchr(ca, 'x') != 0;
  n += __builtin_strrchr(ca, 'x') != 0;
  n += __builtin_index(ca, 'x') != 0;
  n += __builtin_rindex(ca, 'x') != 0;
  n += __builtin_memcmp(ca, "x", 2) == 0;
  n += __builtin_memchr(ca, 'x', 8) != 0;
  n += __builtin_strspn(ca, "x");
  n += __builtin_strcspn(ca, "x");
  n += __builtin_strstr(ca, "x") != 0;
  n += __builtin_strpbrk(ca, "x") != 0;
  n += __builtin_strcasecmp(ca, "x") == 0;
  n += __builtin_strncasecmp(ca, "x", 2) == 0;
  n += __builtin_object_size(ca, 0);
  return n;
}
