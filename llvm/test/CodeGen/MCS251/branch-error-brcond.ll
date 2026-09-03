; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; Phase 6 only lowers BR_CC, i.e. a branch whose condition is a single icmp
; that the DAG combiner folds into the branch. A branch on any other i1
; value keeps a materialised compare (SETCC, which this backend cannot
; select) and/or a BRCOND, both of which are rejected with a clear message
; until a later phase adds them. Here the condition is the xor of two
; icmps, so neither compare folds into the branch.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i8 @brcond_xor(i8 %x) {
; CHECK: LLVM ERROR: MCS251: Phase 6 supports only BR_CC (branch on icmp); SETCC/BRCOND arrive in a later phase
entry:
  %c1 = icmp eq i8 %x, 5
  %c2 = icmp ult i8 %x, 100
  %v = xor i1 %c1, %c2
  br i1 %v, label %a, label %b
a:
  ret i8 1
b:
  ret i8 0
}
