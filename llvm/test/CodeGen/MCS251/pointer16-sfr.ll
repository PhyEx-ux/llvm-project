; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Explicit AS6 byte constants select the direct SFR family, never @WR.

define i8 @sfr_uart_read() {
; CHECK-LABEL: _sfr_uart_read:
; CHECK: mov {{r[0-9]+}}, 0x99
; CHECK-NOT: @wr
  %v = load volatile i8, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  ret i8 %v
}

define void @sfr_uart_write(i8 %v) {
; CHECK-LABEL: _sfr_uart_write:
; CHECK: mov 0x99, {{r[0-9]+}}
; CHECK-NOT: @wr
  store volatile i8 %v, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  ret void
}
