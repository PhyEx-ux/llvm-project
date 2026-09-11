; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s
;
; X2 regression net: AS5 (bit space) stays fail-closed for ordinary data
; access after the AS3/AS4 channels were opened. The gate that fires first is
; the target contract check's pointer address-space whitelist (the backend's
; checkDataAddressSpace is the second line of defence, visible on MIR
; entry); both reject AS5. The only sanctioned route into bit storage is the
; controlled-bit lvalue mechanism / llvm.mcs251.bit.* intrinsics.

define void @ld5() {
; CHECK: LLVM ERROR: MCS251 contract violation: unsupported pointer address space 5
  %p = inttoptr i32 32 to ptr addrspace(5)
  %v = load i8, ptr addrspace(5) %p
  ret void
}
