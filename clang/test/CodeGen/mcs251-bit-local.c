// Automatic-local / parameter MCS-251 `bit` values (P09 P-2, design section
// 3): a call-private bit object is an ordinary align-1 i8 alloca (never a
// bit-object global, never .mcs251.bit, never llvm.used), every defined
// source write normalizes through a non-zero comparison and a zext into the
// byte, every read decodes the full byte with `icmp ne ..., 0` (never a
// trunc's low-bit/odd-even semantics), and a volatile-qualified local uses
// ordinary volatile byte loads/stores that preserve the source access
// counts. The automatic value is outside the physical-bit RMW table: `b =
// ~b`, `b += x` and `b++` compile as ordinary integer/boolean value
// operations (Sema's auto/param exclusion is pinned in
// Sema/mcs251-bit-rmw.c; here the positive CodeGen forms are asserted).
//
// O0 asserts the raw emission points; the -O2 run leaves the alloca world
// (mem2reg) and still never fabricates bit-object storage or bit intrinsics.

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,O0
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O2 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,O2

typedef __bit BOOL2;
typedef bit BOOLK; // Keil spelling

// --- i8 alloca, normalized write, full-byte decode read (section 3.1) ---

// CHECK-LABEL: define {{.*}}void @init_dynamic
void init_dynamic(int x) {
  __bit b = x;
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[Z:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0-NEXT: store i8 [[Z]], ptr {{%[a-z0-9.]+}}
  if (b)
    x = 1;
  // O0: [[V:%[a-z0-9.]+]] = load i8, ptr {{%[a-z0-9.]+}}
  // O0: icmp ne i8 [[V]], 0
  // O0-NOT: trunc
  // At O2 the dead local is promoted away entirely: no alloca survives
  // between this label and the next one in the O2 dump.
  // O2-NOT: alloca
}

// Four spellings (plain __bit, Keil bare bit, Keil typedef, __bit typedef)
// and the cv variants are all plain i8 allocas: const stays an ordinary
// object, volatile keeps its declared access counts through volatile byte
// ops, and none of them becomes bit-object storage.
// CHECK-LABEL: define {{.*}}void @spellings_cv
void spellings_cv(int x) {
  __bit a = x;
  bit kb = x;
  BOOLK kd = x;
  BOOL2 bd = x;
  volatile __bit vb = x;
  const volatile BOOL2 cvb = x;
  (void)a; (void)kb; (void)kd; (void)bd; (void)vb; (void)cvb;
  // O0: alloca i8
  // O0: alloca i8
  // O0: alloca i8
  // O0: alloca i8
  // O0: alloca i8
  // O0: alloca i8
  // O0: store volatile i8 {{%[a-z0-9.]+}}, ptr {{%[a-z0-9.]+}}
  // CHECK-NOT: llvm.mcs251.bit
  // CHECK-NOT: "mcs251-bit-object"
  // CHECK-NOT: llvm.used
  // At O2 only the two volatile locals keep their byte storage: one shared
  // icmp/zext normalization (the source value is non-volatile), two volatile
  // byte stores, two volatile byte reads.
  // O2: [[NZ:%[a-z0-9.]+]] = icmp ne i32
  // O2-NEXT: [[Z:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O2: store volatile i8 [[Z]], ptr
  // O2: store volatile i8 [[Z]], ptr
  // O2-COUNT-2: load volatile i8
}

// --- volatile byte access counts (section 3.1) ---

// One dynamic source write plus one source read => exactly one volatile byte
// store of the normalized value and one volatile byte load.
// CHECK-LABEL: define {{.*}}void @volatile_counts
void volatile_counts(volatile __bit v, int x) {
  // The volatile parameter's entry ABI copy is itself a volatile store
  // (stock-clang behavior for volatile scalar params at O0).
  // O0: store volatile i8 {{%[a-z0-9.]+}}, ptr {{%[a-z0-9.]+}}
  v = x;
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[Z:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0-NEXT: store volatile i8 [[Z]], ptr {{%[a-z0-9.]+}}
  if (v)
    x = 1;
  // O0: [[L:%[a-z0-9.]+]] = load volatile i8, ptr {{%[a-z0-9.]+}}
  // O0: icmp ne i8 [[L]], 0
  // At O2 the access counts survive verbatim: entry-decode store, one
  // source-write store, one source-read load, all volatile byte ops.
  // O2: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: zext i1
  // O2-NEXT: store volatile i8
  // O2: icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: zext i1
  // O2-NEXT: store volatile i8
  // O2: load volatile i8
}

// Two reads of the same volatile local are two volatile byte loads.
// CHECK-LABEL: define {{.*}}i32 @volatile_reads
int volatile_reads(void) {
  volatile __bit v = 1;
  int r = 0;
  if (v) r += 1;
  if (v) r += 2;
  // O0: store volatile i8 1, ptr
  // O0: [[A:%[a-z0-9.]+]] = load volatile i8, ptr {{%[a-z0-9.]+}}
  // O0: icmp ne i8 [[A]], 0
  // O0: [[B:%[a-z0-9.]+]] = load volatile i8, ptr {{%[a-z0-9.]+}}
  // O0: icmp ne i8 [[B]], 0
  // O2: store volatile i8 1, ptr
  // O2: load volatile i8, ptr
  // O2: load volatile i8, ptr
  return r;
}

// --- automatic values are outside the physical RMW table (section 2.4) ---

// `b = ~b` on an automatic is an ordinary value computation (promote, xor,
// normalize back); never an obj.toggle and never rejected.
// CHECK-LABEL: define {{.*}}void @auto_not
void auto_not(void) {
  __bit b = 0;
  b = ~b;
  // CHECK-NOT: llvm.mcs251.bit
}

// `b += x` and `b++`/`++b` likewise.
// CHECK-LABEL: define {{.*}}void @auto_rmw
void auto_rmw(int x) {
  __bit b = x;
  b += x;
  b++;
  ++b;
  // CHECK-NOT: llvm.mcs251.bit
}

// --- uninitialized automatics follow C rules: no forced zeroing (3.1) ---

// CHECK-LABEL: define {{.*}}i32 @uninit_no_zeroing
int uninit_no_zeroing(void) {
  __bit u;
  int r = 0;
  if (u) r = 1;
  // O0-NOT: store i8 {{%[a-z0-9.]+}}, ptr
  // O0: load i8, ptr
  return r;
}

// --- spills across a helper call use the ordinary frame (section 3.1) ---
// A bit local live across a call is a plain i8 stack slot; no BSEG, no bit
// bank, no packing with neighbours.
// CHECK-LABEL: define {{.*}}void @live_across_call
extern int helper(int);
void live_across_call(int x) {
  __bit b = x;
  int r = helper(x);
  if (b) r += 1;
  (void)r;
  // O0: [[Z:%[a-z0-9.]+]] = zext i1 {{%[a-z0-9.]+}} to i8
  // O0: store i8 [[Z]], ptr
  // O0: call {{.*}}i32 @helper
  // O0: load i8, ptr
  // CHECK-NOT: llvm.mcs251.bit
  // At O2 the (dead) bit local is gone; only the ordinary helper call
  // remains in the dump.
  // O2: call {{.*}}i32 @helper
}
