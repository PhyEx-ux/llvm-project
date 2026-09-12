; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 < %S/Inputs/as4-cast-to-as3.ll 2>&1 | FileCheck %s --check-prefix=CODE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 < %S/Inputs/as4-cast-to-as9.ll 2>&1 | FileCheck %s --check-prefix=CODE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 < %S/Inputs/as4-cast-to-as8.ll 2>&1 | FileCheck %s --check-prefix=CODE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 < %S/Inputs/as4-cast-from-as1.ll 2>&1 | FileCheck %s --check-prefix=CODE
;
; The 16-bit Tiny AND XTiny models must both refuse the narrowing conversion
; (Alice review R9: the old file had only the XTiny RUN, so Tiny's refusal was
; covered by the layout query but never by an actual compile).
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 < %S/Inputs/as4-cast-16bit.ll 2>&1 | FileCheck %s --check-prefix=CODE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O0 < %S/Inputs/as4-cast-16bit.ll 2>&1 | FileCheck %s --check-prefix=CODE
;
; A3 negative matrix (RUNTIME-AS-PTR-DESIGN-A.md §3-A3): only the equal-width
; 32-bit AS4 <-> AS0 conversion is opened. Every other cast involving AS4 is
; rejected -- AS4<->AS3, AS4<->AS9, AS4<->AS8, AS4<->AS1, and the narrowing
; i32->i16 form a 16-bit AS0 model would imply (both Tiny and XTiny). Each
; case runs in its own process because report_fatal_error aborts on the first
; one.
;
; The original RAM rules are untouched, so AS3 -> AS0 (and the rest of the
; pre-existing matrix) keeps its old behavior; see pointer16-addrspacecast.ll
; for the approved RAM conversions.

; CODE: LLVM ERROR: MCS251: unsupported address-space cast involving CODE (address space 4); only the equal-width 32-bit AS4<->AS0 conversion is permitted

; A direct AS4 store remains refused by the memory gate: the new branch opened
; a read-capable conversion, not a write path.
; CHECK: LLVM ERROR: MCS251: store to CODE (address space 4) is not permitted; CODE is read-only

define void @code_store(ptr addrspace(4) %p, i8 %v) {
  store i8 %v, ptr addrspace(4) %p
  ret void
}
