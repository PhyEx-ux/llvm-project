// MCS-251 integer <-> `bit` value conversions (P09 P-3, design section 4.2):
// every source conversion from a wider integer into a bit value normalizes
// through a FULL-WIDTH `icmp ne <width> %v, 0` followed by `zext i1 -> i8`
// before the i8 boundary -- the design's frozen load-bearing chain. A
// `trunc`-then-test lowering would give 2 -> 0 (low-bit semantics) and is a
// miscompile, so each conversion function carries a function-scoped
// `CHECK-NOT: trunc` (scoped by CHECK-LABEL, never global: ordinary integer
// narrowing elsewhere still truncs legally, which the positive control at
// the bottom pins). Constants 2 / 0x80 / 0x100 / -1 / 0x80000000 all
// normalize to the byte 1 (and 0 to 0). bit -> integer promotion is a
// zero-extension of the i1 (never a trunc, never a sign-extension).
//
// take_bit is an extern declaration on purpose: it cannot be inlined, so the
// caller-side marshal chain survives the -O2 run in SSA form.

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,O0
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O2 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,O2

volatile unsigned char port_sink;
void take_bit(__bit);

// --- dynamic i32 -> bit argument: full-width icmp, then zext, then i8 call ---

// CHECK-LABEL: define {{.*}}void @dyn_i32_arg
void dyn_i32_arg(int x) {
  take_bit(x);
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[A:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0-NEXT: call {{.*}}void @take_bit(i8 noundef [[A]])
  // O2: [[NZ2:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: [[A2:%[a-z0-9.]+]] = zext i1 [[NZ2]] to i8
  // O2-NEXT: call {{.*}}void @take_bit(i8 noundef [[A2]])
  // CHECK-NOT: trunc
}

// --- dynamic i32 -> bit local initialization: same chain into the alloca ---

// CHECK-LABEL: define {{.*}}void @dyn_i32_local
void dyn_i32_local(int x) {
  __bit b = x;
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[Z:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0-NEXT: store i8 [[Z]], ptr {{%[a-z0-9.]+}}
  // At O2 the local is SSA'd away and instcombine canonicalizes the branch
  // test to icmp-eq with swapped targets; the equivalent-value fact is that
  // the store fires exactly when x != 0, with no trunc/sext anywhere.
  // O2: [[EQ:%[a-z0-9.]+]] = icmp eq i32 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: br i1 [[EQ]], label %if.end, label %if.then
  // O2: store volatile i8 1, ptr
  // CHECK-NOT: trunc
  if (b)
    port_sink = 1;
}

// --- constant corners: every non-zero source constant becomes the byte 1 ---

// CHECK-LABEL: define {{.*}}void @const_args
void const_args(void) {
  take_bit(2);
  take_bit(0x80);
  take_bit(0x100);
  take_bit(-1);
  take_bit(0x80000000);
  take_bit(0);
  take_bit(1);
  // O0-COUNT-5: call {{.*}}void @take_bit(i8 noundef 1)
  // O0: call {{.*}}void @take_bit(i8 noundef 0)
  // O0: call {{.*}}void @take_bit(i8 noundef 1)
  // O2-COUNT-5: call {{.*}}void @take_bit(i8 noundef 1)
  // O2: call {{.*}}void @take_bit(i8 noundef 0)
  // O2: call {{.*}}void @take_bit(i8 noundef 1)
}

// Constant-initialized bit locals are stored as normalized bytes.
// CHECK-LABEL: define {{.*}}void @const_local_init
void const_local_init(void) {
  __bit b = 2;
  __bit c = 0x80;
  __bit d = 0x100;
  __bit e = -1;
  __bit f = 0x80000000;
  // O0-COUNT-5: store i8 1, ptr
  // CHECK-NOT: trunc
  if (b) port_sink = 1;
  if (c) port_sink = 2;
  if (d) port_sink = 3;
  if (e) port_sink = 4;
  if (f) port_sink = 5;
}

// --- bit -> int promotion is a zero-extension of the i1 ---

// CHECK-LABEL: define {{.*}}i32 @bit_to_int
int bit_to_int(__bit b) {
  // O0: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O0: zext i1 {{%[a-z0-9.]+}} to i32
  // O2: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: zext i1 {{%[a-z0-9.]+}} to i32
  // CHECK-NOT: trunc
  // CHECK-NOT: sext
  return b;
}

// Narrow targets zero-extend too (i1 -> i8), still never trunc/sext.
// CHECK-LABEL: define {{.*}}void @bit_to_narrow
void bit_to_narrow(__bit b) {
  unsigned char u = b;
  // CHECK: zext i1 {{%[a-z0-9.]+}} to i8
  // CHECK-NOT: trunc
  // CHECK-NOT: sext
  port_sink = u;
}

// --- positive control for the trunc bans above: ordinary integer
// narrowing (int -> unsigned char) IS a trunc, so the per-function scoping
// is load-bearing -- a global `CHECK-NOT: trunc` would reject this function. ---

// CHECK-LABEL: define {{.*}}void @ordinary_trunc_ok
void ordinary_trunc_ok(int x) {
  // O0: trunc i32
  // O2: trunc i32
  port_sink = (unsigned char)x;
}
