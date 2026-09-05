; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Phase 9: dynamic allocas. `add dr60,wr` is an illegal width mix (both ADD
; operands must be DR), so SPX is read and written through its SFR direct
; addresses (0x81 = SP low, 0x85 = SPH high) and the size is added in a WR:
;
;   read SPX -> WR ; add wr,size ; write SPX back ; result = oldSPX+1
;
; The returned pointer is the object base = old SPX+1 (the stack grows up
; and SPX points at the top-most used byte). Because dynamic allocas move
; SPX, such functions anchor the frame top in dr16 (push/mov/pop in
; prologue/epilogue) so SPX is exactly restored before eret pops
; [SPX-2..SPX].

declare i8 @f8a()

define i16 @dyn_alloca(i16 %n) {
; CHECK-LABEL: _dyn_alloca:
; CHECK: push dr16
; CHECK: mov dr16, dr60
; read SPX (0x81/0x85), add the size, write SPX back (high byte first so a
; mid-update interrupt only ever lands above the new object)
; CHECK: mov r{{[0-9]+}}, 0x81
; CHECK: mov r{{[0-9]+}}, 0x85
; CHECK: add wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK: mov 0x85, r{{[0-9]+}}
; CHECK: mov 0x81, r{{[0-9]+}}
; object base = old SPX + 1
; CHECK: add wr{{[0-9]+}}, #0x0001
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+}}
; CHECK: mov dr60, dr16
; CHECK: pop dr16
; CHECK: eret
entry:
  %b = alloca i8, i16 %n
  %p0 = getelementptr inbounds i8, ptr %b, i16 0
  store i8 90, ptr %p0
  %q = load i8, ptr %p0
  %qq = zext i8 %q to i16
  %r = add i16 %n, %qq
  ret i16 %r
}

; A dynamic alloca mixed with a call: the callee gets an exactly restored
; SPX (the anchor), the caller's own frame survives the nested call.
define i8 @dyn_alloca_call(i16 %n) {
; CHECK-LABEL: _dyn_alloca_call:
; CHECK: push dr16
; CHECK: mov dr16, dr60
; CHECK: ecall _f8a
; CHECK: mov dr60, dr16
; CHECK: pop dr16
; CHECK: eret
entry:
  %b = alloca i8, i16 %n
  %p0 = getelementptr inbounds i8, ptr %b, i16 0
  store i8 1, ptr %p0
  %x = call i8 @f8a()
  %q = load i8, ptr %p0
  %s = add i8 %x, %q
  ret i8 %s
}

; Loop VLAs use llvm.stacksave/stackrestore to recover the pre-iteration SPX.
; stacksave returns the current SPX (SFR read 0x81/0x85), stackrestore writes
; it back (hi-first). The function has var-sized objects, so the frame is
; anchored in dr16.
declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)

define void @loop_vla(i16 %n) {
; CHECK-LABEL: _loop_vla:
; CHECK: push dr16
; CHECK: mov dr16, dr60
; stacksave: read SPX
; CHECK: mov r{{[0-9]+}}, 0x81
; CHECK: mov r{{[0-9]+}}, 0x85
; stackrestore: write SPX back hi-first
; CHECK: mov 0x85, r{{[0-9]+}}
; CHECK: mov 0x81, r{{[0-9]+}}
entry:
  br label %loop
loop:
  %s = call ptr @llvm.stacksave()
  %b = alloca i8, i16 %n
  store i8 1, ptr %b
  call void @llvm.stackrestore(ptr %s)
  br label %loop
}
