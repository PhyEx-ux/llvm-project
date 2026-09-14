// Persistent/static MCS-251 `bit` objects (P09 P-1b, design section 2):
// a bit object definition lowers to an AS0 i8 bit-object handle global with
// the structural attribute "mcs251-bit-object" (never an ordinary byte
// global, never DSEG/CSEG storage), the source initializer is normalized to
// the constant i8 0/1 (nonzero -> 1), every emitted definition is forced
// into llvm.used (an unused internal/const/volatile/static definition cannot
// disappear), and an extern-only declaration materializes as an
// initializer-less external global that is NOT in llvm.used (no spurious
// must-resolve reference for an unused pure extern).
//
// Accesses go through the obj intrinsic family (P09 section 1.1) with the
// handle global itself as the operand (section 2.6: the handle is never
// ptrtoint'ed), a constant write folds to one obj.set/obj.clear, a dynamic
// RHS is sampled once and written once on each path, and the discarded
// `B ^= 1` / `B = !B` forms are a single obj.toggle with no read first.
//
// Assertion discipline (same as mcs251-bit-fixed-ref.c): every function is
// bracketed by CHECK-LABEL intervals, and within each interval every ban of
// the window's ban set (any extra llvm.mcs251.bit.* call) is restated after
// every positive CHECK. The ELF closure (records, BITADDR8, cross-TU lld)
// lives in validation/mcs251-bit/ (two real C TUs) and the backend contract
// verifier is the hard fence against any handle escape.

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -Wno-constant-logical-operand -emit-llvm -o - %s | FileCheck %s

typedef __bit BOOL;

// --- Handle globals, initializers, llvm.used (section 2.1) ---

// 2/-1/0x80 normalize to 1, the ICE fold `1 && 2` normalizes its result, and
// the tentative/const/volatile/never-referenced definitions are all emitted.
__bit g_zero;              // tentative -> strong definition, init 0
__bit g_one = 1;
__bit g_two = 2;           // 2 -> 1
__bit g_neg = -1;          // -1 -> 1
static __bit s_top = 0x80; // internal, top bit -> 1
BOOL g_fold = 1 && 2;      // folded truth -> 1
const __bit g_c = 0;       // source const: still a global (no CSEG, no unnamed_addr)
volatile __bit g_v = 1;    // source volatile: no IR pseudo-volatile
const volatile BOOL g_cv;  // tentative const volatile, init 0
__bit g_a = 0, g_b = 1, g_c2 = 2; // multi-declarator: own definition each

// CHECK: @g_one = global i8 1, align 1 #{{[0-9]+}}
// CHECK-NOT: @never_used_extern
// CHECK: @g_two = global i8 1, align 1
// CHECK: @g_neg = global i8 1, align 1
// CHECK: @s_top = internal global i8 1, align 1
// CHECK: @g_fold = global i8 1, align 1
// CHECK: @g_c = global i8 0, align 1
// CHECK: @g_v = global i8 1, align 1
// CHECK: @g_a = global i8 0, align 1
// CHECK: @g_b = global i8 1, align 1
// CHECK: @g_c2 = global i8 1, align 1

// extern followed by a definition merges to one handle (same canonical
// declaration -> same global, one definition, one bit), never a second GV.
extern BOOL defined_extern;
BOOL defined_extern = 1;
// CHECK: @defined_extern = global i8 1, align 1
// CHECK-NOT: @defined_extern.{{[0-9]}}

// Never-referenced file statics are still emitted (internal linkage); the
// definite one is created at its parse position, the tentative ones at the
// end-of-TU completion pass (module print order = creation order).
static __bit used_file_static;
static __bit writer_file_static;
static __bit unused_file_static_init = 1;
// CHECK: @unused_file_static_init = internal global i8 1, align 1

// The last definite file static (parse position, before all functions), then
// the end-of-TU tentative completions. g_zero is created when write_const is
// emitted (first reference); the never-referenced tentatives complete last.
static __bit writer2_file_static = 0;
// CHECK: @writer2_file_static = internal global i8 0, align 1

// The other two spellings of the same scalar (P09-BIT-TYPE-DESIGN axis 2:
// `__bit`, bare `bit` under -fmcs251-keil, `typedef bit`, `typedef __bit`)
// lower to exactly the same handle forms (definite globals here: parse
// position, own initializer each).
bit bare_g = 1;
static bit bare_s = 0;
extern bit bare_e;
typedef bit BOOL2;
BOOL2 td_g = 2;              // 2 -> 1
static BOOL2 td_s;           // tentative -> 0
extern __bit never_used_extern; // pure declaration, never referenced
// CHECK: @bare_g = global i8 1, align 1
// CHECK: @bare_s = internal global i8 0, align 1
// CHECK: @td_g = global i8 1, align 1
// CHECK: @g_zero = global i8 0, align 1
// CHECK: @locals.f_local_zero = internal global i8 0, align 1
// CHECK: @locals.f_local_one = internal global i8 1, align 1

// Pure extern: materialized (referenced below) as an external declaration
// with the attribute and no initializer.
extern BOOL pure_extern;
// CHECK: @pure_extern = external global i8, align 1
// CHECK: @spellings.bare_ls = internal global i8 0, align 1
// CHECK: @spellings.td_ls = internal global i8 1, align 1
// CHECK: @bare_e = external global i8, align 1
// CHECK: @g_cv = global i8 0, align 1
// CHECK: @used_file_static = internal global i8 0, align 1
// CHECK: @writer_file_static = internal global i8 0, align 1
// CHECK: @td_s = internal global i8 0, align 1
// An unreferenced pure extern declaration is not materialized at all
// (section 2.1.6: no global, and consequently no empty bit-record section
// and no llvm.used entry for it either).
// CHECK-NOT: @never_used_extern

