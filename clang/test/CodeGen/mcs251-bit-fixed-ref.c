// MCS-251 controlled fixed bit references (old-style `sbit`, both declaration
// forms, and __builtin_mcs251_bit_lvalue) lower to the frozen target
// intrinsics llvm.mcs251.bit.read/set/clear/toggle (BIT M2, BT04/BT05,
// rulings P01/P02/P09). The same bit address must produce the same intrinsic
// call regardless of the declaring form, a constant assignment folds to one
// set/clear, a dynamic RHS is sampled once and written once through a branch
// (DIALECT-FRONTEND-DESIGN.md 7.5), and a discarded same-address `^= 1` /
// `= !X` is a single toggle. Fixed bit references are conservatively
// non-mergeable (P02): repeated reads are not combined.
//
// The assertions hold identically at O0, O2 and Os, except where per-level
// CHECK-O0 / CHECK-O2 / CHECK-Os prefixes pin what the levels actually emit
// differently (M2-10 below).
//
// Assertion discipline (M2-4 hardening, Alice r2; M2-6/M2-7 hardening, Alice
// r3; M2-9 completion, Alice r4): every function is bracketed by a leading
// CHECK-LABEL and the next function's CHECK-LABEL. Within that interval every
// ban of the window's ban set (any extra llvm.mcs251.bit.* call, any extra
// icmp, any trunc, any extra side() call) is restated immediately after EVERY
// positive CHECK -- shared or per-level -- and at the head of the interval,
// so there is no unfenced gap between two adjacent positive matches anywhere
// in the function (a bare "NOT next to a match" only fences its own side),
// and the bans after the last positive of a window run up to the next
// CHECK-LABEL. Exact counts are therefore spelled as explicit positive
// CHECKs, each fenced on both sides, not as CHECK-COUNT (whose counted
// matches cannot fence the gaps between them). A side-effecting RHS
// additionally pins the callee's evaluation count to exactly one, fenced
// window by window.
//
// Arm and memory binding (M2-12/M2-13/M2-14 hardening, Alice r5): every
// conditional-write arm is asserted as one chain -- the arm's label
// definition (`mcs251.bit.set:`), then its write call, then its terminator
// `br label %mcs251.bit.cont` -- while the entry `br` keeps pinning both
// successor label names, so swapping only the two label definitions (keeping
// the predicate, the successors and the textual call order) inverts the
// semantics and no longer passes. The O0 assign_result_dyn memory chain is
// captured end to end: the two entry allocas are captured, the join store and
// the result load must both use the captured result alloca, and a ban on any
// second store to that captured address is restated after every positive of
// the window (in particular between the store and the load), so an inserted
// overwriting store or a load redirected to any other address (including
// another alloca whose name starts with the captured one) is rejected. Every
// capture reference is followed by an SSA-name boundary (`{{[[:space:]]*,}}` or `{{[[:space:]]*$}}`
// as the syntax position dictates): a bare `%[[CAP]]` substitutes to a
// substring match, so a tampered `%conv.evil`-style name would otherwise
// satisfy `ret i32 %[[CAP]]`.
//
// CFG and memory topology (M2-15/M2-16/M2-18 hardening, Alice r6/r7): the
// arm "same chain" is no longer just textual order. Between each arm's label
// definition and its write call, and again between the write call and the
// arm terminator `br label %mcs251.bit.cont`, the control-flow fence bans
// any branch, return, switch/invoke/unreachable, block label or function
// definition: in valid IR nothing can legally appear in those windows, so a
// write that was moved out of its arm -- behind an inserted `br` into a dead
// block, or behind a prefix-named lookalike label -- no longer passes, at
// every level. The assign_result_dyn result slot is fenced stronger than a
// name match: after the join store (and again after the load and the ret)
// every store is banned outright, so an overwriting store hides behind
// neither a space before the comma nor a zero-offset GEP/ptrcast alias of
// the captured alloca (the alias chain itself is additionally verified
// structurally). Every capture reference keeps an SSA-name boundary, now
// whitespace-tolerant (`{{[[:space:]]*,}}` / `{{[[:space:]]*$}}`): a
// `%r ,`-style spaced comma or tab-padded suffix is boundary text too, not
// only the exactly adjacent comma these bounds used to require.
//
// The textual assertions are backed by a structural checker, run as extra
// RUN lines through Inputs/mcs251-bit-structure-check.py, always behind a
// real verifier: the emitted module is first piped through
// `opt -passes=verify -S`, so illegal IR (an unknown opcode, a branch to a
// nonexistent successor) is killed by LLVM itself and the checker's input
// is assumed verifier-clean (M2-19..M2-23 hardening, Alice r8 closure
// strategy).  The checker is a closed-world parser: any instruction form
// outside its finite enumeration fails the check outright (fail-closed)
// instead of being silently ignored, and quoted identifiers
// (@\"name\" / %\"name\", escapes included), multi-line switch/callbr case
// lists, invoke/callbr/landingpad printer continuations and trailing
// metadata attachments are all parsed as ordinary names or tolerated, so
// those spellings cannot hide structure.  It verifies that every block
// holding an llvm.mcs251.bit.* call is reachable from its function's entry,
// that each arm block contains exactly its own write and ends in the join
// branch with a predecessor, and that the returned result slot has exactly
// one aliasing store earlier in the load's block, where "aliasing" is a
// conservative closure over the finite set of pointer-producing forms
// (alloca, GEP with any base and offsets -- dynamic offsets collapse to
// "may alias" -- bitcast/ptrcast/addrspacecast, select of any arm, phi of
// any incoming, freeze) and "write" is a finite enumeration (store,
// atomicrmw, atomiccmpxchg, llvm.memcpy/memmove/memset.* calls, and any
// other call, which may clobber anything) -- properties a text matcher
// cannot see and a canonicalized (opt round-tripped) mutant cannot dodge.
//
// Polarity pinning (M2-10, Alice r4): `icmp {{ne|eq}}` matches both
// predicates and therefore does not bind the predicate to the branch
// direction or to the returned value, so a bare predicate flip survived. The
// icmp-producing windows (conv_dyn, dyn_assign, assign_result_dyn,
// dyn_assign_call, not_different_addr) instead pin, under per-level CHECK-O0
// / CHECK-O2 / CHECK-Os prefixes taken from each level's real emitted IR,
// the exact predicate of that level, the full arm binding
// `br i1 %cond, label <first successor>, label <second successor>`, the arm
// contents, and for the value-producing conversions the
// ret-to-normalization binding: the returned register must be the zext of
// the very icmp this level emitted (nonzero -> 1 under that predicate, and
// the stored/returned value chain is captured end to end). A semantically
// equivalent form (predicate and both successors flipped together, or an
// equally valued normalization) is intentionally rejected: the per-level pins
// assert the emitted shape per level, a strictly stronger contract than
// semantic equivalence -- the real three-level bases must and do pass, and
// the equivalent-polarity variant is on record as killed by design. The
// emitted block layout (entry br, mcs251.bit.set: arm, mcs251.bit.clear:
// arm, mcs251.bit.cont: join) is identical at O0, O2 and Os, so the shared
// arm-order chain still serves all three RUN lines; a same-arm double write
// (both calls in one arm) is rejected because a bit call cannot appear
// between a write and its arm terminator.

