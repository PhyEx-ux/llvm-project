; RUN: not llc -mtriple=mcs251 -O2 < %s 2>&1 | FileCheck %s
;
; BT03 negatives.
;
; A dynamic (non-constant) bit address violates the intrinsic's ImmArg<0> and
; is rejected by the IR verifier before any backend code runs.
; CHECK: immarg operand has non-immediate parameter
; CHECK: input module cannot be verified
;
; An IR constant outside [0, 255] passes the immutable-arg check but is
; rejected loudly by the backend lowering (MCS251: bit intrinsic address ...).
; (That path is also covered by bit-intrinsics-errors2.ll below with an
; explicit constant, so here we only exercise the verifier rejection.)

declare i1 @llvm.mcs251.bit.read(i32 immarg)
declare void @llvm.mcs251.bit.set(i32 immarg)

define i8 @dyn_read(i32 %x) {
  %b = call i1 @llvm.mcs251.bit.read(i32 %x)
  %z = zext i1 %b to i8
  ret i8 %z
}

define void @dyn_set(i32 %x) {
  call void @llvm.mcs251.bit.set(i32 %x)
  ret void
}
