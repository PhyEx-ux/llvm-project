; RUN: opt -passes=gvn -S < %s -o - | FileCheck %s --check-prefix=GVN
; RUN: opt -passes='instcombine,early-cse,gvn,simplifycfg' -S < %s -o - | FileCheck %s --check-prefix=PIPE
; RUN: opt -passes='default<O2>' -S < %s -o - | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: %python %S/Inputs/check-as4-alias-injection.py --opt opt --filecheck FileCheck --test-ll %s
;
; A3 alias analysis, corrected acceptance (RUNTIME-AS-PTR-DESIGN-A.md §3-A3
; "验收补充一" and Alice's review counterexamples R1/R3/R8).
;
; What this test is FOR: an AS4 <-> AS0 addrspacecast is a no-op at the DAG
; level (equal width, equal index width), so a store through the AS0 view of
; an address and a load of the same byte through the AS4 view MUST be seen as
; may-alias by the optimizer.  The observable evidence is a *fold*: GVN
; replaces the second load with the stored value.  The earlier version of
; this test demanded the opposite ("the load must survive"), which was simply
; wrong and would have frozen a pessimisation.
;
; Every probe below pins the WHOLE chain, not just the final `ret`: the probe
; writes 42, reads it back (`old`), overwrites with 7 (`new`), and returns
; `new - old`.  Binding only the return (Alice review R8) let a wrong
; optimizer keep the stores and return a bogus constant -- `ret i8 0`, a
; stale/new==old fold, or a wrong store constant -- and still pass.  Here the
; expected `ret i8 -35` is only reachable when old folded to 42, the stored
; value is 7 and new folded to 7; the injected-wrong-result self-test in
; Inputs/check-as4-alias-injection.py mutates the real optimized output into
; each of those defective shapes and requires the UNMODIFIED oracle below to
; reject it.
;
; What this test is NOT FOR: writing real `__code` objects.  Those are
; read-only under the approved (a) route; the writes below target ordinary
; AS0 storage whose address is also expressed in AS4 (the target-extension
; aliasing contract, DESIGN.md B.2.1.1), and are marked as such.
;
; Probes:
;   P1 same-index load-store-load through the converted alias: old=42,
;      store=7, new=7, return = 7 - old = -35
;   P2 same byte through a *different GEP chain* (base+1 then -1): same chain
;   P3 wide (i16) access on both sides: old=42, store=7, ret = -35 (i16)
;   P4 noinline helper: the write must be visible to the helper's read; the
;      helper carries `noinline` so O2 cannot inline the store/ret away
;   P5 an AS0 store to one global must not be forwarded into an AS4 read of
;      a *different* base pointer
;   P6 standard-prototype memcpy shape with an AS4 source (the libc-contagion
;      probe); it must lower to real loads, not vanish
;   C1 negative control: a genuine `noalias` parameter must still allow the
;      fold, proving the probes are not passing because folding is disabled
;   C2 legal CODE reads of a real `addrspace(4) constant` object (the direct
;      AS4 read and the converted AS0 view) must both stay real loads -- no
;      `readonly`/`constant` fold may eat them.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251-unknown-none"

@ram = global [8 x i8] zeroinitializer

; A genuine CODE-resident constant object: C2 reads THIS, not an opaque
; parameter (Alice review R8-4 noted the old C2 never named a real AS4
; constant object).  `external` names the object without defining it here,
; so the object-file path is not required by this .ll.
@code_rom = external addrspace(4) constant [8 x i8]

declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)