// M2-24..28 self-fix: the structural companion also tests both directions of
// wide/sub-byte/unknown memory widths, exact bit-callee membership, phi
// incoming aliases, half-open byte intervals, switch/call continuations and
// va_arg's in-memory cursor update. These embedded cases are verifier-clean
// and checked both raw and after opt, so passing mutants and false rejections
// remain executable regressions rather than only external review evidence.
// RUN: %python %S/Inputs/mcs251-bit-structure-check.py --self-test --opt opt
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O0 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-O0
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O2 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-O2
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -Os -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-Os
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O0 -emit-llvm -o - %s | opt -passes=verify -S - | %python %S/Inputs/mcs251-bit-structure-check.py
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O2 -emit-llvm -o - %s | opt -passes=verify -S - | %python %S/Inputs/mcs251-bit-structure-check.py
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -Os -emit-llvm -o - %s | opt -passes=verify -S - | %python %S/Inputs/mcs251-bit-structure-check.py

// An sbit is not an ordinary byte object: no IR global materializes for it
// (the globals area ends at the first function label below).
// CHECK-NOT: @A =
// CHECK-NOT: @B =
// CHECK-NOT: @D =

enum { P0 = 0x80, P1 = 0x90 };
sbit A = 0x80;
sbit B = P0 ^ 0;
sbit D = P1 ^ 1;

