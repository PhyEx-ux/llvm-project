; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs %s -o %t.o2.s
; RUN: FileCheck %s < %t.o2.s

; ISR campaign T09: the card-frozen multi-exit test. An interrupt entry with
; two volatile-armed early exits must carry the complete A6 fixed save on the
; single shared entry path and the complete inverse restore ending in RETI on
; EVERY exit; no exit may fall back to the ordinary ERET.
;
; Text form note (for Alice review): the T09 card text predates the
; 2026-09-09 appendix ruling. Applied here is that ruling's unified form for
; this exact test class (T04/T05/T06 froze the same change): explicit
; -mcs251-memory-contract=1,2,32,8,1 (the llc default contract is V2 and the
; card-era bare-ptr root/AS0 ISR text cannot run), the ISR definition in
; addrspace(4), and the standard addrspacecast llvm.used keepalive root. The
; machine-level assertions pin the full contract: the ordered entry save, the
; complete inverse restore on EVERY exit (each exit anchored by its volatile
; store value), and RETI with no ERET anywhere in the ISR.

target triple = "mcs251-unknown-none"
@flag = global i8 0
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"

define mcs251_intrcc void @irq() addrspace(4) #0 {
entry:
  %v = load volatile i8, ptr @flag
  %z = icmp eq i8 %v, 0
  br i1 %z, label %a, label %b
a:
  store volatile i8 1, ptr @flag
  ret void
b:
  store volatile i8 2, ptr @flag
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }

; CHECK-LABEL: _irq:
;
; Entry: the complete A6 fixed save, order-anchored, on the single shared
; entry path -- PSW, dr0..dr28 stepping by 4, then DPX.
; CHECK: push psw
; CHECK-NEXT: push dr0
; CHECK-NEXT: push dr4
; CHECK-NEXT: push dr8
; CHECK-NEXT: push dr12
; CHECK-NEXT: push dr16
; CHECK-NEXT: push dr20
; CHECK-NEXT: push dr24
; CHECK-NEXT: push dr28
; CHECK-NEXT: push dpx
; CHECK-NOT: push
; CHECK-NOT: eret
;
; Exit for %a (armed by the volatile store of 1): complete inverse restore,
; then POP PSW and RETI.
; CHECK: mov r4, #0x01
; CHECK: mov @dr0, r4
; CHECK-NEXT: pop dpx
; CHECK-NEXT: pop dr28
; CHECK-NEXT: pop dr24
; CHECK-NEXT: pop dr20
; CHECK-NEXT: pop dr16
; CHECK-NEXT: pop dr12
; CHECK-NEXT: pop dr8
; CHECK-NEXT: pop dr4
; CHECK-NEXT: pop dr0
; CHECK-NEXT: pop psw
; CHECK-NEXT: reti
; CHECK-NOT: push
; CHECK-NOT: eret
;
; Exit for %b (armed by the volatile store of 2): same complete inverse
; restore, then POP PSW and RETI.
; CHECK: mov r4, #0x02
; CHECK: mov @dr0, r4
; CHECK-NEXT: pop dpx
; CHECK-NEXT: pop dr28
; CHECK-NEXT: pop dr24
; CHECK-NEXT: pop dr20
; CHECK-NEXT: pop dr16
; CHECK-NEXT: pop dr12
; CHECK-NEXT: pop dr8
; CHECK-NEXT: pop dr4
; CHECK-NEXT: pop dr0
; CHECK-NEXT: pop psw
; CHECK-NEXT: reti
; CHECK-NOT: eret
