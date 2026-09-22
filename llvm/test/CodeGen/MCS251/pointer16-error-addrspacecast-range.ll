; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null %t/near.ll 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null %t/far.ll 2>&1 | FileCheck %s
;
; A value fitting the 16-bit representation may still be outside the target
; address space. AS1 accepts only strict direct RAM [0,0x80), whether the source
; was same-width AS8 or a narrowed AS9 far container.
;
; CHECK: LLVM ERROR: MCS251: constant address is outside destination address space 1 range [0,0x80)

;--- near.ll
define ptr addrspace(1) @near_to_direct() addrspace(4) {
  %p = addrspacecast ptr addrspace(8) inttoptr (i16 4660 to ptr addrspace(8)) to ptr addrspace(1)
  ret ptr addrspace(1) %p
}

;--- far.ll
define ptr addrspace(1) @far_to_direct() addrspace(4) {
  %p = addrspacecast ptr addrspace(9) inttoptr (i32 4660 to ptr addrspace(9)) to ptr addrspace(1)
  ret ptr addrspace(1) %p
}
