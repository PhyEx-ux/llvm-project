; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O0 -filetype=obj %s -o %t.O0.rel
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O2 -filetype=obj %s -o %t.O2.rel
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O2 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=MIR
;
; Pinned to the v1 compatibility contract: this is a legacy-layout suite. The
; llc no-flag default is the xsmall/v2-Small model (clang cc1 default).

; IPO can select fastcc for local functions. MCS251 deliberately uses the
; same physical stack-based ABI as C: the first argument/result occupies
; DPL/DPTR or DPL:DPH:B:A, continuation arguments live on the stack (no OSEG/DSEG
; static slots), and CSR_MCS251 = {DR12, WR8}.
define internal fastcc i8 @helper8(i8 %a, i8 %b) {
; CHECK-NOT: .area OSEG
; CHECK-NOT: _helper8_PARM_2
; CHECK-LABEL: _helper8:
; CHECK: mov r{{[0-9]+}}, dpl
; CHECK: mov r{{[0-9]+}}, @dr60-0x0003
; CHECK: add r
; CHECK: mov dpl,
; CHECK: eret
  %r = add i8 %a, %b
  ret i8 %r
}
define internal fastcc i16 @helper16(i16 %a, i16 %b) {
; CHECK-NOT: _helper16_PARM_2
; CHECK-LABEL: _helper16:
; CHECK: mov r{{[0-9]+}}, @dr60-0x0004
; CHECK: add wr
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK: eret
  %r = add i16 %a, %b
  ret i16 %r
}
define internal fastcc i32 @helper32(i32 %a, i32 %b) {
; CHECK-NOT: _helper32_PARM_2
; CHECK-LABEL: _helper32:
; CHECK: mov r{{[0-9]+}}, @dr60-0x0006
; CHECK: add dr
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK: mov b,
; CHECK: mov a,
; CHECK: eret
  %r = add i32 %a, %b
  ret i32 %r
}
define internal fastcc i32 @nested(i32 %a, i32 %b) {
; CHECK-NOT: _nested_PARM_2
; CHECK-LABEL: _nested:
; CHECK: inc spx, #0x4
; CHECK: ecall _helper32
; CHECK: dec spx, #0x4
; CHECK: inc spx, #0x4
; CHECK: ecall _helper32
; CHECK: dec spx, #0x4
; CHECK: eret
; MIR-LABEL: name: nested
; MIR: ADJCALLSTACKDOWN 4, 0
; MIR: ECALL @helper32, csr_mcs251,{{.*}}implicit $dpl,{{.*}}implicit $dph,{{.*}}implicit $b,{{.*}}implicit $a
; MIR: ADJCALLSTACKUP 4, 0
; MIR: ADJCALLSTACKDOWN 4, 0
; MIR: ECALL @helper32, csr_mcs251,
; MIR: ADJCALLSTACKUP 4, 0
  %x = call fastcc i32 @helper32(i32 %a, i32 %b)
  %y = call fastcc i32 @helper32(i32 %b, i32 7)
  %r = add i32 %x, %y
  ret i32 %r
}
define i32 @caller(i32 %x) {
; CHECK-LABEL: _caller:
; CHECK: ecall _helper8
; CHECK: ecall _helper16
; CHECK: ecall _nested
; CHECK: eret
  %a = call fastcc i8 @helper8(i8 250, i8 10)
  %b = call fastcc i16 @helper16(i16 65535, i16 2)
  %c = call fastcc i32 @nested(i32 %x, i32 7)
  %az = zext i8 %a to i32
  %bz = zext i16 %b to i32
  %d = add i32 %c, %az
  %r = add i32 %d, %bz
  ret i32 %r
}

declare i16 @c_function(i16)
define fastcc i16 @fast_to_c(i16 %x) {
; CHECK-LABEL: _fast_to_c:
; CHECK: ecall _c_function
; CHECK: eret
  %r = call i16 @c_function(i16 %x)
  ret i16 %r
}
define i32 @indirect_fast(ptr %fp) {
; CHECK-LABEL: _indirect_fast:
; CHECK: ecall @dr
; CHECK: eret
; MIR-LABEL: name: indirect_fast
; MIR: ECALLr {{.*}}csr_mcs251
  %r = call fastcc i32 %fp(i32 35)
  ret i32 %r
}
