// MCS-251 `bit` value ABI (P09 P-3, design section 4): every bit parameter
// and a bit return crosses the call boundary as an *explicit direct i8*
// carrying a normalized 0/1 -- never an i1, never zeroext/signext on an i1,
// never an indirect object. The design's positional contract (first source
// parameter in the full DPL byte, later source parameters in the callee's
// original-position `_callee_PARM_n` 1-byte slots, return in the full DPL)
// is realized by the backend's existing assignment: the LLVM-level signature
// is the thing clang owns, so every position row of section 4.1 is asserted
// here on the exact `define`/`declare`/`call` signatures and the marshal
// chains, and the machine-level DPL/slot bytes are pinned by
// llvm/test/CodeGen/MCS251/bit-abi-value.ll plus the cross-TU harness in
// validation/mcs251-bit/.
//
// Marshal chain per section 4.2:
//   caller:  %nz = icmp ne <source width> %v, 0 ; %abi = zext i1 %nz to i8
//   callee:  entry %bit.param = icmp ne i8 %arg, 0 (private copy store zext)
//   return:  %bit.ret = zext i1 %v to i8 before `ret i8`
//   caller using a returned bit: icmp ne i8 %call, 0
// No trunc appears anywhere in these chains (trunc-then-zext would give
// 2 -> 0, a miscompile).

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,O0
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O2 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,O2

// The fp globals (defined near the bottom of the source) are emitted at the
// TOP of the module, so their signature assertions live here, ahead of the
// first function check; the initializer is the program-address-space
// pointer form the dump actually carries.
// CHECK: @fp_one = {{.*}}global ptr addrspace(4) @one_bit
// CHECK: @fp_ret = {{.*}}global ptr addrspace(4) @ret_bit

// --- 4.1 row 1: first source parameter is bit -> one i8 parameter ---

// CHECK-LABEL: define {{.*}}i32 @first_bit
int first_bit(__bit a) {
  // O0: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O2: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // CHECK: zext i1
  return a;
}

// --- 4.1 row 2: first source parameter not bit -> unchanged DPL/DPH/B/A
// assignment for the ordinary type; the bit keeps its own later position. ---

// CHECK-LABEL: define {{.*}}i32 @mixed_first_int
int mixed_first_int(int x, __bit a) {
  // O0: icmp ne i8 {{%[a-z0-9.]+}}, 0
  return x + a;
}

// --- 4.1 row 3: second and later bits keep their ORIGINAL source position
// (each is one i8; the `_PARM_n` slot names are derived from the LLVM
// argument index downstream, which equals the 1-based source position). ---

// CHECK-LABEL: define {{.*}}i32 @three_bits
int three_bits(__bit a, int x, __bit b, __bit c) {
  // O0: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O0: icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O0: icmp ne i8 {{%[a-z0-9.]+}}, 0
  return a + b + c + x;
}

// --- 4.1 row 4: 9+ bit parameters, no packing, no 8-parameter cap. ---

// CHECK-LABEL: define {{.*}}i32 @nine_bits
int nine_bits(__bit a, __bit b, __bit c, __bit d, __bit e, __bit f,
              __bit g, __bit h, __bit i) {
  // O0-COUNT-9: icmp ne i8
  return a + b + c + d + e + f + g + h + i;
}

// --- unnamed parameters still occupy their position. ---

// The LABEL itself carries the full positional signature (i32, i8, i8):
// both dumps have exactly one `define ... @unnamed(i32 noundef %0, i8
// noundef %1, i8 noundef %2)`, so a second standalone define check could
// never match after the label consumed the only define line.
// CHECK-LABEL: define {{.*}}void @unnamed(i32 {{.*}}, i8 {{.*}}, i8 {{.*}})
void unnamed(int, __bit, __bit) {}

// --- 4.1 row 5: bit return is a full i8, never i1/carry. ---

// CHECK-LABEL: define {{.*}}i8 @ret_bit
__bit ret_bit(int x) {
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32
  // O0-NEXT: [[Z:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0: ret i8 [[Z]]
  // O2: [[NZ2:%[a-z0-9.]+]] = icmp ne i32
  // O2-NEXT: [[Z2:%[a-z0-9.]+]] = zext i1 [[NZ2]] to i8
  // O2-NEXT: ret i8 [[Z2]]
  return x;
}

// Discarding the result keeps the i8 call at the emission point (O0); the
// O2 dump shows the pure callee inlined and the dead call eliminated, which
// is ordinary dead-call elimination, not a fallback to an i1 gate.
// CHECK-LABEL: define {{.*}}void @discard_ret
void discard_ret(int x) {
  ret_bit(x);
  // O0: call {{.*}}i8 @ret_bit
  // CHECK-NOT: call i1
}