// Both declaration forms at the same address emit the same intrinsic calls:
// exactly two sets then exactly two clears, nothing else in the function. The
// four writes are spelled out with the full ban fence (bit calls and trunc)
// after each one, so a surplus access of any kind (read, toggle, an extra set
// between the clears, a trunc ...) is rejected in every gap.
// CHECK-LABEL: define {{.*}}@same_ops_both_forms(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void same_ops_both_forms(void) {
  A = 1; B = 1;
  A = 0; B = 0;
}

// __builtin_mcs251_bit_lvalue at the same address is the same fixed reference:
// exactly one set and one clear, with the whole bit-call ban (and the trunc
// ban) between them: an interleaved read or toggle is rejected, not just a
// second set.
// CHECK-LABEL: define {{.*}}@builtin_equiv_set(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void builtin_equiv_set(void) {
  __builtin_mcs251_bit_lvalue(0x80) = 1;
  __builtin_mcs251_bit_lvalue(P0 ^ 0) = 0;
}
// Exactly one read, widened by a zext, never a trunc.
// CHECK-LABEL: define {{.*}}@builtin_equiv_read(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.read(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
int builtin_equiv_read(void) {
  return __builtin_mcs251_bit_lvalue(0x80);
}

// Reads through each form are a single intrinsic call at the fixed address.
// CHECK-LABEL: define {{.*}}@read_form_a(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.read(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
int read_form_a(void) { return A; }
// CHECK-LABEL: define {{.*}}@read_form_b(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.read(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
int read_form_b(void) { return B; }

// Constant assignments fold to one set (nonzero) or clear (zero); 2 and -1
// fold to a set like 1 (nonzero -> 1 normalization). Exactly one clear and
// exactly three sets, each fenced with both bans so nothing can interleave
// between the counted writes.
// CHECK-LABEL: define {{.*}}@const_assign(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void const_assign(void) {
  A = 0; A = 1; A = 2; A = -1;
}

// Integer-to-bit conversion is "nonzero -> 1" (exactly one icmp against 0),
// not a truncation; constant operands fold to 0/1. Per level (M2-10): every
// level emits `icmp ne`, and the returned register is the zext of that very
// icmp (captured end to end), so an eq predicate, a wrong normalization or a
// constant return value is rejected at each level separately. The bans on
// extra icmps, bit calls and truncs are restated in every gap, including
// between the icmp and the zext/ret chain. Per level (M2-14): each ret ends
// with the SSA-name boundary `{{[[:space:]]*$}}`, so returning an alias of the captured
// zext name (`%conv.evil = xor i32 %conv, 1; ret i32 %conv.evil`) no longer
// satisfies the prefix match `ret i32 %conv`.
// CHECK-LABEL: define {{.*}}@conv_dyn(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: %[[CDT0:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: %[[CDT2:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: %[[CDTS:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: %[[CDC0:.*]] = zext i1 %[[CDT0]] to i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: ret i32 %[[CDC0]]{{[[:space:]]*$}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: %[[CDC2:.*]] = zext i1 %[[CDT2]] to i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: ret i32 %[[CDC2]]{{[[:space:]]*$}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: %[[CDCS:.*]] = zext i1 %[[CDTS]] to i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: ret i32 %[[CDCS]]{{[[:space:]]*$}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
int conv_dyn(int x) { return (__bit)x; }
// CHECK-LABEL: define {{.*}}@conv_const_0(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: ret i32 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
int conv_const_0(void) { return (__bit)0; }
// CHECK-LABEL: define {{.*}}@conv_const_2(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: ret i32 1
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
int conv_const_2(void) { return (__bit)2; }
// CHECK-LABEL: define {{.*}}@conv_const_neg1(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: ret i32 1
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
int conv_const_neg1(void) { return (__bit)-1; }

