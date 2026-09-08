; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; 0xff is RSTCFG and is permanently forbidden by the hardware safety ruling.
;
; CHECK: LLVM ERROR: MCS251: SFR address 0xff is permanently forbidden

define void @reject_rstcfg() {
  store volatile i8 1, ptr addrspace(6) inttoptr (i16 255 to ptr addrspace(6)), align 1
  ret void
}