; P1: store 42 via the AS0 view, read it back through the AS4 view, store 7
; through the AS0 view, read the same byte again through the AS4 view: the
; second read must fold to 7 and `new - old` to -35.  A wrong "different AS
; therefore NoAlias" assumption would *also* fold, so the discriminating
; information here is the must-alias result, and the control C1 below proves
; the fold is not happening for unrelated reasons.
;
; GVN/PIPE keep both stores (no DSE), so the stored constants are pinned
; directly.  O2 drops the now-dead first store and folds to `store i8 7` +
; `ret i8 -35`.  The final store and its address computation DO survive:
; P1/P2 bind %base -> addrspacecast -> GEP(%i) -> store destination, and P3
; binds %base -> addrspacecast -> store destination.  R8's old O2 patterns
; checked only `store 7, ptr` and `ret -35`, accepting a write redirected to
; @ram.  Pin the surviving chain, not a fictitious first store or load that
; O2 has removed.  The injection checker also changes the O2 cast source and
; GEP index while keeping the store's SSA operand and return unchanged.
define i8 @probe_lsl_same(ptr addrspace(4) %base, i32 %i) {
; GVN-LABEL: define i8 @probe_lsl_same(
; GVN: store i8 42, ptr %sp
; GVN: store i8 7, ptr %sp
; GVN-NOT: load i8, ptr addrspace(4)
; GVN: ret i8 -35
; PIPE-LABEL: define i8 @probe_lsl_same(
; PIPE: store i8 42, ptr %sp
; PIPE: store i8 7, ptr %sp
; PIPE-NOT: load i8, ptr addrspace(4)
; PIPE: ret i8 -35
; O2-LABEL: define {{.*}}i8 @probe_lsl_same(
; O2: [[SAME_Q:%.*]] = addrspacecast ptr addrspace(4) %base to ptr
; O2: [[SAME_SP:%.*]] = getelementptr i8, ptr [[SAME_Q]], i32 %i
; O2: store i8 7, ptr [[SAME_SP]],
; O2-NOT: load i8, ptr addrspace(4)
; O2: ret i8 -35
; ASM-LABEL: _probe_lsl_same:
; ASM: mov r{{[0-9]+}}, #0x07
; ASM: mov @dr{{[0-9]+}}, r{{[0-9]+}}
; O0-LABEL: _probe_lsl_same:
; O0: mov r{{[0-9]+}}, #0x2a
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+}}
; O0: mov r{{[0-9]+}}, #0x07
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+}}
  %q = addrspacecast ptr addrspace(4) %base to ptr
  %lp = getelementptr i8, ptr addrspace(4) %base, i32 %i
  %sp = getelementptr i8, ptr %q, i32 %i
  store i8 42, ptr %sp, align 1
  %old = load i8, ptr addrspace(4) %lp, align 1
  store i8 7, ptr %sp, align 1
  %new = load i8, ptr addrspace(4) %lp, align 1
  %d = sub i8 %new, %old
  ret i8 %d
}

