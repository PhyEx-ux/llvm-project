; RUN: llc -mtriple=mcs251 -stop-after=finalize-isel -verify-machineinstrs -o - %s | FileCheck %s

; Phase 7 MIR lock (finalize-isel, before register allocation): the selected
; ECALL must carry the CSR_MCS251 register mask. The mask is the ONLY thing
; that tells the register allocator that every allocatable GPR dies across
; the call -- if it were ever dropped from the MCS251ISD::CALL operands or
; the selection, values live across calls would be silently kept in
; clobbered registers (no assembler-level test can catch that; call.ll only
; sees post-RA code where RA happened to not need the fact). Also lock the
; implicit PSW def (flag clobber) and the implicit use AND def of DPL: the
; use is the ABI argument slot from CopyToReg, the def is the result slot
; read by CopyFromReg -- together they pin the call's ABI locations onto
; the instruction itself.

declare i8 @g8p(i8)

define i8 @call_arg8(i8 %a) {
; CHECK-LABEL: name: call_arg8
; CHECK:       ECALL @g8p, csr_mcs251,{{.*}}implicit-def dead $psw,{{.*}}implicit $dpl,{{.*}}implicit-def $dpl
; CHECK-NEXT:  ADJCALLSTACKUP 0, 0
; CHECK-NEXT:  ERET implicit $dpl
  %t = add i8 %a, 1
  %r = call i8 @g8p(i8 %t)
  ret i8 %r
}
