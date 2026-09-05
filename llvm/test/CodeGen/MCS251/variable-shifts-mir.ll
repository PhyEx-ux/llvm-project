; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -stop-after=finalize-isel %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -stop-after=finalize-isel %s -o - | FileCheck %s

; Counter PHI is constrained to low byte registers, independent of the full
; DR value. Guard zero before the body, decrement as a NON-terminator (so
; FastRA can spill NextCount on both edges), and use a short forward branch
; to a long-jump trampoline. No flags produced by SRL32one escape as a test
; of the 32-bit value: the final icmp has a fresh CMP32rr.
define i8 @shift_test(i32 %x, i32 %n) {
; CHECK-LABEL: name: shift_test
; CHECK: %[[COUNT:[0-9]+]]:gpr8low = COPY
; CHECK-NEXT: CMP8ri %[[COUNT]], 0, implicit-def $psw
; CHECK-NEXT: JE %bb.[[ZERO:[0-9]+]], implicit $psw
; CHECK-NEXT: EJMP %bb.[[LOOP:[0-9]+]]
; CHECK: bb.[[ZERO]]:
; CHECK: EJMP %bb.[[DONE:[0-9]+]]
; CHECK: bb.[[LOOP]]:
; CHECK: %[[VAL:[0-9]+]]:gpr32 = PHI
; CHECK-NEXT: %[[REM:[0-9]+]]:gpr8low = PHI %[[COUNT]], %bb.0, %[[NEXT:[0-9]+]], %bb.[[BACK:[0-9]+]]
; CHECK-NEXT: %[[SHIFT:[0-9]+]]:gpr32 = SRL32one %[[VAL]], implicit-def $a, implicit-def $psw
; CHECK-NEXT: %[[NEXT]]:gpr8low = SUB8ri %[[REM]], 1, implicit-def $psw
; CHECK-NEXT: JNE %bb.[[BACK]], implicit $psw
; CHECK-NEXT: EJMP %bb.[[DONE]]
; CHECK: bb.[[BACK]]:
; CHECK: EJMP %bb.[[LOOP]]
; CHECK: bb.[[DONE]]:
; CHECK: %[[RESULT:[0-9]+]]:gpr32 = PHI
; CHECK: CMP32rr {{(killed )?}}%[[RESULT]], {{.*}}implicit-def $psw
  %s = lshr i32 %x, %n
  %c = icmp eq i32 %s, 0
  %r = zext i1 %c to i8
  ret i8 %r
}