// llvm.used: appending, AS0 [N x ptr], section "llvm.metadata", elements the
// handles themselves -- every definition above exactly once (an
// extern-referenced-then-defined object registers at its definition), the
// extern-only pure_extern/bare_e NOT registered (no must-resolve reference
// for an unused pure extern). Definite globals enter in parse order, local
// statics at their function's emission, tentatives at the end-of-TU
// completion pass (parse order).
// CHECK: @llvm.used = appending global [25 x ptr] [ptr @g_one, ptr @g_two, ptr @g_neg, ptr @s_top, ptr @g_fold, ptr @g_c, ptr @g_v, ptr @g_a, ptr @g_b, ptr @g_c2, ptr @defined_extern, ptr @unused_file_static_init, ptr @writer2_file_static, ptr @bare_g, ptr @bare_s, ptr @td_g, ptr @locals.f_local_zero, ptr @locals.f_local_one, ptr @spellings.bare_ls, ptr @spellings.td_ls, ptr @g_zero, ptr @g_cv, ptr @used_file_static, ptr @writer_file_static, ptr @td_s], section "llvm.metadata"
// CHECK-NOT: @pure_extern
// CHECK-NOT: @never_used_extern

// --- Symbolic access routes (section 2.6 / 2.4) ---

// read: exactly one obj.read per evaluated source read; the i1 participates
// in the current integer model as an ordinary value.
int read_one(void) {
  return g_one; // read once, zext to the return type
}
// CHECK-LABEL: define {{.*}}@read_one(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.obj.read(ptr @g_one)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

// Constant assignment: exactly one set / one clear, no read of the target.
void write_const(void) {
  g_zero = 1;
  g_zero = 0;
}
// CHECK-LABEL: define {{.*}}@write_const(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @g_zero)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.clear(ptr @g_zero)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

extern int side_source(void);

// Dynamic RHS: sampled once, exactly one write on each path; no MOV bit,C.
void write_dynamic(int x) {
  g_one = x;
}
// CHECK-LABEL: define {{.*}}@write_dynamic(
// CHECK: icmp ne
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @g_one)
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.clear(ptr @g_one)
// CHECK: ret

// r = (B = independent): the assignment result reuses the normalized RHS
// value; the target is not read back after the write.
int assign_result(void) {
  int r = (g_two = side_source());
  return r;
}
// CHECK-LABEL: define {{.*}}@assign_result(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.obj.read
// CHECK: call {{.*}}i32 @side_source()
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @g_two)
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.clear(ptr @g_two)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.obj.read
// CHECK: ret

// The discarded toggle forms on a persistent object: one obj.toggle each, no
// read before it (the recognition happens before any read, section 2.4).
void toggle_forms(void) {
  g_neg ^= 1;
  g_neg = !g_neg;
  (void)(g_neg ^= 1);
}
// CHECK-LABEL: define {{.*}}@toggle_forms(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.toggle(ptr @g_neg)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.toggle(ptr @g_neg)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.toggle(ptr @g_neg)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

// B = !other (different identity): one source read of the other object plus
// one destination write on each path -- never a CPL toggle, never a
// self-read of the destination.
void copy_negated(void) {
  g_fold = !g_one;
}
// CHECK-LABEL: define {{.*}}@copy_negated(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.obj.read(ptr @g_fold)
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.obj.read(ptr @g_one)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.obj.read
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @g_fold)
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.clear(ptr @g_fold)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.obj.read
// CHECK: ret

// Direct condition: one sample feeds the branch (JB/JNB at machine level).
int branch_on(void) {
  if (g_two)
    return 1;
  return 0;
}
// CHECK-LABEL: define {{.*}}@branch_on(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.obj.read(ptr @g_two)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

// Local statics in an emitted function: own handle, own llvm.used entry.
void locals(void) {
  static __bit f_local_zero;
  static __bit f_local_one = 2; // -> 1
  f_local_zero = 1;
  f_local_one = 0;
}
// CHECK-LABEL: define {{.*}}@locals(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @locals.f_local_zero)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.clear(ptr @locals.f_local_one)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

// Extern reference through the handle: external identity preserved, no
// definition, no llvm.used entry; the same handle serves reads and writes.
int use_pure_extern(void) {
  pure_extern = 1;
  return pure_extern;
}
// CHECK-LABEL: define {{.*}}@use_pure_extern(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @pure_extern)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.obj.read(ptr @pure_extern)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

// The bare-keyword and typedef spellings through the remaining storage
// classes: a local static per spelling (own handle, own llvm.used entry) and
// the extern reference through the bare spelling. The cross-spelling copy is
// one source read + one destination write on each path.
void spellings(void) {
  static bit bare_ls;
  static BOOL2 td_ls = 2; // -> 1
  bare_ls = td_ls;
  bare_e = 1;
}
// CHECK-LABEL: define {{.*}}@spellings(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.obj.read(ptr @spellings.td_ls)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @spellings.bare_ls)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.clear(ptr @spellings.bare_ls)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: call {{.*}}void @llvm.mcs251.bit.obj.set(ptr @bare_e)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK: ret

// The structural attribute group itself is printed with the tail.
// CHECK: attributes #{{[0-9]+}} = { "mcs251-bit-object" }

// Tail bans: the handle globals never appear as ordinary pointers or byte
// storage anywhere in the remaining module text (declarations, attributes,
// metadata); per-window bans above fenced every function interval.
// CHECK-NOT: ptrtoint
// CHECK-NOT: bitcast {{.*}}@g_
// CHECK-NOT: load i8, {{.*}}@g_
// CHECK-NOT: store i8 {{.*}}, {{.*}}@g_
