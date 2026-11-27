// RUN: %clang --target=mcs251-unknown-none -std=c11 -O2 -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mcs251-unknown-none -std=c11 -O2 -c %s -o %t.O2.o
// RUN: %clang --target=mcs251-unknown-none -std=c11 -O0 -c %s -o %t.O0.o
// RUN: llvm-readobj -h %t.O2.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-objdump -d %t.O2.o | FileCheck %s --check-prefix=OBJ

// Verify C11 atomics are advertised (no __STDC_NO_ATOMICS__) and compile
// end-to-end into default ELF32BE objects using EA critical sections.
#ifdef __STDC_NO_ATOMICS__
#error "__STDC_NO_ATOMICS__ must not be defined when atomics are supported"
#endif

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

// ELF: Format: elf32-mcs251
// ELF: Arch: mcs251

_Atomic uint32_t g_counter = 0;
_Atomic uint16_t g_half = 0;
atomic_flag g_flag = ATOMIC_FLAG_INIT;

struct BigAtomic {
  uint32_t a;
  uint32_t b;
  uint32_t c;
};
_Atomic struct BigAtomic g_big;

// IR-LABEL: define dso_local i32 @inc_counter
// IR: atomicrmw add ptr @g_counter, i32 1 seq_cst
// OBJ-LABEL: <_inc_counter>:
// OBJ: jbc 0xaf
// OBJ: setb 0xaf
uint32_t inc_counter(void) {
  return atomic_fetch_add(&g_counter, 1u);
}

// IR-LABEL: define dso_local zeroext i1 @try_lock
// IR: atomicrmw xchg ptr @g_flag, i8 1 seq_cst
// OBJ-LABEL: <_try_lock>:
// OBJ: jbc 0xaf
// OBJ: setb 0xaf
bool try_lock(void) {
  return atomic_flag_test_and_set(&g_flag);
}

void unlock(void) {
  atomic_flag_clear(&g_flag);
}

// IR-LABEL: define dso_local {{.*}}i1 @cas_half
// IR: cmpxchg ptr @g_half, i16
// OBJ-LABEL: <_cas_half>:
// OBJ: jbc 0xaf
// OBJ: setb 0xaf
bool cas_half(uint16_t *exp, uint16_t des) {
  return atomic_compare_exchange_strong(&g_half, exp, des);
}

// GNU __sync_* builtins on ordinary integers.
// IR-LABEL: define dso_local i32 @sync_add
// IR: atomicrmw add ptr {{%.+}}, i32 {{%.+}} seq_cst, align 4
// OBJ-LABEL: <_sync_add>:
// OBJ: jbc 0xaf
// OBJ: setb 0xaf
uint32_t sync_add(uint32_t *p, uint32_t v) {
  return __sync_fetch_and_add(p, v);
}

// Fences emit zero external calls.
// IR-LABEL: define dso_local void @do_fences
// IR: fence seq_cst
// IR: fence syncscope("singlethread") acq_rel
// OBJ-LABEL: <_do_fences>:
// OBJ-NOT: ecall
// OBJ: eret
void do_fences(void) {
  atomic_thread_fence(memory_order_seq_cst);
  atomic_signal_fence(memory_order_acq_rel);
  __sync_synchronize();
}

// Large >4B _Atomic struct lowers to generic __atomic_load / __atomic_store
// (both non-power-of-2 12B and power-of-2 8B must avoid synthesizing i64 IR).
struct PairAtomic {
  uint32_t x;
  uint32_t y;
};
_Atomic struct PairAtomic g_pair;

// IR-LABEL: define dso_local void @copy_big
// IR: call addrspace(4) void @__atomic_load(i32 {{.*}}12,
// IR: call addrspace(4) void @__atomic_store(i32 {{.*}}12,
// OBJ-LABEL: <_copy_big>:
// OBJ: ecall
void copy_big(_Atomic struct BigAtomic *dst) {
  struct BigAtomic tmp = atomic_load(&g_big);
  atomic_store(dst, tmp);
}

// IR-LABEL: define dso_local zeroext i1 @update_pair8
// IR-NOT: i64
// IR: call addrspace(4) void @__atomic_load(i32 {{.*}}8,
// IR: call addrspace(4) void @__atomic_store(i32 {{.*}}8,
// IR: call zeroext addrspace(4) i1 @__atomic_compare_exchange(i32 {{.*}}8,
bool update_pair8(uint32_t delta) {
  struct PairAtomic snap = atomic_load(&g_pair);
  snap.x += delta;
  snap.y += delta;
  atomic_store(&g_pair, snap);
  struct PairAtomic expect = snap;
  struct PairAtomic desired = {snap.x + 1u, snap.y + 1u};
  return atomic_compare_exchange_strong(&g_pair, &expect, desired);
}

// Direct _Atomic lvalue read/write on byte-aligned 16-bit/32-bit globals must
// lower inline (not libcalls) via hasBuiltinAtomic().
// IR-LABEL: define dso_local zeroext i16 @direct_lvalue_half
// IR: load atomic i16, ptr @g_half seq_cst, align 1
// IR: store atomic i16 {{%.+}}, ptr @g_half seq_cst, align 1
uint16_t direct_lvalue_half(uint16_t v) {
  uint16_t old = g_half;
  g_half = v;
  return old;
}