; P2: the same byte reached through a different *GEP chain* on the AS0 side
; (base+1 then -1), as the old test intended but failed to construct: its
; "different offset" probe used `add i32 %i, 0`.  BasicAA must strip the
; addrspacecast, canonicalize both chains and still fold.  Same full chain as
; P1.
define i8 @probe_lsl_gep(ptr addrspace(4) %base, i32 %i) {
; GVN-LABEL: define i8 @probe_lsl_gep(
; GVN: store i8 42, ptr %sp
; GVN: [[J:%.*]] = add i32 %i, 1
; GVN: [[Q2:%.*]] = getelementptr i8, ptr {{.*}}, i32 [[J]]
; GVN: [[Q3:%.*]] = getelementptr i8, ptr [[Q2]], i32 -1
; GVN: store i8 7, ptr [[Q3]]
; GVN-NOT: load i8, ptr addrspace(4)
; GVN: ret i8 -35
; PIPE-LABEL: define i8 @probe_lsl_gep(
; PIPE: [[GEP_Q:%.*]] = addrspacecast ptr addrspace(4) %base to ptr
; PIPE: [[GEP_SP:%.*]] = getelementptr i8, ptr [[GEP_Q]], i32 %i
; PIPE: store i8 42, ptr [[GEP_SP]],
; PIPE: store i8 7, ptr [[GEP_SP]],
; PIPE-NOT: load i8, ptr addrspace(4)
; PIPE: ret i8 -35
; O2-LABEL: define {{.*}}i8 @probe_lsl_gep(
; O2: [[GEP_Q:%.*]] = addrspacecast ptr addrspace(4) %base to ptr
; O2: [[GEP_SP:%.*]] = getelementptr i8, ptr [[GEP_Q]], i32 %i
; O2: store i8 7, ptr [[GEP_SP]],
; O2-NOT: load i8, ptr addrspace(4)
; O2: ret i8 -35
  %q = addrspacecast ptr addrspace(4) %base to ptr
  %lp = getelementptr i8, ptr addrspace(4) %base, i32 %i
  %sp = getelementptr i8, ptr %q, i32 %i
  store i8 42, ptr %sp, align 1
  %old = load i8, ptr addrspace(4) %lp, align 1
  %j = add i32 %i, 1
  %q2 = getelementptr i8, ptr %q, i32 %j
  %q3 = getelementptr i8, ptr %q2, i32 -1
  store i8 7, ptr %q3, align 1
  %new = load i8, ptr addrspace(4) %lp, align 1
  %d = sub i8 %new, %old
  ret i8 %d
}

; P3: wide (i16) accesses.  Both ends must agree on the two bytes.  Both
; stores are pinned under GVN (42 then 7) and the return to -35, so a wrong
; `ret i16 0` (new==old) or a stale store constant is a FAIL.  PIPE and O2
; remove the dead first store: pin the surviving store's value/destination
; and return instead.  O2 additionally binds the destination to %base.
define i16 @probe_lsl_wide(ptr addrspace(4) %base) {
; GVN-LABEL: define i16 @probe_lsl_wide(
; GVN: store i16 42, ptr %q
; GVN: store i16 7, ptr %q
; GVN-NOT: load i16, ptr addrspace(4)
; GVN: ret i16 -35
; PIPE-LABEL: define i16 @probe_lsl_wide(
; PIPE: store i16 7, ptr %q
; PIPE-NOT: load i16, ptr addrspace(4)
; PIPE: ret i16 -35
; O2-LABEL: define {{.*}}i16 @probe_lsl_wide(
; O2: [[WIDE_Q:%.*]] = addrspacecast ptr addrspace(4) %base to ptr
; O2: store i16 7, ptr [[WIDE_Q]],
; O2-NOT: load i16, ptr addrspace(4)
; O2: ret i16 -35
  %q = addrspacecast ptr addrspace(4) %base to ptr
  store i16 42, ptr %q, align 1
  %old = load i16, ptr addrspace(4) %base, align 1
  store i16 7, ptr %q, align 1
  %new = load i16, ptr addrspace(4) %base, align 1
  %d = sub i16 %new, %old
  ret i16 %d
}

; P4: a noinline helper observes the converted-pointer write, so the value
; escapes and the write cannot be dropped.  Inside the helper the two
; pointers are separate SSA values (`%b` AS4, `%p` AS0): they MAY alias, so
; the second load must stay.  `lsl_helper` carries `noinline` (Alice review
; R8: without it O2 inlined the helper and the interprocedural observation
; disappeared); the attribute is asserted by the injection checker on the
; optimized IR and the call must survive as a real call.
define i8 @probe_helper(ptr addrspace(4) %base, i32 %i) {
; GVN-LABEL: define i8 @probe_helper(
; GVN: call addrspace(4) i8 @lsl_helper(ptr addrspace(4) %base, ptr %q, i32 %i)
; GVN: ret i8
; PIPE-LABEL: define i8 @probe_helper(
; PIPE: call addrspace(4) i8 @lsl_helper(
; PIPE: ret i8
; O2-LABEL: define {{.*}}i8 @probe_helper(
; O2: call addrspace(4) i8 @lsl_helper(
; O2: ret i8
; ASM-LABEL: _probe_helper:
; ASM: ecall _lsl_helper
  %q = addrspacecast ptr addrspace(4) %base to ptr
  %r = call i8 @lsl_helper(ptr addrspace(4) %base, ptr %q, i32 %i)
  ret i8 %r
}
define i8 @lsl_helper(ptr addrspace(4) %b, ptr %p, i32 %i) noinline {
; GVN-LABEL: define i8 @lsl_helper(
; GVN: [[OLD:%.*]] = load i8, ptr addrspace(4) %lp
; GVN: store i8 7, ptr %sp
; GVN: [[NEW:%.*]] = load i8, ptr addrspace(4) %lp
; GVN: [[D:%.*]] = sub i8 [[NEW]], [[OLD]]
; GVN: ret i8 [[D]]
; PIPE-LABEL: define i8 @lsl_helper(
; PIPE: [[OLD:%.*]] = load i8, ptr addrspace(4) %lp
; PIPE: store i8 7, ptr %sp
; PIPE: [[NEW:%.*]] = load i8, ptr addrspace(4) %lp
; PIPE: [[D:%.*]] = sub i8 [[NEW]], [[OLD]]
; PIPE: ret i8 [[D]]
; O2-LABEL: define {{.*}}i8 @lsl_helper(
; O2: [[OLD:%.*]] = load i8, ptr addrspace(4) %lp
; O2: store i8 7, ptr %sp
; O2: [[NEW:%.*]] = load i8, ptr addrspace(4) %lp
; O2: [[D:%.*]] = sub i8 [[NEW]], [[OLD]]
; O2: ret i8 [[D]]
; O0-LABEL: _lsl_helper:
; O0: mov r{{[0-9]+}}, #0x07
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+}}
; ASM-LABEL: _lsl_helper:
; ASM: mov r{{[0-9]+}}, #0x07
; ASM: mov @dr{{[0-9]+}}, r{{[0-9]+}}
  %lp = getelementptr i8, ptr addrspace(4) %b, i32 %i
  %old = load i8, ptr addrspace(4) %lp, align 1
  %sp = getelementptr i8, ptr %p, i32 %i
  store i8 7, ptr %sp, align 1
  %new = load i8, ptr addrspace(4) %lp, align 1
  %d = sub i8 %new, %old
  ret i8 %d
}

; P5: an AS0 store to @ram must not be forwarded into the AS4 read of an
; unrelated parameter base.  The load must survive: this is where a
; *wrong* cross-space must-alias would show up as an incorrect fold.
define i8 @probe_other_object(ptr addrspace(4) %base) {
; GVN-LABEL: define i8 @probe_other_object(
; GVN: store i8 1, ptr @ram
; GVN: load i8, ptr addrspace(4) %base
; GVN: ret i8
; PIPE-LABEL: define i8 @probe_other_object(
; PIPE: load i8, ptr addrspace(4) %base
; PIPE: ret i8
; O2-LABEL: define {{.*}}i8 @probe_other_object(
; O2: load i8, ptr addrspace(4) %base
; O2: ret i8
; ASM-LABEL: _probe_other_object:
; ASM: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; O0-LABEL: _probe_other_object:
; O0: mov {{r[0-9]+}}, @dr{{[0-9]+}}
  store i8 1, ptr @ram, align 1
  %v = load i8, ptr addrspace(4) %base, align 1
  ret i8 %v
}

; P6: the standard memcpy shape with an AS4 source, via the intrinsic with a
; fixed length so the lowering is fully visible.  The copy must materialize
; as real DR moves on the CODE side and DSEG/IRAM stores on the AS0 side.
define void @probe_memcpy_fixed(ptr addrspace(4) %src) {
; O0-LABEL: _probe_memcpy_fixed:
; O0: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+}}
; ASM-LABEL: _probe_memcpy_fixed:
; ASM: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; ASM: mov @dr{{[0-9]+}}, r{{[0-9]+}}
; GVN-LABEL: define void @probe_memcpy_fixed(
; GVN: call addrspace(4) void @llvm.memcpy.p0.p0.i32
; PIPE-LABEL: define void @probe_memcpy_fixed(
; PIPE: [[V:%.*]] = load i32, ptr {{.*}}
; PIPE: store i32 [[V]], ptr @ram
; O2-LABEL: define {{.*}}void @probe_memcpy_fixed(
; O2: [[V:%.*]] = load i32, ptr {{.*}}
; O2: store i32 [[V]], ptr @ram
  %s = addrspacecast ptr addrspace(4) %src to ptr
  call void @llvm.memcpy.p0.p0.i32(ptr align 1 @ram, ptr align 1 %s, i32 4, i1 false)
  ret void
}

; C1: negative control -- a genuine `noalias` parameter must let GVN fold the
; second load.  If this fold disappeared, the probes above would be passing
; for the wrong reason (folding disabled), not because aliasing is modelled.
define i8 @control_noalias(ptr addrspace(4) %base, ptr noalias %other, i32 %i) {
; GVN-LABEL: define i8 @control_noalias(
; GVN: store i8 -86, ptr %other
; GVN-NOT: load i8, ptr addrspace(4)
; GVN: ret i8 0
; PIPE-LABEL: define i8 @control_noalias(
; PIPE-NOT: load i8, ptr addrspace(4)
  %lp = getelementptr i8, ptr addrspace(4) %base, i32 %i
  %old = load i8, ptr addrspace(4) %lp, align 1
  store i8 -86, ptr %other, align 1
  %new = load i8, ptr addrspace(4) %lp, align 1
  %d = sub i8 %new, %old
  ret i8 %d
}

; C2: legal CODE reads must stay real loads under every pipeline.  These read
; the genuine `addrspace(4) constant` object declared above (never written),
; so if an optimizer were to fold them to constants the read channel would no
; longer be exercised -- and a backend that miscompiled the AS4 load would no
; longer be visible.  Both the direct AS4 read and the converted AS0 read are
; checked.
define i8 @legal_code_read_as4(i32 %i) {
; GVN-LABEL: define i8 @legal_code_read_as4(
; GVN: load i8, ptr addrspace(4) %p
; PIPE-LABEL: define i8 @legal_code_read_as4(
; PIPE: load i8, ptr addrspace(4) %p
; O2-LABEL: define {{.*}}i8 @legal_code_read_as4(
; O2: load i8, ptr addrspace(4) %p
; O0-LABEL: _legal_code_read_as4:
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; ASM-LABEL: _legal_code_read_as4:
; ASM: add dr{{[0-9]+}}, dr{{[0-9]+}}
; ASM: mov {{r[0-9]+}}, @dr{{[0-9]+}}
  %p = getelementptr i8, ptr addrspace(4) @code_rom, i32 %i
  %v = load i8, ptr addrspace(4) %p, align 1
  ret i8 %v
}

define i8 @legal_code_read_as0(i32 %i) {
; GVN-LABEL: define i8 @legal_code_read_as0(
; GVN: [[R:%.*]] = getelementptr i8, ptr addrspacecast (ptr addrspace(4) @code_rom to ptr), i32 %i
; GVN: load i8, ptr [[R]]
; PIPE-LABEL: define i8 @legal_code_read_as0(
; PIPE: load i8, ptr
; O2-LABEL: define {{.*}}i8 @legal_code_read_as0(
; O2: load i8, ptr
; O0-LABEL: _legal_code_read_as0:
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0: mov {{r[0-9]+}}, @dr{{[0-9]+}}
; ASM-LABEL: _legal_code_read_as0:
; ASM: add dr{{[0-9]+}}, dr{{[0-9]+}}
; ASM: mov {{r[0-9]+}}, @dr{{[0-9]+}}
  %q = addrspacecast ptr addrspace(4) @code_rom to ptr
  %r = getelementptr i8, ptr %q, i32 %i
  %v = load i8, ptr %r, align 1
  ret i8 %v
}