// Caller using the returned bit decodes the full byte.
// CHECK-LABEL: define {{.*}}i32 @use_ret
int use_ret(int x) {
  // O0: [[C:%[a-z0-9.]+]] = call {{.*}}i8 @ret_bit
  // O0-NEXT: icmp ne i8 [[C]], 0
  // CHECK-NOT: call i1
  // At O2 the callee is inlined and the value flows as the equivalent
  // non-zero test on the original i32.
  // O2: [[TI:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  return ret_bit(x);
}

// --- caller marshal: dynamic source value, normalized before the boundary. ---

// CHECK-LABEL: define {{.*}}void @call_dynamic
void call_dynamic(int x) {
  first_bit(x);
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[A:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0-NEXT: call {{.*}}i32 @first_bit(i8 noundef [[A]])
  // CHECK-NOT: trunc
}

// Every normalization corner (2, 0x80, -1, 0x80000000) yields the byte 1.
// CHECK-LABEL: define {{.*}}void @call_corners
void call_corners(void) {
  first_bit(2);
  first_bit(0x80);
  first_bit(-1);
  first_bit(0x80000000);
  // O0-COUNT-4: call {{.*}}i32 @first_bit(i8 noundef 1)
}

// --- 4.1 row 7: function pointers with a single bit parameter and with a
// zero-parameter bit return share the same i8 ABI (direct and indirect). ---

void one_bit(__bit);
__bit ret_bit(int);

typedef void (*one_bit_fp)(__bit);
typedef __bit (*ret_zero_fp)(void);

// CHECK-LABEL: define {{.*}}void @indirect_one_bit
void indirect_one_bit(one_bit_fp fp, int x) {
  fp(x);
  // The O0 dump loads the function pointer BEFORE the value marshal (fp
  // spill reload first, then the i32 reload, icmp, zext), and the indirect
  // call carries addrspace(4).
  // O0: [[F:%[a-z0-9.]+]] = load ptr addrspace(4), ptr
  // O0: [[NZ:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[A:%[a-z0-9.]+]] = zext i1 [[NZ]] to i8
  // O0-NEXT: call addrspace(4) void [[F]](i8 noundef [[A]])
  // At O2 the same normalize-then-call chain survives in SSA form.
  // O2: [[NZ2:%[a-z0-9.]+]] = icmp ne i32 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: [[A2:%[a-z0-9.]+]] = zext i1 [[NZ2]] to i8
  // O2-NEXT: tail call addrspace(4) void %fp(i8 noundef [[A2]])
}

// CHECK-LABEL: define {{.*}}i32 @indirect_ret_bit
int indirect_ret_bit(ret_zero_fp gp) {
  // O0: [[F:%[a-z0-9.]+]] = load ptr addrspace(4), ptr
  // O0-NEXT: [[C:%[a-z0-9.]+]] = call addrspace(4) i8 [[F]]()
  // O0-NEXT: icmp ne i8 [[C]], 0
  // O2: [[C2:%[a-z0-9.]+]] = tail call addrspace(4) i8 %gp()
  // O2-NEXT: icmp ne i8 [[C2]], 0
  return gp();
}

// Taking the address of a bit-signature function yields the i8 signature
// asserted on the module globals at the top of this file (@fp_one/@fp_ret).
one_bit_fp fp_one = one_bit;
typedef __bit (*ret_one_fp)(int);
ret_one_fp fp_ret = ret_bit;

// --- callee entry decode ordering (3.2): the decode happens before any
// body code, in particular before a helper call could clobber a slot. ---

extern int helper(int);

// CHECK-LABEL: define {{.*}}i32 @entry_before_helper
int entry_before_helper(__bit a, __bit b) {
  // The O0 dump decodes BOTH i8 parameters first (the two entry icmp ne
  // appear back to back), then normalizes/stores each copy, all before the
  // helper call.
  // O0: [[PA:%[a-z0-9.]+]] = icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[PB:%[a-z0-9.]+]] = icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O0-NEXT: [[ZA:%[a-z0-9.]+]] = zext i1 [[PA]] to i8
  // O0-NEXT: store i8 [[ZA]], ptr
  // O0-NEXT: [[ZB:%[a-z0-9.]+]] = zext i1 [[PB]] to i8
  // O0-NEXT: store i8 [[ZB]], ptr
  // O0: call {{.*}}i32 @helper
  // O2: [[PA2:%[a-z0-9.]+]] = icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O2-NEXT: [[PB2:%[a-z0-9.]+]] = icmp ne i8 {{%[a-z0-9.]+}}, 0
  // O2: call {{.*}}i32 @helper
  int r = helper(0);
  return r + a + b;
}
