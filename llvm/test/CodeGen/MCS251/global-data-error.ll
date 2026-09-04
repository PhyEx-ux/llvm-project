; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Phase 12, Step 1: defined global data must be rejected loudly.  Data-area
; emission is not implemented yet, and the ASxxxx dialect suppresses section
; switching, so a defined global would be emitted into the CODE area CSEG
; with legal sdas251 directives (.globl/.word/.byte) and silently mislink --
; the loads/stores below address region-00 data space, not code space.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)
; External declarations are fine: see load.ll/store.ll.

@g = global i16 42

define i16 @read_g() {
; CHECK: LLVM ERROR: MCS251: defined global data is not supported yet
  %v = load i16, ptr @g
  ret i16 %v
}
