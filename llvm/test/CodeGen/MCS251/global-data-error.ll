; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Mutable defined data still requires a writable area and initialization.
; The minimal read-only CSEG path must not silently place it in ROM.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)
; External declarations are fine: see load.ll/store.ll.

@g = global i16 42

define i16 @read_g() {
; CHECK: LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only CSEG
  %v = load i16, ptr @g
  ret i16 %v
}
