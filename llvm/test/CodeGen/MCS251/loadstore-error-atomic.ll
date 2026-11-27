; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Atomic loads are now lowered cleanly by MCS251AtomicExpand (1-byte atomic
; loads compile to a fenced volatile byte read; 2/4-byte loads bracket with EA
; save-disable/restore).

define i8 @atomic_load(ptr %p) {
; CHECK-LABEL: _atomic_load:
; CHECK:       eret
  %v = load atomic i8, ptr %p seq_cst, align 1
  ret i8 %v
}
