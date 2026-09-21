; RUN: not llc -mtriple=mcs251 -O0 < %s 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -O2 < %s 2>&1 | FileCheck %s
;
; BRJT S1 (design §4 G5 / risk R3, measured 2026-09-15): with ISD::BRIND set
; to Expand, llc reading an `indirectbr` (computed goto) IR failed loudly at
; instruction selection with the classic "Cannot select: brind" diagnostic --
; an acknowledged missing feature (MCS251 has no indirect jump on register
; pattern), NOT a miscompile.
;
; WP4 (D1) update: computed goto is now rejected by the STRUCTURAL contract
; check before any legalization or instruction selection can see it, with the
; actionable message and a deliberate clean exit (status 1) instead of the
; generic CannotYetSelect abort. The module-level intent of this test is
; unchanged (an `indirectbr` module is refused fail-closed); the -O0/-O2 pair
; pins that the refusal is optimization-independent, because the structural
; phase runs before either pipeline and rejects the `blockaddress` initializer
; even where an optimizer could devirtualize the branch away.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@nextaddr = global ptr addrspace(4) null
@blocks = private constant [2 x ptr addrspace(4)] [
  ptr addrspace(4) blockaddress(@foo, %la),
  ptr addrspace(4) blockaddress(@foo, %lb)
]

define internal i32 @foo(i32 %i) nounwind {
entry:
  %p = load ptr addrspace(4), ptr @nextaddr, align 4
  %z = icmp eq ptr addrspace(4) %p, null
  br i1 %z, label %setup, label %dispatch
setup:
  %q = getelementptr inbounds [2 x ptr addrspace(4)], ptr @blocks, i32 0, i32 %i
  %r = load ptr addrspace(4), ptr %q, align 4
  br label %dispatch
dispatch:
  %t = phi ptr addrspace(4) [ %p, %entry ], [ %r, %setup ]
  indirectbr ptr addrspace(4) %t, [label %la, label %lb]
la:
  br label %done
lb:
  br label %done
done:
  %u = phi i32 [ 1, %la ], [ 2, %lb ]
  ret i32 %u
}

; CHECK: LLVM ERROR: MCS251 contract violation: computed goto is not supported: an address-of-label constant ('blockaddress') appears in a static initializer; use a switch statement
