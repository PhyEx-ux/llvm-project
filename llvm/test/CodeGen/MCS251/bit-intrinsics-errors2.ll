; RUN: not --crash llc -mtriple=mcs251 -O2 < %s -o /dev/null 2>&1 | FileCheck %s
;
; BT03 negative: an immarg constant outside the bit-address space [0, 255]
; passes the IR verifier but must be rejected loudly by the backend lowering
; (not silently truncated to a different, valid-looking bit address).
; CHECK: LLVM ERROR: MCS251: bit intrinsic address 256 is out of range [0, 255]

declare void @llvm.mcs251.bit.set(i32 immarg)

define void @oob() {
  call void @llvm.mcs251.bit.set(i32 256)
  ret void
}