// Dynamic assignment: one sample of the RHS (exactly one icmp, no read of the
// target bit), exactly one bit write per arm, and the mcs251.bit.cont join
// block present after both arms (section 7.5 "sample once, write once"). Per
// level (M2-10): O0 emits `icmp ne` and branches set-first, O2/Os emit
// `icmp eq` and branch clear-first; each level's pin binds the predicate to
// its exact `br i1 %cond, label ..., label ...` successor order, so a bare
// predicate flip (eq with set-first at O0, ne with clear-first at O2/Os) no
// longer passes, and a predicate+successors flip (semantically equivalent)
// is rejected as well by design. The arm terminators are pinned: after the
// set call, and again after the clear call, the very next bit call ban runs
// up to the arm jump `br label %mcs251.bit.cont` into the join block. A
// same-arm double write has the second write between the first write and its
// arm jump, and is rejected; the same fence also rejects any read or toggle
// sneaked into an arm or before the join. Each arm is additionally bound to
// its label definition (M2-12): the `mcs251.bit.set:` label line must be
// matched between the entry br and the set call, and `mcs251.bit.clear:`
// between the set arm's join jump and the clear call, so exchanging only the
// two label definitions -- keeping the predicate, the br successors and the
// textual write order -- inverts the write routing and is rejected at every
// level.
// CHECK-LABEL: define {{.*}}@dyn_assign(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: %[[DAT0:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: br i1 %[[DAT0]], label %mcs251.bit.set, label %mcs251.bit.clear
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: %[[DAT2:.*]] = icmp eq i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: br i1 %[[DAT2]], label %mcs251.bit.clear, label %mcs251.bit.set
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: %[[DATS:.*]] = icmp eq i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: br i1 %[[DATS]], label %mcs251.bit.clear, label %mcs251.bit.set
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.set:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.clear:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.cont:
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
void dyn_assign(int x) { A = x; }

