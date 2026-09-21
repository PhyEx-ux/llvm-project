; RUN: not llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Atomic memory operations are rejected with the unified WP4 family diagnostic
; (A4): one sentence names the operation and the alternative, and the rejection
; is a deliberate clean exit (status 1), not a crash. The IR-layer structural
; contract check front-runs instruction selection, so the historical
; 'Cannot generate unaligned atomic load' / 'Cannot select: AtomicLoadAdd' /
; 'Cannot select: AtomicFence' aborts are gone.

define i8 @atomic_load(ptr %p) {
; CHECK: LLVM ERROR: MCS251 contract violation: C11/GNU atomic operations are not supported on this target (atomic load)
  %v = load atomic i8, ptr %p seq_cst, align 1
  ret i8 %v
}
