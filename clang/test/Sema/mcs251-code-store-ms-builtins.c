// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fms-extensions \
// RUN:   -fsyntax-only -Wno-atomic-alignment -Wno-sync-alignment -verify %s

// X1-9: language gating is not unreachability. These builtins register only
// under -fms-extensions, but with the extension enabled they are ordinary
// builtin calls on this target and store through their first argument, so the
// write-slot table covers them (Alice: _bittestandset emitted an AS4 store,
// rc=0). While the gate is off the names never resolve to builtin IDs and the
// rows are inert. Host non-regression for the shared rows lives in
// mcs251-code-store-host-gate.c.

long __code cl;
long g_l;
char __code cb;

void bittestandset(void) { _bittestandset(&cl, 0); } // expected-error {{cannot store to an MCS251 '__code' object}}
void bittestandreset(void) { _bittestandreset(&cl, 0); } // expected-error {{cannot store to an MCS251 '__code' object}}
void bittestandcomplement(void) { _bittestandcomplement(&cl, 0); } // expected-error {{cannot store to an MCS251 '__code' object}}
void interlockedbittestandset(void) { _interlockedbittestandset(&cl, 0); } // expected-error {{cannot store to an MCS251 '__code' object}}
void interlocked_exchange(void) { _InterlockedExchange(&cl, 1); } // expected-error {{cannot store to an MCS251 '__code' object}}
void interlocked_increment(void) { _InterlockedIncrement(&cl); } // expected-error {{cannot store to an MCS251 '__code' object}}
void interlocked_compare_exchange(void) { _InterlockedCompareExchange(&cl, 1, 0); } // expected-error {{cannot store to an MCS251 '__code' object}}
void iso_volatile_store(void) { __iso_volatile_store8(&cb, 1); } // expected-error {{cannot store to an MCS251 '__code' object}}
// The ms_va family is target-restricted to x86-64/aarch64 in SemaChecking
// (and __builtin_ms_va_list does not exist on this ABI), so its rows are
// inert here; the availability error pins that audit conclusion.
void ms_va_start_f(int a, ...) {
  __builtin_va_list l;
  __builtin_ms_va_start(l, a); // expected-error {{this builtin is only available on x86-64 and aarch64 targets}}
}

// The generic destination stays legal; the CODE object as read source too.
void bittest_generic(void) { _bittestandset(&g_l, 0); }
void bittest_read(long *p) { (void)_bittest(p, 0); } // const pointer: a read
void interlocked_from_code(long *p) { _InterlockedExchange(&g_l, (long)cl); }
void iso_volatile_load(char *p) { (void)__iso_volatile_load8(p); }
// Extension builtins without a pointer argument never touch CODE.
void no_pointer(void) { __debugbreak(); }