// The assignment result is the assigned value: it does not re-read the bit.
// CHECK-LABEL: define {{.*}}@assign_result_const(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
int assign_result_const(void) { int r = (A = 1); return r; }
// Like dyn_assign, but the join must return the assigned value. All three
// levels emit `icmp ne` and branch set-first here (the result value keeps the
// ne polarity); each level's pin binds the predicate to its br successor
// order and the join's value chain end to end: the zext operand is that very
// icmp, the store stores that zext, and the ret returns the loaded value
// (O0) / the zext itself (O2/Os). A tampered join (wrong normalization,
// constant store, mismatched ret register) is rejected per level. The arms
// are label-bound like dyn_assign (M2-12). The O0 memory chain is bound to
// one address end to end (M2-13): both entry allocas are captured
// (`%[[ARDX]]` the parameter slot, `%[[ARDR]]` the result slot), the store
// and the load must both use the captured result slot, the load/ret carry
// SSA-name boundaries, and a ban on any second store to the captured
// address is restated after every positive of the window -- in particular
// between the store and the load, where O0 emits nothing else -- so an
// inserted overwriting store or a load redirected to any other slot
// (including a same-prefixed alias) is rejected.
// CHECK-LABEL: define {{.*}}@assign_result_dyn(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: %[[ARDX:.*]] = alloca i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: %[[ARDR:.*]] = alloca i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0: %[[ART0:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0: br i1 %[[ART0]], label %mcs251.bit.set, label %mcs251.bit.clear
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O2: %[[ART2:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: br i1 %[[ART2]], label %mcs251.bit.set, label %mcs251.bit.clear
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: %[[ARTS:.*]] = icmp ne i32 %{{.+}}, 0
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: br i1 %[[ARTS]], label %mcs251.bit.set, label %mcs251.bit.clear
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.set:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK: mcs251.bit.clear:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK: mcs251.bit.cont:
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0: %[[ARDC0:.*]] = zext i1 %[[ART0]] to i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0: store i32 %[[ARDC0]], ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0-NOT: store
// CHECK-O0: %[[ARL0:.*]] = load i32, ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0-NOT: store
// CHECK-O0: ret i32 %[[ARL0]]{{[[:space:]]*$}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O0-NOT: store
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0-NOT: store {{.*}}ptr %[[ARDR]]{{[[:space:]]*,}}
// CHECK-O2: %[[ARDC2:.*]] = zext i1 %[[ART2]] to i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: ret i32 %[[ARDC2]]{{[[:space:]]*$}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: %[[ARDCS:.*]] = zext i1 %[[ARTS]] to i32
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: ret i32 %[[ARDCS]]{{[[:space:]]*$}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
int assign_result_dyn(int x) { int r = (A = x); return r; }

// A side-effecting RHS is evaluated exactly once: exactly one call of side()
// anywhere in the function (fenced window by window, including between the
// clear write and the join), one icmp on its result, one bit write per arm
// with pinned arm terminators, join block present. Per level (M2-10): O0
// emits `icmp ne` on the side() result and branches set-first, O2/Os emit
// `icmp eq` and branch clear-first; the icmp operand is pinned to the very
// side() call captured above, and each level's br successor order is pinned,
// so predicate flips and arm swaps are rejected per level. The arms are
// label-bound like dyn_assign (M2-12): exchanging only the two label
// definitions is rejected at every level as well.
// CHECK-LABEL: define {{.*}}@dyn_assign_call(
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: %[[DACSC:.*]] = {{.*}}call {{.*}}i32 @side()
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: %[[DACT0:.*]] = icmp ne i32 %[[DACSC]], 0
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O0: br i1 %[[DACT0]], label %mcs251.bit.set, label %mcs251.bit.clear
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: %[[DACT2:.*]] = icmp eq i32 %[[DACSC]], 0
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-O2: br i1 %[[DACT2]], label %mcs251.bit.clear, label %mcs251.bit.set
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: %[[DACTS:.*]] = icmp eq i32 %[[DACSC]], 0
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK-Os: br i1 %[[DACTS]], label %mcs251.bit.clear, label %mcs251.bit.set
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.set:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.clear:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
// CHECK: mcs251.bit.cont:
// CHECK-NOT: call {{.*}}@side
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: icmp
// CHECK-NOT: trunc
int side(void);
void dyn_assign_call(void) { A = side(); }

// A discarded same-address toggle is a single target toggle, in every form.
// CHECK-LABEL: define {{.*}}@toggle_same_xor(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.toggle(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void toggle_same_xor(void) { A ^= 1; }
// CHECK-LABEL: define {{.*}}@toggle_same_not(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.toggle(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void toggle_same_not(void) { A = !A; }
// CHECK-LABEL: define {{.*}}@toggle_builtin(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.toggle(i32 128)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void toggle_builtin(void) { __builtin_mcs251_bit_lvalue(0x80) ^= 1; }

// A negated *different* address is not a toggle: exactly one sample of D, then
// exactly one write of A per arm with pinned arm terminators, join block
// present, no toggle at all. Per level (M2-10): O0 materializes the negation
// as `icmp ne i1 %read, false` + `xor i1 %tobool, true` and branches
// set-first on the xor; O2/Os branch clear-first directly on the read's i1
// (no icmp at all). Each level's pin binds the exact branch condition and
// successor order, so an inverted sense (set when D is true) is rejected per
// level. No icmp ban exists in this window because the O0 form legitimately
// contains one between the read and the set. The arms are label-bound like
// dyn_assign (M2-12): exchanging only the two label definitions is rejected
// at every level as well.
// CHECK-LABEL: define {{.*}}@not_different_addr(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: %[[NDR:.*]] = {{.*}}call {{.*}}i1 @llvm.mcs251.bit.read(i32 145)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK-O0: %[[NDT0:.*]] = icmp ne i1 %[[NDR]], false
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK-O0: %[[NDL0:.*]] = xor i1 %[[NDT0]], true
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK-O0: br i1 %[[NDL0]], label %mcs251.bit.set, label %mcs251.bit.clear
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK-O2: br i1 %[[NDR]], label %mcs251.bit.clear, label %mcs251.bit.set
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK-Os: br i1 %[[NDR]], label %mcs251.bit.clear, label %mcs251.bit.set
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: mcs251.bit.set:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: mcs251.bit.clear:
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}void @llvm.mcs251.bit.clear(i32 128)
// CHECK-NOT: {{br |ret |switch |indirectbr |invoke |resume |unreachable|^define|^[A-Za-z0-9_.$]+:}}
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: br label %mcs251.bit.cont
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: mcs251.bit.cont:
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
void not_different_addr(void) { A = !D; }

// P02 conservatism: repeated reads of a fixed bit reference are not combined:
// exactly two reads, with the bit-call ban (and the trunc ban) between them
// (an interleaved write is rejected).
// CHECK-LABEL: define {{.*}}@two_reads(
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.read(i32 145)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
// CHECK: call {{.*}}i1 @llvm.mcs251.bit.read(i32 145)
// CHECK-NOT: call {{.*}}@llvm.mcs251.bit.
// CHECK-NOT: trunc
int two_reads(void) { return D + D; }
