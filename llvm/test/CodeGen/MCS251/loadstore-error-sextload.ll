; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Sign-extending loads are rejected: widening a byte into a word is only
; implemented for zero/any extension (ZEXT8: zero hi lane + REG_SEQUENCE);
; a sign extension would need a shift or branch sequence that this phase
; does not have. (The DAG combiner merges `load + sext` into a sextload,
; which is what reaches the fatal error.)
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i16 @sextload8(ptr %p) {
; CHECK: LLVM ERROR: MCS251: sign-extending loads are not supported (no 8-to-16 bit sign extension yet)
  %v = load i8, ptr %p
  %z = sext i8 %v to i16
  ret i16 %z
}
