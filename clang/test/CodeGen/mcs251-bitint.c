// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c23 -O0 -emit-llvm -o - %s | FileCheck %s --check-prefixes=O0
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c23 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefixes=FOLDS
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c23 -fsyntax-only -DNEG_WIDTH=33 %s 2>&1 | FileCheck %s --check-prefix=NEG
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c23 -fsyntax-only -DNEG_WIDTH=64 %s 2>&1 | FileCheck %s --check-prefix=NEG
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c23 -fsyntax-only -DNEG_UNSIGNED=1 -DNEG_WIDTH=33 %s 2>&1 | FileCheck %s --check-prefix=NEGU
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c23 -fsyntax-only -fexperimental-max-bitint-width=64 -DNEG_WIDTH=64 %s 2>&1 | FileCheck %s --check-prefix=NEG

// _BitInt(N) with N<=32 is enabled; the cap is a hard target limit that even
// -fexperimental-max-bitint-width cannot lift (the backend has no storage or
// arithmetic beyond the 32-bit DR registers).
//
// OPS checks the emitted IR for volatile-fed operands (no frontend folding):
//   - _BitInt(24) locals are i24 objects; sdiv stays an i24 op.
//   - unsigned _BitInt(12) storage rounds to i16 (alignTo(12,8)==16).
//   - C23 6.3.1.8 mixed arithmetic: int represents all of 0..4095, so the
//     _BitInt(12) operand promotes to int and the multiply is i32 "nsw"
//     (no 12-bit truncation); two _BitInt(12) operands keep mul i12.
//
// CHECK also pins the frontend constant evaluator's wrap semantics:
// 4000*3 folds to 12000 (mixed) and 3808 (mod 4096, same-width wrap).
//
// The folded globals are asserted at -O2 (FOLDS).  At -O0 nothing folds
// any more: the legacy contract verifier folded constants regardless of
// optnone; the split MCS251LoweringPrep pass skips optnone functions,
// so O0 pins the unfolded instruction sequence instead.  The four
// "folded constant store" NOTs are fenced after EVERY positive match
// (campaign discipline: a NOT only covers the gap up to the next
// positive), because the @g1..@g4 stores sit between the arithmetic
// lines -- trailing NOTs after the srem left them uncovered.

#ifdef NEG_UNSIGNED
unsigned _BitInt(NEG_WIDTH) too_wide;
// NEGU: error: unsigned _BitInt of bit sizes greater than 32 not supported
#elif defined(NEG_WIDTH)
_BitInt(NEG_WIDTH) too_wide;
// NEG: error: signed _BitInt of bit sizes greater than 32 not supported
// NEG-NOT: remark
#else
int g1, g2, g3, g4;

void ops(void) {
  volatile _BitInt(24) x = 1000000;
  volatile _BitInt(24) d = 7;
  volatile unsigned _BitInt(12) y = 4000;
  volatile unsigned _BitInt(12) w = 3;
  g1 = x / d;
  g2 = y * w;
  g3 = y * 3;
}

void folds(void) {
  _BitInt(24) x = 1000000;
  _BitInt(24) d = 7;
  unsigned _BitInt(12) y = 4000;
  unsigned _BitInt(12) w = 3;
  _BitInt(32) z = 999999;
  g1 = x / d;
  g2 = y * w;
  g3 = y * 3;
  g4 = z % 31;
}
#endif

// O0-LABEL: define {{.*}}@ops
// O0: %x = alloca i24, align 1
// O0: %d = alloca i24, align 1
// O0: %y = alloca i16, align 1
// O0: %w = alloca i16, align 1
// O0: store volatile i24 1000000
// O0: store volatile i24 7
// O0: store volatile i16 4000
// O0: store volatile i16 3
// O0: sdiv i24
// O0: sext i24
// O0: mul i12
// O0: zext i12
// O0: mul nsw i32 {{.*}}, 3
//
// O0-LABEL: define {{.*}}@folds
// O0: store i24 1000000, ptr %x
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: store i24 7, ptr %d
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: store i16 4000, ptr %y
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: store i16 3, ptr %w
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: store i32 999999, ptr %z
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: sdiv i24
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: sext i24
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: mul i12
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: zext i12
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: mul nsw i32 {{.*}}, 3
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
// O0: srem i32 {{.*}}, 31
// O0-NOT: store i32 142857
// O0-NOT: store i32 3808
// O0-NOT: store i32 12000
// O0-NOT: store i32 1, ptr @g4
//
// FOLDS-LABEL: define {{.*}}@folds
// FOLDS: store i32 142857, ptr @g1
// FOLDS: store i32 3808, ptr @g2
// FOLDS: store i32 12000, ptr @g3
// FOLDS: store i32 1, ptr @g4
